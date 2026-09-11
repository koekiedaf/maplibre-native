#pragma once

#include <mln/map/mode.hpp>
#include <mln/style/terrain_impl.hpp>
#include <mln/util/immutable.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/util/constants.hpp>
#include <mln/gfx/vertex_buffer.hpp>
#include <mln/gfx/index_buffer.hpp>
#include <mln/renderer/texture_pool.hpp>
#include <mln/util/mat4.hpp>

#include <array>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace mln {

class LatLng;
class TransformState;
class UpdateParameters;
class RenderSource;
class PaintParameters;
class RenderTree;
class LayerGroupBase;
class TerrainLayerTweaker;
class DEMData;
using LayerGroupBasePtr = std::shared_ptr<LayerGroupBase>;
using UniqueChangeRequestVec = std::vector<std::unique_ptr<class ChangeRequest>>;

namespace gfx {
class Context;
class Drawable;
class ShaderRegistry;
class Texture2D;
} // namespace gfx

#if MLN_RENDER_BACKEND_OPENGL
namespace gl {
class Texture2DArray;
} // namespace gl
#endif

/**
 * @brief Manages 3D terrain rendering using DEM (Digital Elevation Model) data
 *
 * RenderTerrain is responsible for:
 * - Loading and caching DEM tiles from raster-dem sources
 * - Generating and caching terrain mesh geometry
 * - Providing elevation lookups for any coordinate
 * - Managing GPU resources for terrain rendering
 */
class RenderTerrain {
public:
    RenderTerrain(Immutable<style::Terrain::Impl>);
    ~RenderTerrain();

    /// The terrain mesh (and drape render target) tile set, computed as an
    /// elevation-aware, LOD-based ideal cover directly from the transform - like
    /// maplibre-gl-js `coveringTiles({tileSize: 512, terrain})` - rather than
    /// from the DEM source's loaded render tiles. This matters for sparse DEMs
    /// (e.g. mapterhorn), where high-zoom tiles 404 and native falls back to a
    /// lower-zoom DEM tile: deriving the mesh cover from that fallback set would
    /// drag the drape resolution down with it (large low-zoom drape targets =
    /// blurry roads) and leave uncovered areas as skirt until the DEM loads. The
    /// mesh cover instead stays at the view's ideal zoom; the DEM/drape textures
    /// fall back to ancestors per tile while exact tiles load. Needs the DEM
    /// source, so it is a member (not static). Returns {} when there is none.
    std::set<UnwrappedTileID> computeMeshCover(const TransformState& state,
                                               const std::shared_ptr<UpdateParameters>& updateParameters) const;

    /// Resolve the DEM render source from the orchestrator if not yet bound.
    /// Renderer::Impl::render calls this before building the frame's drape-target
    /// pool from computeMeshCover: the source is otherwise first bound inside
    /// update(), which runs after the pool is built, so the FIRST frame after a
    /// style (re)load computed an empty cover -> zero drape targets -> update()
    /// created no terrain drawables -> terrain rendered nothing. Continuous
    /// rendering hid this (frame 2 recovers); single-frame still renders - the
    /// render tests - stayed blank.
    void prepareSource(class RenderOrchestrator& orchestrator);

    /// Cache the mesh cover Renderer::Impl computed for this frame, so update()
    /// meshes exactly the tile set the drape-target pool was built from.
    /// computeMeshCover() is not stable within a frame: it derives its tile size
    /// from `demDim`, which is 0 until the first DEM decodes and only then takes
    /// the source's real value. For a 256px DEM the pre-pool call therefore falls
    /// back to 512 and covers a different (shallower) tile set than the in-update
    /// call, so every getRenderTarget() lookup missed and no terrain drawable was
    /// ever created - the map rendered empty.
    /// DuckMaps fork only, task C5: also copies the cover into lastFrameMeshCover before
    /// moving it into frameMeshCover, so the debug elevation trace can report the mesh
    /// cover the frame actually used after update() consumes (and clears) frameMeshCover
    /// below - see getLastFrameMeshCoverTileIds.
    void setFrameMeshCover(std::set<UnwrappedTileID> cover) {
        lastFrameMeshCover = cover;
        frameMeshCover = std::move(cover);
    }

    /**
     * @brief Update terrain rendering (create/update drawables)
     * @param orchestrator Render orchestrator for accessing render sources
     * @param shaders Shader registry for getting terrain shader
     * @param context Graphics context for creating drawables and layer groups
     * @param state Transform state
     * @param updateParameters Update parameters
     * @param renderTree Render tree
     * @param changes Vector to collect change requests
     */
    void update(class RenderOrchestrator& orchestrator,
                gfx::ShaderRegistry& shaders,
                gfx::Context& context,
                const TexturePool& texturePool,
                const TransformState& state,
                const std::shared_ptr<UpdateParameters>& updateParameters,
                const RenderTree& renderTree,
                UniqueChangeRequestVec& changes);

    /**
     * @brief Move tile-local coordinates outside [0, EXTENT) onto the neighbouring tile
     *
     * Geometry that crosses a tile edge - a label placed along a line, or a map centre
     * near a boundary - asks for elevation beyond the tile it started in. Clamping such a
     * coordinate to the tile edge answers with the wrong elevation and looks plausible;
     * resolving it to the tile that actually contains the point does not. Mirrors
     * maplibre-gl-js `OverscaledTileID.normalizeCoordinates` (#7040).
     *
     * @param tileID [in,out] the tile the coordinates are relative to; replaced by the tile
     *               that contains them, wrapping around the antimeridian
     * @param x [in,out] x relative to the tile, may start outside [0, EXTENT)
     * @param y [in,out] y relative to the tile, may start outside [0, EXTENT)
     * @return false when the point lies past a pole, where there is no tile to resolve to
     */
    static bool normalizeTileCoordinates(UnwrappedTileID& tileID, float& x, float& y);

    /**
     * @brief Get elevation at a specific tile coordinate
     * @param tileID The tile containing the coordinate
     * @param x X coordinate within the tile, may be outside [0, EXTENT)
     * @param y Y coordinate within the tile, may be outside [0, EXTENT)
     * @return Elevation in meters (or 0 if no DEM data available)
     */
    float getElevation(const UnwrappedTileID& tileID, float x, float y) const;

    /**
     * @brief Get elevation with exaggeration applied
     * @param tileID The tile containing the coordinate
     * @param x X coordinate within the tile
     * @param y Y coordinate within the tile
     * @return Elevation in meters with exaggeration multiplier applied
     */
    float getElevationWithExaggeration(const UnwrappedTileID& tileID, float x, float y) const;

    /**
     * @brief Exaggerated terrain height under a geographic position
     *
     * Samples at the depth of the finest DEM tile currently loaded. `getElevation` only
     * matches a DEM tile that is the sample tile or an ancestor of it, so a fixed sample
     * zoom silently reads 0 wherever the DEM is loaded deeper than that.
     *
     * @param latLng the position to sample
     * @return height in metres of the rendered surface, or 0 when no DEM covers it
     */
    double getElevationForLatLng(const LatLng& latLng) const;

    /**
     * @brief Exaggerated terrain height under a geographic position, honestly reporting
     * when there is nothing to sample
     *
     * Same walk as `getElevationForLatLng` (sample at the finest DEM zoom currently loaded,
     * matching the sample tile or its closest loaded ancestor), but returns `std::nullopt`
     * instead of 0 wherever that walk does not land on a real, decoded DEM tile: no DEM
     * source, no DEM tile loaded anywhere in view, past a pole, or the resolved tile/ancestor
     * has no bucket or no decoded image (which is also what happens when nothing has loaded
     * there yet and only the flat 1x1 placeholder DEM would be available for rendering).
     * `getElevationForLatLng` cannot be reused for this because it folds all of those cases
     * into the same 0.0 as genuine sea level; this is the version that keeps them apart.
     *
     * @param latLng the position to sample
     * @return height in metres of the rendered surface, or nullopt when no loaded DEM tile
     * covers it
     */
    std::optional<double> queryElevationForLatLng(const LatLng& latLng) const;

    /**
     * @brief DuckMaps fork only, task C1: debug instrumentation for the elevation query.
     *
     * Result of `probeElevationForLatLng`: the exaggerated elevation `queryElevationForLatLng`
     * would have returned, plus the identity of the DEM tile that actually served it, so a trace
     * can show that the elevation of a fixed point is a function of which DEM tile happens to be
     * resident, not of the point.
     */
    struct ElevationProbe {
        bool hit = false;    ///< false when no loaded DEM tile covers the point at all
        float meters = 0.0f; ///< exaggerated elevation in metres; meaningless when !hit
        uint8_t demZ = 0;    ///< canonical z of the DEM tile that served the sample
        uint32_t demX = 0;   ///< canonical x of the DEM tile that served the sample
        uint32_t demY = 0;   ///< canonical y of the DEM tile that served the sample
        bool exact = false;  ///< true when demZ equalled the requested sample tile's z (no
                              ///< ancestor fallback)
    };

    /**
     * @brief DuckMaps fork only, task C1: same "sample as deep as the finest DEM tile loaded"
     * walk as `queryElevationForLatLng`, but also reports which DEM tile served the sample.
     * Debug-only; not called unless the elevation trace (`DUCKMAPS_ELEVATION_TRACE`) is on.
     *
     * @param latLng the position to sample
     * @return an ElevationProbe with hit=false when no loaded DEM tile covers the point
     */
    ElevationProbe probeElevationForLatLng(const LatLng& latLng) const;

    /**
     * @brief DuckMaps fork only, task C1: the canonical z/x/y of every DEM tile currently
     * resident in the terrain's DEM source (the same `demSource->getRawRenderTiles()` scanned
     * by `getElevation`/`queryElevation`/`probeElevationForLatLng`), for the debug elevation
     * trace's `demTiles` field. Debug-only; not called unless the elevation trace is on. Not
     * wrap-aware: the trace only needs which physical tiles are loaded, not which copy of the
     * antimeridian each is currently wrapped to.
     */
    std::vector<CanonicalTileID> getResidentDemTileIds() const;

    /**
     * @brief DuckMaps fork only, task C5: the canonical z/x/y of every tile in the mesh
     * cover Renderer::Impl computed for the last rendered frame (see setFrameMeshCover),
     * for the debug elevation trace's `meshCover` field. This is the cover the frame's
     * drape-target pool and terrain mesh actually used, not a recomputation -
     * computeMeshCover() is not stable within a frame (see setFrameMeshCover's doc
     * comment above), so calling it again here would not match what was drawn.
     * Debug-only; not called unless the elevation trace is on. Empty before the first
     * frame that has terrain.
     */
    std::vector<CanonicalTileID> getLastFrameMeshCoverTileIds() const;

    /**
     * @brief Get the terrain exaggeration multiplier
     */
    float getExaggeration() const;

    /**
     * @brief Get the source ID providing DEM data
     */
    const std::string& getSourceID() const;

    /**
     * @brief Check if terrain is enabled and has DEM data
     */
    bool isEnabled() const;

    /**
     * @brief Remove the terrain's mesh layer group from the orchestrator.
     *
     * Must be queued (and the changes processed) before this RenderTerrain is
     * destroyed. activateLayerGroup() registers the mesh layer group with the
     * orchestrator, which then holds its own reference; dropping RenderTerrain
     * alone leaves that group behind, so the orchestrator keeps drawing an
     * orphaned terrain surface (a second, floating terrain layer appears after
     * the user toggles 3D terrain off and back on).
     */
    void deactivate(UniqueChangeRequestVec& changes);

    /**
     * @brief Get the DEM unpack vector for the source's raster-dem encoding
     */
    const std::array<float, 4>& getDEMUnpackVector() const { return demUnpackVector; }

    /**
     * @brief DuckMaps fork only, task 2.4a: the tile ids RenderTerrain currently has a terrain
     * drawable for (the keys of tilesWithDrawables, equivalently of drawableDemCoords) - the
     * cover terrain-contour follows, one of its own drawables per tile here, since it draws over
     * RenderTerrain's own mesh rather than tracking renderTiles/a source of its own (see
     * RenderTerrainContourLayer::update).
     */
    std::vector<OverscaledTileID> getTilesWithDrawables() const {
        std::vector<OverscaledTileID> tileIDs;
        tileIDs.reserve(tilesWithDrawables.size());
        for (const auto& entry : tilesWithDrawables) {
            tileIDs.push_back(entry.first);
        }
        return tileIDs;
    }

    /**
     * @brief {scale, x offset, y offset, DEM dim} mapping a terrain drawable's
     * tile-local position (0..EXTENT) into its bound DEM texture's normalized
     * space, for the shader's get_elevation() (see the demCoords built in update)
     */
    std::array<float, 4> getDrawableDemCoords(const OverscaledTileID& tileID) const {
        const auto it = drawableDemCoords.find(tileID);
        return it != drawableDemCoords.end()
                   ? it->second
                   : std::array<float, 4>{{1.0f / util::EXTENT, 0.0f, 0.0f, static_cast<float>(demDim)}};
    }

    /**
     * @brief Per-tile DEM binding data for layers that sample elevation in their
     * vertex shaders (the native analog of maplibre-gl-js terrain.getTerrainData)
     */
    struct TerrainData {
        std::shared_ptr<gfx::Texture2D> demTexture;
        /// scale and x/y offset mapping tile-local coordinates (0..EXTENT) of the
        /// requested tile into normalized coordinates (0..1) of the DEM tile
        std::array<float, 4> demCoords;
        float demDim;
    };

    /**
     * @brief Get the DEM texture and coordinate mapping covering the given tile,
     * from the matching DEM tile or its closest available ancestor
     */
    std::optional<TerrainData> getTerrainData(const UnwrappedTileID&) const;

    /**
     * @brief A 1x1 zero-elevation DEM texture bound when no DEM tile is available,
     * so shaders that declare the DEM sampler always have a valid binding
     */
    const std::shared_ptr<gfx::Texture2D>& getPlaceholderDEMTexture(gfx::Context&);

    /**
     * @brief Get the terrain implementation
     */
    const Immutable<style::Terrain::Impl>& getImpl() const { return impl; }

    /**
     * @brief Get the terrain mesh for a specific tile
     *
     * Returns a cached mesh or generates a new one. The mesh is a regular grid
     * that will be displaced by DEM data in the vertex shader.
     *
     * @param tileID The tile ID (unused currently - same mesh for all tiles)
     * @return Pointer to vertex buffer and index buffer
     */
    struct TerrainMesh {
        std::shared_ptr<gfx::VertexBuffer<float>> vertexBuffer;
        std::shared_ptr<gfx::IndexBuffer> indexBuffer;
        size_t vertexCount;
        size_t indexCount;
        std::vector<int16_t> vertices; // Raw vertex data (x, y pairs as short2)
        std::vector<uint16_t> indices; // Raw index data
    };

    const TerrainMesh& getMesh(gfx::Context& context);

    /// Mesh used by the instanced depth pass. Aliased to the full terrain mesh for now;
    /// the coarser depth-only mesh optimization from the source PR can be pulled separately.
    const TerrainMesh& getDepthMesh(gfx::Context& context);

    /**
     * @brief Get the layer group for terrain drawables
     */
    const LayerGroupBasePtr& getLayerGroup() const { return layerGroup; }

    /**
     * @brief Render the terrain depth pass: the terrain meshes drawn with the
     * depth-packing shader into a viewport-sized render target, sampled by the
     * symbol shaders for occlusion (maplibre-gl-js calculate_visibility)
     */
    void renderDepth(RenderOrchestrator&, const RenderTree&, PaintParameters&);

    /**
     * @brief Ensure the depth render target exists for this frame's size.
     *
     * Must run before the upload phase so the symbol tweaker binds the real
     * depth texture rather than the far-plane placeholder; renderDepth then
     * renders into it later the same frame, before the symbols draw.
     */
    void prepareDepthTarget(PaintParameters&);

    /// The packed-RGBA terrain depth texture for the current frame, or a 1x1
    /// far-plane placeholder while the depth pass has not rendered yet
    const std::shared_ptr<gfx::Texture2D>& getDepthTexture(gfx::Context&);

    /// Layer group holding the terrain depth-pass drawables (not part of the
    /// orchestrator; rendered only by renderDepth)
    const LayerGroupBasePtr& getDepthLayerGroup() const { return depthLayerGroup; }

    /**
     * @brief Get the terrain layer tweaker
     */
    TerrainLayerTweaker* getTweaker() const { return tweaker.get(); }

    // Immutable terrain configuration
    Immutable<style::Terrain::Impl> impl;

private:
    /**
     * @brief Get elevation at a specific tile coordinate, honestly reporting when there is no
     * loaded DEM tile to sample
     *
     * Same tile walk as `getElevation` (the requested tile, or its closest loaded ancestor,
     * among the DEM source's raw render tiles), but returns `std::nullopt` in every case
     * `getElevation` instead returns a bare 0.0f for lack of anything better: no DEM source,
     * off the tile grid past a pole, no covering tile found, the covering tile is not a decoded
     * RasterDEM tile, or it has no bucket / no decoded image yet.
     * @param tileID The tile containing the coordinate
     * @param x X coordinate within the tile, may be outside [0, EXTENT)
     * @param y Y coordinate within the tile, may be outside [0, EXTENT)
     * @return Elevation in meters, or nullopt when no loaded DEM tile covers the point
     */
    std::optional<float> queryElevation(const UnwrappedTileID& tileID, float x, float y) const;

    /**
     * @brief DuckMaps fork only, task C1: the scan-and-bilinear body shared by `getElevation`,
     * `queryElevation` and `probeElevationForLatLng`, factored out so the probe is not a third
     * copy. Behaviour-identical to the two bodies it replaces: same tile walk (requested tile or
     * its closest loaded ancestor among `demSource->getRawRenderTiles()`, picking the candidate
     * with the highest `canonical.z`), same bilinear interpolation. `hit` is false in every case
     * `getElevation` used to fall back to a bare 0.0f (no DEM source, past a pole, no covering
     * tile, not a decoded RasterDEM tile, no bucket, no decoded image) and every case
     * `queryElevation` used to return `std::nullopt`.
     * @param tileID The tile containing the coordinate
     * @param x X coordinate within the tile, may be outside [0, EXTENT)
     * @param y Y coordinate within the tile, may be outside [0, EXTENT)
     */
    struct ElevationSample {
        bool hit = false;
        float meters = 0.0f;
        uint8_t demZ = 0;
        uint32_t demX = 0;
        uint32_t demY = 0;
        bool exact = false;
    };
    ElevationSample findElevationSample(const UnwrappedTileID& tileID, float x, float y) const;

    /**
     * @brief Generate terrain mesh geometry
     *
     * Creates a regular grid mesh (default 128x128) with border frames
     * to prevent stitching artifacts between tiles.
     */
    void generateMesh(gfx::Context& context);

    /**
     * @brief Activate or deactivate the layer group
     */
    void activateLayerGroup(bool activate, UniqueChangeRequestVec& changes);

    // Terrain mesh (shared across all tiles)
    std::optional<TerrainMesh> mesh;
    // The skirt setting the cached mesh was built with. update() drops the mesh and every
    // tile drawable built from it when the map's setting no longer matches.
    TerrainSkirtLength meshSkirtLength = TerrainSkirtLength::Auto;

    // Layer group for terrain drawables
    LayerGroupBasePtr layerGroup;

    // Layer group and viewport-sized target for the terrain depth pass
    LayerGroupBasePtr depthLayerGroup;
    std::shared_ptr<RenderTarget> depthRenderTarget;
    // The depth pass output only changes when the camera moves or the terrain mesh
    // set changes, so it is re-rendered only then (as maplibre-gl-js's maybeDrawDepth
    // does) instead of every frame. depthDirty is set when depth drawables are added
    // or removed; lastDepthProjMatrix detects camera movement.
    bool depthDirty = true;
    std::optional<mat4> lastDepthProjMatrix;
    // See getDepthTexture
    std::shared_ptr<gfx::Texture2D> placeholderDepthTexture;

    // Terrain layer tweaker for UBO updates
    std::unique_ptr<TerrainLayerTweaker> tweaker;

    // Track which tiles have terrain drawables; the value is true when the
    // drawable samples the tile's own DEM texture, false when it is using an
    // ancestor tile's DEM as a fallback while its own DEM is still loading
    // Mesh drawables by tile, with the DEM quality tier they were built with
    // (0 = placeholder/flat, 1 = ancestor fallback, 2 = own DEM); a drawable
    // is replaced whenever a higher tier becomes available
    std::unordered_map<OverscaledTileID, uint8_t> tilesWithDrawables;

    // Per-drawable scale/offset into the bound DEM texture ({1,0,0,0} unless
    // an ancestor tile's DEM is bound); read by the terrain layer tweaker
    std::map<OverscaledTileID, std::array<float, 4>> drawableDemCoords;
#if MLN_RENDER_BACKEND_OPENGL
    // Per-tile demTextureArray layer (own or ancestor DEM), -1 when the tile has no packed DEM;
    // feeds the instanced depth pass (see rebuildInstancedDepthDrawable / depthInstances).
    std::map<OverscaledTileID, float> drawableDemLayer;
#endif

    // Mesh resolution (grid cells per side)
    static constexpr size_t MESH_SIZE = 128;

    // Log the camera eye's clearance over the rendered terrain (throttled), so tests can report
    // when the sea-level-anchored camera dips below terrain (TERRAIN.md Phase 4). See the .cpp.
    void logAboveGroundMargin(const TransformState& state);
    double lastAboveGroundLog = 0.0;
    static constexpr double kAboveGroundLogInterval = 0.25; // seconds
    // Only log when the eye is within this clearance of the terrain (or below it); above this,
    // stay silent to keep normal viewing noise-free. Also filters DEM-miss (groundM==0) rows.
    static constexpr double kAboveGroundAlertM = 1000.0; // metres

    // The mesh-tile cap now comes from the per-map TerrainLoadMode
    // (TerrainLoadBudget::maxMeshTiles) rather than a single constant, so Quality keeps a long
    // terrain render distance while Balanced/Performance trade it for frame time. See
    // RenderTerrain::update.

    // Cached DEM source
    RenderSource* demSource = nullptr;
    /// Mesh cover for the current frame, set by Renderer::Impl before the drape
    /// target pool is built; consumed (and cleared) by update()
    std::optional<std::set<UnwrappedTileID>> frameMeshCover;
    /// DuckMaps fork only, task C5: a copy of the last frame's mesh cover (see
    /// setFrameMeshCover above), kept for the debug elevation trace after update()
    /// consumes and clears frameMeshCover.
    std::set<UnwrappedTileID> lastFrameMeshCover;

    // DEM decode vector for the source's encoding (default: Mapbox Terrain-RGB)
    std::array<float, 4> demUnpackVector = {{6553.6f, 25.6f, 0.1f, 10000.0f}};

    // DEM tile inner dimension (e.g. 256/512), source-constant; rides in
    // dem_coords.w so the shader's get_elevation() knows the texel grid
    int32_t demDim = 0;

    // DEM textures by tile, for elevation sampling by non-draped layers
    struct DEMTextureEntry {
        std::shared_ptr<gfx::Texture2D> texture;
        int32_t dim;
        uint64_t lastUsed = 0;
    };
    std::map<UnwrappedTileID, DEMTextureEntry> demTextures;
    // Retention cap for demTextures (~1MB per 514x514 DEM texture); entries not
    // used in the current frame are evicted least-recently-used first beyond
    // this, preventing unbounded growth while browsing (previously reached 2GB+)
    static constexpr size_t maxDEMTextures = 96;
    uint64_t demUpdateCounter = 0;

#if MLN_RENDER_BACKEND_OPENGL
    // OpenGL-only: the same DEM tiles packed into one GL_TEXTURE_2D_ARRAY so the
    // depth pass (and later the surface pass) can sample a per-instance layer in a
    // single instanced draw instead of one draw per tile. Populated alongside the
    // per-tile textures above (which remain the source of truth for CPU elevation
    // and non-instanced sampling); a simple free-list recycles layer slots as tiles
    // come and go. Cap the layer count to keep the array a bounded size.
    static constexpr uint32_t maxDEMArrayLayers = 64;
    std::unique_ptr<gl::Texture2DArray> demTextureArray;
    std::map<UnwrappedTileID, uint32_t> demArrayLayer; // tile -> array layer index
    std::vector<uint32_t> demArrayFreeLayers;          // recycled layer slots
    uint32_t demArrayNextLayer = 0;                    // next never-used slot
    void packDEMArrayLayer(gfx::Context&, const UnwrappedTileID&, const DEMData&);
    void freeDEMArrayLayer(const UnwrappedTileID&);

    // Instanced depth pass: one draw covers every mesh tile, sampling each tile's DEM from
    // its demTextureArray layer (see terrain_depth.vertex / TerrainDepthInstanceUBO). Rebuilt
    // when the tile set changes; the per-instance transform matrix is refreshed every frame in
    // updateInstancedDepthUBO (renderDepth), since it depends on the camera.
    static constexpr uint32_t maxDepthInstances = 64; // must match TERRAIN_MAX_INSTANCES in shader
    struct DepthInstance {
        OverscaledTileID tileID;
        std::array<float, 4> demCoords; // scale, x/y offset, dem_dim (.w)
        float demLayer;                 // demTextureArray layer for this tile (own or ancestor)
    };
    std::vector<DepthInstance> depthInstances; // current tile set, index == a_instance / gl_InstanceID
    std::size_t depthInstanceSignature = 0;    // hash of the tile set the instanced drawable was built for
    gfx::UniformBufferPtr depthInstanceUBO;    // TerrainDepthInstanceUBO[N], refreshed per frame
    void rebuildInstancedDepthDrawable(gfx::Context&, gfx::ShaderRegistry&);
    void updateInstancedDepthUBO(PaintParameters&);
#endif

    // See getPlaceholderDEMTexture
    std::shared_ptr<gfx::Texture2D> placeholderDEMTexture;

    // Layer index (terrain renders early in 3D pass, use negative index)
    static constexpr int32_t TERRAIN_LAYER_INDEX = -1000;

    /**
     * @brief Create a DEM texture from DEMData
     * @param context Graphics context
     * @param demData DEM elevation data
     * @return Shared pointer to created texture
     */
    std::shared_ptr<gfx::Texture2D> createDEMTexture(gfx::Context& context, const DEMData& demData);

    /**
     * @brief Create a terrain drawable for a specific tile
     * @param context Graphics context
     * @param shaders Shader registry
     * @param tileID Tile ID for this drawable
     * @param demTexture DEM texture for elevation data
     * @return Unique pointer to created drawable
     */
    std::unique_ptr<gfx::Drawable> createDrawableForTile(gfx::Context& context,
                                                         gfx::ShaderRegistry& shaders,
                                                         const OverscaledTileID& tileID,
                                                         std::shared_ptr<gfx::Texture2D> demTexture,
                                                         std::shared_ptr<gfx::Texture2D> mapTexture,
                                                         bool depthPass = false);
};

} // namespace mln
