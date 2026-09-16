#include <mln/renderer/render_terrain.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/renderer/render_source.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/renderer/render_pass.hpp>
#include <mln/renderer/render_tree.hpp>
#include <mln/renderer/render_static_data.hpp>
#include <mln/renderer/render_orchestrator.hpp>
#include <mln/renderer/render_target.hpp>
#include <mln/renderer/dem_elevation_provider.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/change_request.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/layers/terrain_layer_tweaker.hpp>
#include <mln/renderer/buckets/hillshade_bucket.hpp>
#include <mln/geometry/dem_data.hpp>
#include <mln/util/tile_cover.hpp>
#include <mln/tile/raster_dem_tile.hpp>
#include <mln/tile/tile.hpp>
#if MLN_RENDER_BACKEND_OPENGL
#include <mln/gl/context.hpp>
#include <mln/gl/texture_2d_array.hpp>
#include <mln/gl/drawable_gl.hpp> // gl::DrawableGL::setArrayTexture for the instanced depth DEM
#endif
#include <mln/gfx/context.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/drawable_impl.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/gfx/color_mode.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/terrain_layer_ubo.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/shaders/segment.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/projection.hpp>
#include <mln/math/angles.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/image.hpp>
#include <mln/util/mat4.hpp>
#include <mln/util/monotonic_timer.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/map/camera.hpp>
#include <mln/util/convert.hpp>         // util::cast for the instanced depth UBO matrix
#include <mln/util/hash.hpp>            // util::hash_combine for the depth-instance set signature
#include <mln/gfx/vertex_attribute.hpp> // VertexAttributeArray for the a_instance attribute

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace mln {

namespace {

// Scale and x/y offset mapping a child tile's local space into the (possibly
// ancestor) DEM tile that covers it: a child dz levels deeper occupies the
// 1/scale sub-square at (dx, dy) of the ancestor.
struct DEMSubTileOffset {
    float scale;
    float dx;
    float dy;
};

DEMSubTileOffset demSubTileOffset(const CanonicalTileID& child, const CanonicalTileID& ancestor) {
    const int dz = child.z - ancestor.z;
    return {static_cast<float>(1u << dz),
            static_cast<float>(child.x - (ancestor.x << dz)),
            static_cast<float>(child.y - (ancestor.y << dz))};
}

// DuckMaps fork only, debug instrumentation for task R1A: 64-bit FNV-1a, folded over `data` and
// chained from `hash` so a caller can hash several disjoint byte ranges (e.g. a DEM tile's
// border ring, which is not one contiguous run) as one value. Not used by any shipping path.
uint64_t fnv1a64(const uint8_t* data, size_t len, uint64_t hash = 0xcbf29ce484222325ULL) {
    constexpr uint64_t prime = 0x100000001b3ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= prime;
    }
    return hash;
}

// DuckMaps fork only: what "overlap" means for the mesh cover dilation. Two tiles cover the
// same ground when they are the same tile, or one is an ancestor of the other in the quadtree
// (same wrap - a different wrap is a different copy of the world and never overlaps, however
// the x/y compare). This is deliberately NOT a frustum test: frustumCull answers "is this
// tile's (elevation-extended) footprint on screen", which a culled coarse tile and a visible
// fine descendant can both answer "yes" to while covering the identical ground - that is
// exactly the fault the one-ring dilation had (a z12 neighbour landing on z13/z14/z15 tiles
// the cover already held). Whether two tiles overlap is a property of their coordinates alone,
// never of visibility, so a visibility test can never substitute for this one.
bool tilesOverlap(const UnwrappedTileID& a, const UnwrappedTileID& b) {
    if (a.wrap != b.wrap) {
        return false;
    }
    if (a.canonical.z == b.canonical.z) {
        return a.canonical.x == b.canonical.x && a.canonical.y == b.canonical.y;
    }
    return a.isChildOf(b) || b.isChildOf(a);
}

// DuckMaps fork only: whether `tile`'s ENTIRE footprint is already spoken for by a single
// coarser-or-equal tile in `tiles` - the only case where dropping `tile` outright loses no
// ground, because that one existing tile's footprint is a superset of `tile`'s.
bool hasAncestorOrSelfIn(const UnwrappedTileID& tile, const std::set<UnwrappedTileID>& tiles) {
    for (const auto& t : tiles) {
        if (t.wrap == tile.wrap && (t == tile || tile.isChildOf(t))) {
            return true;
        }
    }
    return false;
}

// DuckMaps fork only: whether `tiles` holds anything strictly INSIDE `tile`'s footprint - i.e.
// `tile` is at least partially (maybe, but not provably, wholly) already covered from below.
// On its own this says nothing about the rest of `tile`'s area, which is exactly why a
// dilation candidate cannot simply be rejected on this alone (see collectUncoveredParts).
bool hasDescendantIn(const UnwrappedTileID& tile, const std::set<UnwrappedTileID>& tiles) {
    for (const auto& t : tiles) {
        if (t.wrap == tile.wrap && t.isChildOf(tile)) {
            return true;
        }
    }
    return false;
}

// DuckMaps fork only: the exact ground-level fix for the dilation's overlap fault. Rejecting a
// whole dilation candidate the moment ANY part of it overlaps `out` was the first attempt at
// this fix, and it traded one regression for another: a coarse frontier neighbour typically
// overlaps `out` only along the narrow strip where the view frustum's own boundary cuts across
// its footprint (that boundary has no reason to land on a tile edge), so most of that
// neighbour's area is genuinely new ground the dilation exists to add - rejecting the whole
// tile for a sliver of overlap reintroduced the skirts/holes the dilation itself was written to
// prevent (measured: paper - the style's own background colour - appearing in the bottom-400-row
// count of five of nine regression viewpoints at pitch 60, worse than the pre-fix baseline).
//
// The correct operation is a set difference at tile granularity: split `tile` into quadrants
// only where `out` actually has something inside it, recursing until each remaining piece is
// either wholly new ground (kept whole, no need to split further) or wholly already covered by
// a single existing coarser-or-equal tile (dropped, contributing nothing). This is a pure
// function of `tile` and the fixed (pre-dilation) `out` - it never looks at other dilation
// candidates - so which frontier tile's neighbour loop visits `tile` first cannot change the
// result. Recursion is bounded by `maxZ` (the deepest zoom already present in `out`, so it
// never runs deeper than the data it is comparing against) purely as a safety net: in practice
// it terminates within one or two levels, because it only ever descends where `out` already has
// a tile to compare against.
void collectUncoveredParts(const UnwrappedTileID& tile,
                            const std::set<UnwrappedTileID>& out,
                            uint8_t maxZ,
                            std::set<UnwrappedTileID>& result) {
    if (hasAncestorOrSelfIn(tile, out)) {
        return; // wholly redundant - a single existing tile already spans all of this ground
    }
    if (!hasDescendantIn(tile, out)) {
        result.insert(tile); // wholly new ground - keep it whole, no finer split needed
        return;
    }
    if (tile.canonical.z >= maxZ) {
        return; // safety net only (see comment above): never observed to trigger in practice
    }
    for (const auto& child : tile.children()) {
        collectUncoveredParts(child, out, maxZ, result);
    }
}

// DuckMaps fork only: the overlapping-pair diagnostic this task measures before and after the
// dilation fix (see tilesOverlap above). O(n^2) over a mesh cover's tile count - tens of tiles
// - so this is only ever computed on the debug trace path (see meshCoverTraceEnabled), never
// in a normal frame.
int countOverlappingPairs(const std::set<UnwrappedTileID>& tiles) {
    int count = 0;
    const std::vector<UnwrappedTileID> v(tiles.begin(), tiles.end());
    for (std::size_t i = 0; i < v.size(); ++i) {
        for (std::size_t j = i + 1; j < v.size(); ++j) {
            if (tilesOverlap(v[i], v[j])) {
                ++count;
            }
        }
    }
    return count;
}

// DuckMaps fork only: same shape as TerrainContourLayerTweaker's contourTraceEnabled()/
// contourTraceSlot() - getenv checked once (static), the slot holds at most one frame's worth
// of JSON and is cleared on drain (and on any early return out of computeMeshCover), so a
// frame where terrain has no DEM source correctly reports "null" rather than repeating a
// stale frame's counts.
bool meshCoverTraceEnabled() {
    static const bool enabled = [] {
        const char* path = std::getenv("DUCKMAPS_ELEVATION_TRACE");
        return path && *path;
    }();
    return enabled;
}

std::optional<std::string>& meshCoverTraceSlot() {
    static std::optional<std::string> slot;
    return slot;
}

} // namespace

RenderTerrain::RenderTerrain(Immutable<style::Terrain::Impl> impl_)
    : impl(std::move(impl_)) {}

RenderTerrain::~RenderTerrain() = default;

std::set<UnwrappedTileID> RenderTerrain::computeMeshCover(
    const TransformState& state, const std::shared_ptr<UpdateParameters>& updateParameters) const {
    std::set<UnwrappedTileID> out;
    // DuckMaps fork only: clear any previous frame's trace before the early returns below, so
    // a frame with no DEM source (or a zoom too shallow for any cover at all) correctly drains
    // as "null" rather than repeating a stale frame's counts - same reasoning as
    // TerrainContourLayerTweaker's own trace slot.
    const bool traceMeshCover = meshCoverTraceEnabled();
    if (traceMeshCover) {
        meshCoverTraceSlot().reset();
    }
    if (!demSource) {
        return out;
    }

    // Elevation-aware visibility: terrain leaning towards the camera occupies
    // screen space its flat footprint does not, so the cover is tested against the
    // DEM height rather than the z=0 plane (as util::tileCover / gl-js do).
    DEMElevationProvider elevationProvider(demSource, getExaggeration());

    // Cover at the tile size the DEM source selects tiles with, so the per-tile DEM lookup below
    // has a tile size to compute overscaledZoom from. This no longer bounds how deep the cover
    // goes (see zoomRange just below): the mesh and its drape target resolve to the VIEW, same as
    // maplibre-gl-js's terrain tile manager (`this.minzoom=0, this.maxzoom=22` in its coveringTiles
    // call, independent of the DEM source's own maxzoom); only the DEM sample resolves to whatever
    // level the DEM actually has, via the closest loaded ancestor per tile (demSubTileOffset,
    // below). Meshing shallower than the DEM leaves its tiles descendants, which the per-tile
    // lookup cannot match, so the mesh renders flat off the placeholder; it also undersamples the
    // DEM, aliasing the relief into waves on the fixed 128x128 mesh - so the cover must never go
    // shallower than the DEM, but going deeper than it is exactly the near-field-under-tilt case
    // and is what this range now allows.
    const uint16_t terrainCoverTileSize = demSource->getTileSize();
    // 22, not demSource->getMaxZoom(): maplibre-gl-js's terrain tile manager hardcodes this same
    // 22 as its cover's maxzoom (util::DEFAULT_MAX_ZOOM matches it), not the DEM source's own
    // maxzoom, which is exactly why its near field under tilt does not go soft the way ours did.
    // The DEM source's maxzoom still bounds the DEM/drape sampling (getTerrainData's ancestor
    // fallback), just not the mesh cover's own zoom range.
    const Range<uint8_t> zoomRange{0, util::DEFAULT_MAX_ZOOM};

    // LOD parameters from the frame drive the same near-high/far-low zoom
    // selection every other source uses, so the near field drapes at a higher
    // zoom (smaller ground area per 1024 target = sharper draped content).
    util::TileCoverParameters coverParams{.transformState = state, .elevationProvider = &elevationProvider};
    double zoomShift = 0.0;
    if (updateParameters) {
        coverParams.tileLodMinRadius = updateParameters->tileLodMinRadius;
        coverParams.tileLodScale = updateParameters->tileLodScale;
        coverParams.tileLodPitchThreshold = updateParameters->tileLodPitchThreshold;
        zoomShift = updateParameters->tileLodZoomShift;
    }
    // GL JS covers with the variable-zoom tile function whenever terrain is present, at any
    // pitch, so the terrain cover asks for it rather than inheriting the map's mode. The
    // single constant desired zoom the other modes use is what makes this cover descend from
    // z0 and request tiles GL JS never asks for.
    coverParams.tileLodMode = TileLodMode::Adaptive;

    const double zoom = util::clamp<double>(state.getZoom() + zoomShift, state.getMinZoom(), state.getMaxZoom());
    const int32_t overscaledZoom = util::coveringZoomLevel(zoom, style::SourceType::RasterDEM, terrainCoverTileSize);
    if (overscaledZoom < static_cast<int32_t>(zoomRange.min)) {
        return out;
    }
    const int32_t idealZoom = std::min<int32_t>(zoomRange.max, overscaledZoom);
    for (const auto& id : util::tileCover(
             coverParams, static_cast<uint8_t>(idealZoom), zoomRange, static_cast<uint8_t>(overscaledZoom))) {
        out.insert(id.toUnwrapped());
    }

    // DuckMaps fork only: trace point 1/3 - util::tileCover's own raw output, before this
    // function touches it at all. Whether THIS set already contains overlapping pairs is a
    // separate question from the dilation fix below (tileCover is a disjoint quadtree DFS, so
    // it should not, but that is worth confirming rather than assuming) - see this task's own
    // report for the measured count.
    const std::size_t preDilationCount = out.size();
    const int preDilationOverlapPairs = traceMeshCover ? countOverlappingPairs(out) : 0;

    // One-ring cover dilation. tileCover is a top-down DFS that only descends into tiles
    // whose (sea-level) ancestor intersects the frustum, so a frontier tile whose terrain
    // rises into view but whose flat ancestor was culled is never even visited - it stays
    // out of the cover, and the terrain draws a skirt with nothing behind it, until a
    // camera nudge shifts the frustum and pulls it in. Add every 8-neighbour of the cover,
    // then keep only those whose elevation-extended bounds still intersect the frustum
    // (frustumCull, using the DEM provider's conservative fallback range for the
    // not-yet-loaded neighbours). The elevation sign decides which neighbours survive, so
    // this is correct for terrain above OR below sea level (bathymetry) without hardcoding
    // a direction; the frustum trim stops it from tripling the cover like a blind ring
    // would.
    //
    // The frustum trim alone is not enough: it answers "is this neighbour's footprint on
    // screen", never "does the cover already have this ground at another zoom" - a culled
    // coarse tile's neighbour, AT THAT SAME COARSE ZOOM, can be squarely on screen and still
    // land on top of finer tiles the cover already holds (a z12 frontier tile's z12 neighbour
    // over z13/z14/z15 tiles already in `out`), which is exactly the fault this dilation had:
    // the same ground meshed, draped and contour-painted twice, in an order that follows tile
    // load and so varies between otherwise identical runs.
    //
    // Rejecting a whole raw neighbour tile the moment ANY part of it overlaps `out`, and
    // splitting a raw neighbour into its uncovered quadrants BEFORE the frustum trim, were both
    // tried and both measured wrong in the same direction: paper (the style's own background
    // colour) increased in the bottom 400 rows of several regression viewpoints at pitch 60
    // versus the pre-fix baseline, on ground the OLD blind dilation used to mesh. The frustum
    // trim (frustumCull, below) tests each tile's OWN elevation-extended footprint against the
    // view frustum, and a coarse whole tile is far more likely to graze the frustum boundary
    // than the small quadrant of it that is the ONLY part actually free of overlap - splitting
    // first, or rejecting first, means the trim only ever gets to test the small piece, which a
    // dilation ring sitting right at the frustum's edge can legitimately fail even though the
    // coarse whole tile - the thing the OLD code actually tested - passed. So visibility is
    // decided FIRST, on the exact raw whole-tile footprints the old blind dilation itself used
    // (`dilated` below is built the same way it always was: every 8-neighbour, unfiltered),
    // and only tiles that survive that same frustum trim are then split to remove whatever
    // duplicates `out`. A tile already known to be visible does not need re-testing at a
    // smaller size to stay visible.
    //
    // De-duplication happens in two passes over the post-cull result. First, each surviving
    // raw neighbour is reduced to collectUncoveredParts(neighbour, out, ...) - its set
    // difference against the ORIGINAL (pre-dilation, fixed) `out`, never against a set the loop
    // is simultaneously growing, so this does not depend on which neighbour is visited first.
    // Second, candidates from DIFFERENT raw neighbours can still overlap EACH OTHER (two
    // different frontier tiles can each propose ground that nests one inside the other, with
    // neither overlapping `out`); resolved by keeping only the candidates that are not
    // themselves a descendant of some OTHER candidate, i.e. keeping the COARSEST tile offered
    // for any patch of new ground and dropping its finer duplicates - coverage-preserving
    // because an ancestor's footprint is a strict superset of every descendant's, and a pure
    // function of the fixed candidate set (never "what was added already"), so this pass is
    // order-independent for the same reason the first one is.
    uint8_t maxOutZ = zoomRange.min;
    for (const auto& id : out) {
        maxOutZ = std::max(maxOutZ, id.canonical.z);
    }
    std::set<UnwrappedTileID> rawNeighbours;
    for (const auto& id : out) {
        const int32_t numTiles = 1 << id.canonical.z;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int ny = static_cast<int>(id.canonical.y) + dy;
                if (ny < 0 || ny >= numTiles) continue; // nothing past the poles
                int nx = static_cast<int>(id.canonical.x) + dx;
                int nwrap = id.wrap;
                if (nx < 0) {
                    nx += numTiles;
                    --nwrap;
                } else if (nx >= numTiles) {
                    nx -= numTiles;
                    ++nwrap;
                }
                rawNeighbours.emplace(static_cast<int16_t>(nwrap),
                                      CanonicalTileID(id.canonical.z, static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)));
            }
        }
    }
    std::set<UnwrappedTileID> dilated = out;
    dilated.insert(rawNeighbours.begin(), rawNeighbours.end());
    if (dilated.size() != out.size()) {
        dilated = util::frustumCull(coverParams, dilated);
    }
    // Everything visible that is not one of the original `out` tiles: the raw neighbours that
    // survived the SAME frustum trim the old blind dilation relied on.
    std::set<UnwrappedTileID> visibleNeighbours;
    for (const auto& id : dilated) {
        if (out.find(id) == out.end()) {
            visibleNeighbours.insert(id);
        }
    }
    std::set<UnwrappedTileID> candidates;
    for (const auto& neighbour : visibleNeighbours) {
        collectUncoveredParts(neighbour, out, maxOutZ, candidates);
    }
    std::set<UnwrappedTileID> survivors;
    for (const auto& candidate : candidates) {
        bool isDescendantOfAnotherCandidate = false;
        for (const auto& other : candidates) {
            if (other != candidate && candidate.isChildOf(other)) {
                isDescendantOfAnotherCandidate = true;
                break;
            }
        }
        if (!isDescendantOfAnotherCandidate) {
            survivors.insert(candidate);
        }
    }
    out.insert(survivors.begin(), survivors.end());

    // DuckMaps fork only: trace point 2/3 - after the dilation and its frustum trim, before
    // the tile-count cap below.
    const std::size_t postDilationCount = out.size();
    const int postDilationOverlapPairs = traceMeshCover ? countOverlappingPairs(out) : 0;

    // Cap the mesh tile count: keep those nearest the map centre, drop the farthest (the
    // horizon tiles a high tilt pulls in). Everything downstream scales with this count -
    // mesh draws, DRAPE TARGETS and their re-renders, depth instances - which is why the cap
    // belongs here, in the one function that answers "which tiles is this frame's terrain",
    // rather than after the fact. It used to be applied in `update()`, by which point
    // `Renderer::Impl::render` had already called this function itself and allocated one
    // drape target per tile of the uncapped set; the cap then bounded the mesh and nothing
    // else. Measured at Gavarnie pitch 80 before the move: 113 drape targets against a
    // Quality cap of 64, at roughly 9 MB of texture each.
    //
    // Per-mode cap (TerrainLoadBudget::maxMeshTiles): Quality keeps a generous cap so terrain
    // render distance stays long; Balanced and Performance trade distance for frame time.
    const size_t maxMeshTiles = updateParameters ? terrainLoadBudget(updateParameters->terrainLoadMode).maxMeshTiles
                                                 : 0;
    if (maxMeshTiles > 0 && out.size() > maxMeshTiles) {
        // Map centre in normalised web-mercator [0,1] (standard projection)
        const LatLng centre = state.getLatLng();
        const double cx = centre.longitude() / 360.0 + 0.5;
        const double latRad = util::deg2rad(centre.latitude());
        const double cy = 0.5 - std::log(std::tan(M_PI / 4.0 + latRad / 2.0)) / (2.0 * M_PI);

        const auto tileDist2 = [&](const UnwrappedTileID& id) {
            const double scale = static_cast<double>(1u << id.canonical.z);
            const double tx = (static_cast<double>(id.canonical.x) + 0.5) / scale + id.wrap;
            const double ty = (static_cast<double>(id.canonical.y) + 0.5) / scale;
            const double dx = tx - cx;
            const double dy = ty - cy;
            return dx * dx + dy * dy;
        };

        std::vector<UnwrappedTileID> sorted(out.begin(), out.end());
        std::partial_sort(
            sorted.begin(),
            sorted.begin() + static_cast<std::ptrdiff_t>(maxMeshTiles),
            sorted.end(),
            [&](const UnwrappedTileID& a, const UnwrappedTileID& b) { return tileDist2(a) < tileDist2(b); });
        out = std::set<UnwrappedTileID>(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(maxMeshTiles));
    }

    // DuckMaps fork only: trace point 3/3 - the final cover this frame will mesh, drape and
    // draw contours over. Written once here rather than incrementally, so a frame that returns
    // early above (no DEM source, zoom below range) leaves the slot cleared instead of holding
    // a partial record.
    if (traceMeshCover) {
        std::ostringstream os;
        os << "{\"preDilationCount\":" << preDilationCount
           << ",\"preDilationOverlapPairs\":" << preDilationOverlapPairs
           << ",\"postDilationCount\":" << postDilationCount
           << ",\"postDilationOverlapPairs\":" << postDilationOverlapPairs << ",\"finalCount\":" << out.size()
           << ",\"finalOverlapPairs\":" << countOverlappingPairs(out) << "}";
        meshCoverTraceSlot() = os.str();
    }

    return out;
}

void RenderTerrain::prepareSource(RenderOrchestrator& orchestrator) {
    // Find the DEM source if we haven't already (see header: must happen before
    // the frame's drape-target pool is built, or the first frame renders no terrain)
    if (!demSource && !impl->sourceID.empty()) {
        demSource = orchestrator.getRenderSource(impl->sourceID);
        if (!demSource) {
            Log::Warning(Event::Render, "Terrain could not find DEM source: " + impl->sourceID);
        }
    }
}

void RenderTerrain::update(RenderOrchestrator& orchestrator,
                           gfx::ShaderRegistry& shaders,
                           gfx::Context& context,
                           const TexturePool& texturePool,
                           const TransformState& state,
                           const std::shared_ptr<UpdateParameters>& updateParameters,
                           const RenderTree& /*renderTree*/,
                           UniqueChangeRequestVec& changes) {
    prepareSource(orchestrator);

    // Create layer group if we don't have one (including after rebuild)
    if (!layerGroup) {
        if (auto layerGroup_ = context.createLayerGroup(TERRAIN_LAYER_INDEX, /*initialCapacity=*/1, "terrain", false)) {
            layerGroup = std::move(layerGroup_);
            activateLayerGroup(true, changes);
        } else {
            Log::Error(Event::Render, "Failed to create terrain layer group");
            return;
        }
    }

    // Depth-pass twin of the terrain layer group; not activated in the
    // orchestrator, rendered only by renderDepth into the depth target
    if (!depthLayerGroup) {
        depthLayerGroup = context.createLayerGroup(TERRAIN_LAYER_INDEX, /*initialCapacity=*/1, "terrain-depth", false);
    }

    // Create tweaker if we don't have one
    if (!tweaker) {
        tweaker = std::make_unique<TerrainLayerTweaker>(this, &orchestrator);
    }

    // The skirts are baked into the shared mesh, so a change of setting has to drop the mesh
    // and every tile drawable holding a buffer built from it.
    if (updateParameters && updateParameters->terrainSkirtLength != meshSkirtLength) {
        meshSkirtLength = updateParameters->terrainSkirtLength;
        mesh.reset();
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        if (depthLayerGroup) {
            depthLayerGroup->clearDrawables();
            depthDirty = true;
        }
        tilesWithDrawables.clear();
    }

    // If we don't have a DEM source, we can't create terrain drawables
    if (!demSource) {
        return;
    }

    // Get tiles from the DEM source
    auto renderTiles = demSource->getRawRenderTiles();
    if (renderTiles->empty()) {
        return;
    }

    // Cast to LayerGroup for addDrawable
    auto* lg = static_cast<LayerGroup*>(layerGroup.get());
    if (!lg) {
        return;
    }

    // Decode and cache the DEM textures of loaded DEM tiles
    ++demUpdateCounter;
    for (const auto& renderTile : *renderTiles) {
        const auto& tile = renderTile.getTile();
        if (tile.kind != Tile::Kind::RasterDEM) {
            continue;
        }
        auto* demTile = const_cast<RasterDEMTile*>(static_cast<const RasterDEMTile*>(&tile));
        auto* hillshadeBucket = demTile->getBucket();
        const auto* demData = hillshadeBucket ? &hillshadeBucket->getDEMData() : nullptr;
        if (demData && demData->getImagePtr() && !demData->getImagePtr()->size.isEmpty()) {
            // All tiles come from the same raster-dem source, so they share one
            // encoding and DEM dimension
            demUnpackVector = demData->getUnpackVector();
            demDim = demData->dim;
            if (auto existing = demTextures.find(renderTile.id); existing != demTextures.end()) {
                existing->second.lastUsed = demUpdateCounter;
            } else if (auto texture = createDEMTexture(context, *demData)) {
                // Keep the texture available for elevation sampling by non-draped layers
                demTextures[renderTile.id] = {texture, demData->dim, demUpdateCounter};
#if MLN_RENDER_BACKEND_OPENGL
                // Also pack this tile's DEM into the array for the (upcoming) instanced
                // depth pass. Additive: the per-tile texture above is still the fallback.
                packDEMArrayLayer(context, renderTile.id, *demData);
#endif
            }
        }
    }

    // The mesh (and drape) tile set: an elevation-aware, LOD-based ideal cover
    // taken straight from the view, not from the DEM source's loaded tiles - so a
    // sparse DEM's low-zoom fallback tiles don't drag the drape resolution down
    // (blurry roads) or leave uncovered areas as skirt. DEM/drape textures fall
    // back to ancestors per tile below while exact tiles load. tileCover already
    // limits to the frustum (elevation included), so no extra cull is needed.
    // Use the cover Renderer::Impl already computed for this frame when available:
    // recomputing here can yield a different set once demDim is known (see
    // setFrameMeshCover), which would desync this mesh from the drape-target pool.
    std::set<UnwrappedTileID> meshTiles = frameMeshCover ? std::move(*frameMeshCover)
                                                         : computeMeshCover(state, updateParameters);
    frameMeshCover.reset();

    // The mesh tile cap (TerrainLoadBudget::maxMeshTiles) is applied inside
    // computeMeshCover now, not here. It used to be applied here, AFTER
    // Renderer::Impl::render had already called computeMeshCover itself and allocated one
    // drape target per tile of the UNCAPPED set - so the cap bounded the mesh and nothing
    // else, while its own doc comment promised it bounded "mesh draws, drape targets and
    // re-renders, depth instances". Measured at Gavarnie pitch 80: 113 drape targets where
    // the Quality cap is 64, at about 9 MB of texture each.

    // Drop drawables and cached DEM textures for tiles that left the mesh tile
    // set, keeping everything else intact between frames
    std::unordered_set<OverscaledTileID> currentTiles;
    for (const auto& id : meshTiles) {
        currentTiles.emplace(id.canonical.z, id.wrap, id.canonical);
    }
    lg->removeDrawablesIf(
        [&](gfx::Drawable& drawable) { return drawable.getTileID() && !currentTiles.contains(*drawable.getTileID()); });
    auto* depthLg = static_cast<LayerGroup*>(depthLayerGroup.get());
    if (depthLg) {
        if (depthLg->removeDrawablesIf([&](gfx::Drawable& drawable) {
                return drawable.getTileID() && !currentTiles.contains(*drawable.getTileID());
            }) > 0) {
            depthDirty = true;
        }
    }
    for (auto it = tilesWithDrawables.begin(); it != tilesWithDrawables.end();) {
        if (!currentTiles.contains(it->first)) {
            drawableDemCoords.erase(it->first);
            it = tilesWithDrawables.erase(it);
        } else {
            ++it;
        }
    }
    // Retain cached DEM textures that are related to the current tile set so they
    // can serve as ancestor fallbacks while exact tiles load (as maplibre-gl-js
    // retains terrain tiles in its source cache); drop unrelated ones
    for (auto it = demTextures.begin(); it != demTextures.end();) {
        bool related = false;
        for (const auto& current : currentTiles) {
            const UnwrappedTileID unwrapped = current.toUnwrapped();
            if (unwrapped == it->first || unwrapped.isChildOf(it->first) || it->first.isChildOf(unwrapped)) {
                related = true;
                break;
            }
        }
        if (related) {
            it = std::next(it);
        } else {
#if MLN_RENDER_BACKEND_OPENGL
            freeDEMArrayLayer(it->first);
#endif
            it = demTextures.erase(it);
        }
    }
    // Cap the cache: ancestor/descendant relations accumulate while browsing
    // (zooming makes whole chains "related"), which previously grew past 2GB
    // of DEM textures and overflowed/OOMed. Evict least-recently-used entries
    // that were not used this frame until the cache is back under budget.
    if (demTextures.size() > maxDEMTextures) {
        std::vector<std::pair<uint64_t, UnwrappedTileID>> evictable;
        for (const auto& [id, entry] : demTextures) {
            if (entry.lastUsed != demUpdateCounter) {
                evictable.emplace_back(entry.lastUsed, id);
            }
        }
        std::sort(evictable.begin(), evictable.end());
        for (const auto& [lastUsed, id] : evictable) {
            if (demTextures.size() <= maxDEMTextures) {
                break;
            }
#if MLN_RENDER_BACKEND_OPENGL
            freeDEMArrayLayer(id);
#endif
            demTextures.erase(id);
        }
    }

    // Create terrain drawables for each mesh tile
    for (const auto& unwrapped : meshTiles) {
        const OverscaledTileID tileID(unwrapped.canonical.z, unwrapped.wrap, unwrapped.canonical);

        // Skip if the tile already has a drawable bound to its own DEM (nothing can beat that),
        // but mark that DEM used: this early-out is such a tile's only path, so the LRU could
        // otherwise evict the texture from under the drawable still sampling it.
        if (const auto existing = tilesWithDrawables.find(tileID);
            existing != tilesWithDrawables.end() &&
            existing->second == static_cast<int8_t>(unwrapped.canonical.z)) {
            if (auto cached = demTextures.find(unwrapped); cached != demTextures.end()) {
                cached->second.lastUsed = demUpdateCounter;
            }
            continue;
        }

        // Resolve the DEM texture: the tile's own decoded DEM if available,
        // otherwise the closest cached ancestor as a fallback so the terrain
        // mesh stays up while the tile loads (as maplibre-gl-js does),
        // otherwise the flat placeholder
        std::shared_ptr<gfx::Texture2D> demTexture;
        // {scale, x offset, y offset, DEM dim}: maps tile-local coords (0..EXTENT)
        // into the bound DEM tile's normalized space, matching getTerrainData so
        // the terrain mesh and the elevated layers sample identically. The DEM
        // dimension rides in .w for the shader's get_elevation() call.
        std::array<float, 4> demCoords{{1.0f / util::EXTENT, 0.0f, 0.0f, static_cast<float>(demDim)}};
        // The canonical zoom of the DEM tile this drawable will sample, or -1 for the flat
        // placeholder. Higher is strictly better (a deeper DEM tile covering the same ground),
        // and the tile's own z is the best there is.
        int8_t demZoom = -1;
        // DEM tile whose texture / array-layer this tile uses. Only *read* by the GL
        // instanced-depth block below, so mark it maybe_unused: other backends keep the
        // per-tile depth drawables and would otherwise fail -Wunused-but-set-variable.
        [[maybe_unused]] const UnwrappedTileID* demTileUsed = nullptr;

        if (auto cached = demTextures.find(unwrapped); cached != demTextures.end()) {
            cached->second.lastUsed = demUpdateCounter;
            demTexture = cached->second.texture;
            demZoom = static_cast<int8_t>(unwrapped.canonical.z);
            demTileUsed = &unwrapped;
        } else {
            // Fall back to the closest cached ancestor DEM
            const UnwrappedTileID* ancestorID = nullptr;
            DEMTextureEntry* ancestorEntry = nullptr;
            int bestZoom = -1;
            for (auto& [candidate, entry] : demTextures) {
                if (candidate != unwrapped && unwrapped.isChildOf(candidate) &&
                    static_cast<int>(candidate.canonical.z) > bestZoom) {
                    bestZoom = candidate.canonical.z;
                    ancestorID = &candidate;
                    ancestorEntry = &entry;
                    demTexture = entry.texture;
                }
            }
            if (!demTexture) {
                // No DEM at all yet: render the mesh flat with the placeholder
                // DEM so the draped map still shows (a briefly flat area is
                // less jarring than a hole in the terrain)
                demTexture = getPlaceholderDEMTexture(context);
                if (!demTexture) {
                    continue;
                }
            } else {
                ancestorEntry->lastUsed = demUpdateCounter;
                demZoom = static_cast<int8_t>(ancestorID->canonical.z);
                demTileUsed = ancestorID;
                const auto off = demSubTileOffset(unwrapped.canonical, ancestorID->canonical);
                demCoords = {{1.0f / (util::EXTENT * off.scale),
                              off.dx / off.scale,
                              off.dy / off.scale,
                              static_cast<float>(demDim)}};
            }
        }

        // If a drawable already exists for this tile, keep it until a DEM at a HIGHER zoom
        // becomes available, then replace it. Comparing zooms rather than a three-value
        // quality tier is what stops the drawable latching onto whichever ancestor loaded
        // first (see `tilesWithDrawables`).
        if (const auto existing = tilesWithDrawables.find(tileID); existing != tilesWithDrawables.end()) {
            if (existing->second >= demZoom) {
                continue;
            }
            lg->removeDrawablesIf(
                [&](gfx::Drawable& drawable) { return drawable.getTileID() && *drawable.getTileID() == tileID; });
            if (depthLg) {
                depthLg->removeDrawablesIf(
                    [&](gfx::Drawable& drawable) { return drawable.getTileID() && *drawable.getTileID() == tileID; });
                depthDirty = true;
            }
            tilesWithDrawables.erase(existing);
        }
        drawableDemCoords[tileID] = demCoords;
#if MLN_RENDER_BACKEND_OPENGL
        {
            float layer = -1.0f;
            if (demTileUsed) {
                if (const auto la = demArrayLayer.find(*demTileUsed); la != demArrayLayer.end()) {
                    layer = static_cast<float>(la->second);
                }
            }
            drawableDemLayer[tileID] = layer;
        }
#endif

        // Create terrain drawable for this tile
        const auto renderTarget = texturePool.getRenderTarget(unwrapped);
        if (!renderTarget) {
            continue;
        }
        auto drawable = createDrawableForTile(context, shaders, tileID, demTexture, renderTarget->getTexture());
        if (drawable) {
            lg->addDrawable(std::move(drawable));
            tilesWithDrawables[tileID] = demZoom;
#if !MLN_RENDER_BACKEND_OPENGL
            // Non-GL backends: one depth drawable per tile (no instancing path there).
            if (depthLg) {
                if (auto depthDrawable = createDrawableForTile(
                        context, shaders, tileID, demTexture, nullptr, /*depthPass=*/true)) {
                    depthLg->addDrawable(std::move(depthDrawable));
                    depthDirty = true;
                }
            }
#endif
        }
    }

    // Debug-only, off by default: gate the whole above-ground check (per-frame free-camera +
    // elevation sampling) on the flag so it costs nothing unless explicitly enabled.
    if (updateParameters && updateParameters->debugAboveGroundLog) {
        logAboveGroundMargin(state);
    }

#if MLN_RENDER_BACKEND_OPENGL
    // GL: collect the per-instance (tile, dem_coords, dem_layer) list for the whole mesh tile
    // set and rebuild the single instanced depth drawable only when that set changes (its
    // transforms refresh every frame in updateInstancedDepthUBO). Tiles without a packed DEM
    // array layer (-1) are skipped - they briefly miss depth occlusion, the same tolerance the
    // old ancestor/placeholder fallback had.
    {
        std::vector<DepthInstance> instances;
        instances.reserve(meshTiles.size());
        std::size_t sig = 0;
        for (const auto& unwrapped : meshTiles) {
            const OverscaledTileID tileID(unwrapped.canonical.z, unwrapped.wrap, unwrapped.canonical);
            const auto lc = drawableDemLayer.find(tileID);
            if (lc == drawableDemLayer.end() || lc->second < 0.0f || instances.size() >= maxDepthInstances) {
                continue;
            }
            const auto cc = drawableDemCoords.find(tileID);
            const std::array<float, 4> coords =
                cc != drawableDemCoords.end()
                    ? cc->second
                    : std::array<float, 4>{{1.0f / util::EXTENT, 0.0f, 0.0f, static_cast<float>(demDim)}};
            instances.push_back({tileID, coords, lc->second});
            util::hash_combine(sig, std::hash<OverscaledTileID>{}(tileID));
            util::hash_combine(sig, static_cast<int>(lc->second));
        }
        depthInstances = std::move(instances);
        if (sig != depthInstanceSignature) {
            depthInstanceSignature = sig;
            rebuildInstancedDepthDrawable(context, shaders);
            depthDirty = true;
        }
    }
#endif
}

void RenderTerrain::logAboveGroundMargin(const TransformState& state) {
    // Log the camera eye's clearance over the *rendered* terrain surface each frame (throttled),
    // so the flight/interaction tests can report when the sea-level-anchored camera dips below
    // terrain - the FPV underground/white artifact (TERRAIN.md Phase 4). Groundwork for a future
    // terrain-anchored camera: a concrete, measurable "how far under, and where" signal.
    if (!demSource) {
        return;
    }
    const auto fco = state.getFreeCameraOptions();
    const auto loc = fco.getLocation(); // eye Lat/Lng + altitude in metres (engine's own conversion)
    if (!fco.position || !loc) {
        return;
    }
    const double now = util::MonotonicTimer::now().count();
    if (now - lastAboveGroundLog < kAboveGroundLogInterval) {
        return;
    }
    lastAboveGroundLog = now;

    // Sample the rendered (exaggerated) terrain height directly under the eye. The eye's
    // horizontal position is the free-camera mercator x/y (0..1); getElevation walks to the best
    // loaded DEM ancestor, so a deep sample zoom just picks the finest tile available there.
    const auto& p = *fco.position;
    constexpr int sampleZoom = 14;
    const double n = std::pow(2.0, sampleZoom);
    const double fx = p[0] * n;
    const double fy = p[1] * n;
    const auto tx = static_cast<int64_t>(std::floor(fx));
    const auto ty = static_cast<int64_t>(std::floor(fy));
    const UnwrappedTileID sampleTile(static_cast<uint8_t>(sampleZoom), tx, ty);
    const auto localX = static_cast<float>((fx - static_cast<double>(tx)) * util::EXTENT);
    const auto localY = static_cast<float>((fy - static_cast<double>(ty)) * util::EXTENT);

    const double groundM = getElevationWithExaggeration(sampleTile, localX, localY);
    const double marginM = loc->altitude - groundM;
    // Only log when the camera is near or below the terrain - the interesting case, and low
    // noise (normal viewing sits km above). A DEM miss reads as groundM==0 -> large positive
    // margin, so it also stays below this gate and is not mistaken for real clearance.
    if (marginM >= kAboveGroundAlertM) {
        return;
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << "ABOVE-GROUND marginM=" << marginM << " camAltM=" << loc->altitude
       << " groundM=" << groundM << " under=" << (marginM < 0.0 ? 1 : 0) << std::setprecision(4)
       << " zoom=" << state.getZoom() << " pitch=" << state.getPitch() << " lng=" << loc->location.longitude()
       << " lat=" << loc->location.latitude();
    Log::Info(Event::Render, os.str());
}

bool RenderTerrain::normalizeTileCoordinates(UnwrappedTileID& tileID, float& x, float& y) {
    constexpr float extent = static_cast<float>(util::EXTENT);
    if (x >= 0.0f && x < extent && y >= 0.0f && y < extent) {
        return true;
    }

    const auto offsetX = static_cast<int64_t>(std::floor(x / extent));
    const auto offsetY = static_cast<int64_t>(std::floor(y / extent));

    const int64_t dim = int64_t{1} << tileID.canonical.z;
    const int64_t tileY = static_cast<int64_t>(tileID.canonical.y) + offsetY;
    if (tileY < 0 || tileY >= dim) {
        // Past a pole. The tile grid does not continue, so there is nothing to sample.
        return false;
    }

    // Rounding can leave the shifted coordinate exactly on the far edge, which belongs to
    // the next tile over; keep it inside the tile we just resolved.
    x = util::clamp(x - static_cast<float>(offsetX) * extent, 0.0f, std::nextafter(extent, 0.0f));
    y = util::clamp(y - static_cast<float>(offsetY) * extent, 0.0f, std::nextafter(extent, 0.0f));

    // x wraps around the world rather than ending: hand the constructor a global tile x and
    // let it derive the wrap.
    const int64_t globalX = static_cast<int64_t>(tileID.wrap) * dim + static_cast<int64_t>(tileID.canonical.x) +
                            offsetX;
    tileID = UnwrappedTileID(tileID.canonical.z, globalX, tileY);
    return true;
}

RenderTerrain::ElevationSample RenderTerrain::findElevationSample(const UnwrappedTileID& tileID_,
                                                                    float x,
                                                                    float y) const {
    ElevationSample sample;
    if (!demSource) {
        return sample;
    }

    UnwrappedTileID tileID = tileID_;
    if (!normalizeTileCoordinates(tileID, x, y)) {
        return sample; // past a pole; nothing to sample
    }

    // Find the DEM tile matching the requested tile, or its closest available ancestor
    const auto renderTiles = demSource->getRawRenderTiles();
    const RenderTile* demRenderTile = nullptr;
    int bestZoom = -1;
    for (const auto& renderTile : *renderTiles) {
        const UnwrappedTileID& candidate = renderTile.id;
        if ((candidate == tileID || tileID.isChildOf(candidate)) &&
            static_cast<int>(candidate.canonical.z) > bestZoom) {
            bestZoom = candidate.canonical.z;
            demRenderTile = &renderTile;
        }
    }
    if (!demRenderTile) {
        return sample; // no loaded DEM tile covers this point at all
    }

    const auto& tile = demRenderTile->getTile();
    if (tile.kind != Tile::Kind::RasterDEM) {
        return sample;
    }
    auto* demTile = const_cast<RasterDEMTile*>(static_cast<const RasterDEMTile*>(&tile));
    auto* bucket = demTile->getBucket();
    if (!bucket) {
        return sample; // matched a tile, but its DEM has not decoded yet
    }
    const auto& demData = bucket->getDEMData();
    if (!demData.getImagePtr() || demData.dim <= 0) {
        return sample;
    }

    // Map the tile-local coordinate into the (possibly ancestor) DEM tile
    const UnwrappedTileID& demTileID = demRenderTile->id;
    const auto off = demSubTileOffset(tileID.canonical, demTileID.canonical);
    const float xInDem = (off.dx * util::EXTENT + x) / off.scale;
    const float yInDem = (off.dy * util::EXTENT + y) / off.scale;

    // Bilinear interpolation of the DEM texels, as in maplibre-gl-js Terrain.getDEMElevation
    const float dim = static_cast<float>(demData.dim);
    const float px = util::clamp(xInDem / util::EXTENT * dim, 0.0f, dim - 1.0f);
    const float py = util::clamp(yInDem / util::EXTENT * dim, 0.0f, dim - 1.0f);
    const auto x0 = static_cast<int32_t>(std::floor(px));
    const auto y0 = static_cast<int32_t>(std::floor(py));
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const float tl = static_cast<float>(demData.get(x0, y0));
    const float tr = static_cast<float>(demData.get(x0 + 1, y0));
    const float bl = static_cast<float>(demData.get(x0, y0 + 1));
    const float br = static_cast<float>(demData.get(x0 + 1, y0 + 1));
    const float top = tl + (tr - tl) * fx;
    const float bottom = bl + (br - bl) * fx;

    sample.hit = true;
    sample.meters = top + (bottom - top) * fy;
    sample.demZ = demTileID.canonical.z;
    sample.demX = demTileID.canonical.x;
    sample.demY = demTileID.canonical.y;
    sample.exact = (demTileID.canonical.z == tileID.canonical.z);
    return sample;
}

float RenderTerrain::getElevation(const UnwrappedTileID& tileID, float x, float y) const {
    // Every fallback in the pre-refactor body returned a bare 0.0f, which is ElevationSample's
    // default when !hit, so this is behaviour-identical to the body it replaces.
    return findElevationSample(tileID, x, y).meters;
}

float RenderTerrain::getElevationWithExaggeration(const UnwrappedTileID& tileID, float x, float y) const {
    return getElevation(tileID, x, y) * getExaggeration();
}

std::optional<float> RenderTerrain::queryElevation(const UnwrappedTileID& tileID, float x, float y) const {
    const auto sample = findElevationSample(tileID, x, y);
    if (!sample.hit) {
        return std::nullopt;
    }
    return sample.meters;
}

double RenderTerrain::getElevationForLatLng(const LatLng& latLng) const {
    if (!demSource) {
        return 0.0;
    }

    // Sample as deep as the finest DEM tile loaded: getElevation walks up to the closest
    // covering ancestor, but never down, so sampling shallower than the DEM reads nothing.
    int sampleZoom = -1;
    const auto renderTiles = demSource->getRawRenderTiles();
    for (const auto& renderTile : *renderTiles) {
        if (renderTile.getTile().kind == Tile::Kind::RasterDEM) {
            sampleZoom = std::max(sampleZoom, static_cast<int>(renderTile.id.canonical.z));
        }
    }
    if (sampleZoom < 0) {
        return 0.0;
    }

    const double n = std::pow(2.0, sampleZoom);
    // The int-zoom overload of project() returns tile units directly (0..2^zoom); the
    // same-named double-scale overload returns pixels. Do not divide by the tile size.
    const auto point = Projection::project(latLng, sampleZoom);
    const double fx = point.x;
    const double fy = point.y;
    const auto tx = static_cast<int64_t>(std::floor(fx));
    const auto ty = static_cast<int64_t>(std::floor(fy));
    if (ty < 0 || static_cast<double>(ty) >= n) {
        return 0.0; // past a pole
    }

    const UnwrappedTileID sampleTile(static_cast<uint8_t>(sampleZoom), tx, ty);
    const auto localX = static_cast<float>((fx - static_cast<double>(tx)) * util::EXTENT);
    const auto localY = static_cast<float>((fy - static_cast<double>(ty)) * util::EXTENT);
    return getElevationWithExaggeration(sampleTile, localX, localY);
}

std::optional<double> RenderTerrain::queryElevationForLatLng(const LatLng& latLng) const {
    if (!demSource) {
        return std::nullopt;
    }

    // Same "sample as deep as the finest DEM tile loaded" walk as getElevationForLatLng.
    int sampleZoom = -1;
    const auto renderTiles = demSource->getRawRenderTiles();
    for (const auto& renderTile : *renderTiles) {
        if (renderTile.getTile().kind == Tile::Kind::RasterDEM) {
            sampleZoom = std::max(sampleZoom, static_cast<int>(renderTile.id.canonical.z));
        }
    }
    if (sampleZoom < 0) {
        return std::nullopt; // no DEM tile loaded anywhere in view
    }

    const double n = std::pow(2.0, sampleZoom);
    const auto point = Projection::project(latLng, sampleZoom);
    const double fx = point.x;
    const double fy = point.y;
    const auto tx = static_cast<int64_t>(std::floor(fx));
    const auto ty = static_cast<int64_t>(std::floor(fy));
    if (ty < 0 || static_cast<double>(ty) >= n) {
        return std::nullopt; // past a pole
    }

    // Task C8 part 3, 16 September 2026. This used to sample at `sampleZoom` and nowhere else,
    // and `sampleZoom` is the DEEPEST DEM zoom loaded anywhere in view. A point outside that
    // level's own coverage therefore missed completely and was reported as "no DEM here", even
    // with perfectly good coarser tiles loaded over it. That is why the camera's own ground
    // point - which sits behind the visible area at any pitch, so its deep tile is very often
    // not loaded - read as no-hit on most frames of a travel trace, which left the 60 m terrain
    // floor with no input exactly while the camera was moving.
    //
    // So the walk now falls back level by level to the best DEM actually available, deepest
    // first, and only gives up when nothing at any level can be read. The level that served the
    // sample is what `probeElevationForLatLng` reports as `demZ`, so the trace shows it.
    for (int z = sampleZoom; z >= 0; --z) {
        const double nz = std::pow(2.0, z);
        const auto p = Projection::project(latLng, z);
        const auto zx = static_cast<int64_t>(std::floor(p.x));
        const auto zy = static_cast<int64_t>(std::floor(p.y));
        if (zy < 0 || static_cast<double>(zy) >= nz) {
            continue;
        }
        const UnwrappedTileID tile(static_cast<uint8_t>(z), zx, zy);
        const auto lx = static_cast<float>((p.x - static_cast<double>(zx)) * util::EXTENT);
        const auto ly = static_cast<float>((p.y - static_cast<double>(zy)) * util::EXTENT);
        if (const auto elevation = queryElevation(tile, lx, ly)) {
            return static_cast<double>(*elevation) * static_cast<double>(getExaggeration());
        }
    }
    return std::nullopt; // nothing decoded at any level over this point
}

RenderTerrain::ElevationProbe RenderTerrain::probeElevationForLatLng(const LatLng& latLng) const {
    ElevationProbe probe;
    if (!demSource) {
        return probe;
    }

    // Same "sample as deep as the finest DEM tile loaded" walk as queryElevationForLatLng.
    int sampleZoom = -1;
    const auto renderTiles = demSource->getRawRenderTiles();
    for (const auto& renderTile : *renderTiles) {
        if (renderTile.getTile().kind == Tile::Kind::RasterDEM) {
            sampleZoom = std::max(sampleZoom, static_cast<int>(renderTile.id.canonical.z));
        }
    }
    if (sampleZoom < 0) {
        return probe; // no DEM tile loaded anywhere in view
    }

    const double n = std::pow(2.0, sampleZoom);
    const auto point = Projection::project(latLng, sampleZoom);
    const double fx = point.x;
    const double fy = point.y;
    const auto tx = static_cast<int64_t>(std::floor(fx));
    const auto ty = static_cast<int64_t>(std::floor(fy));
    if (ty < 0 || static_cast<double>(ty) >= n) {
        return probe; // past a pole
    }

    // Task C8 part 3: the same deepest-first fallback as queryElevationForLatLng above, so the
    // probe reports the level that actually served the sample rather than reporting a miss at the
    // deepest level loaded somewhere else in the view.
    UnwrappedTileID sampleTile(static_cast<uint8_t>(sampleZoom), tx, ty);
    auto localX = static_cast<float>((fx - static_cast<double>(tx)) * util::EXTENT);
    auto localY = static_cast<float>((fy - static_cast<double>(ty)) * util::EXTENT);
    for (int z = sampleZoom; z >= 0; --z) {
        const double nz = std::pow(2.0, z);
        const auto p = Projection::project(latLng, z);
        const auto zx = static_cast<int64_t>(std::floor(p.x));
        const auto zy = static_cast<int64_t>(std::floor(p.y));
        if (zy < 0 || static_cast<double>(zy) >= nz) {
            continue;
        }
        const UnwrappedTileID candidate(static_cast<uint8_t>(z), zx, zy);
        const auto lx = static_cast<float>((p.x - static_cast<double>(zx)) * util::EXTENT);
        const auto ly = static_cast<float>((p.y - static_cast<double>(zy)) * util::EXTENT);
        if (findElevationSample(candidate, lx, ly).hit) {
            sampleTile = candidate;
            localX = lx;
            localY = ly;
            break;
        }
    }

    const auto sample = findElevationSample(sampleTile, localX, localY);
    probe.hit = sample.hit;
    probe.exact = sample.exact;
    probe.demZ = sample.demZ;
    probe.demX = sample.demX;
    probe.demY = sample.demY;
    probe.meters = sample.hit ? sample.meters * getExaggeration() : 0.0f;
    return probe;
}

std::vector<CanonicalTileID> RenderTerrain::getResidentDemTileIds() const {
    std::vector<CanonicalTileID> ids;
    if (!demSource) {
        return ids;
    }
    const auto renderTiles = demSource->getRawRenderTiles();
    ids.reserve(renderTiles->size());
    for (const auto& renderTile : *renderTiles) {
        ids.push_back(renderTile.id.canonical);
    }
    return ids;
}

std::size_t RenderTerrain::terrainSettleSignature() const {
    // See the header for why this exists. Two accumulators, each salted, so a cover entry and
    // a binding entry can never cancel each other out in the sum.
    std::size_t total = 0;
    for (const auto& id : lastFrameMeshCover) {
        std::size_t h = 0x9e3779b9u;
        util::hash_combine(h, id.wrap);
        util::hash_combine(h, id.canonical.z);
        util::hash_combine(h, id.canonical.x);
        util::hash_combine(h, id.canonical.y);
        total += h;
    }
    for (const auto& [tileID, demZoom] : tilesWithDrawables) {
        std::size_t h = 0x85ebca6bu;
        util::hash_combine(h, tileID.overscaledZ);
        util::hash_combine(h, tileID.canonical.z);
        util::hash_combine(h, tileID.canonical.x);
        util::hash_combine(h, tileID.canonical.y);
        util::hash_combine(h, static_cast<int>(demZoom));
        if (const auto it = drawableDemCoords.find(tileID); it != drawableDemCoords.end()) {
            for (const float v : it->second) {
                // The exact bits, not the value: two runs that bind the same DEM tile with the
                // same sub-tile offset must hash the same, and a half-ulp difference in that
                // offset is a different sample and should not be smoothed away here.
                std::uint32_t bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                util::hash_combine(h, bits);
            }
        }
        total += h;
    }
    return total;
}

std::string RenderTerrain::debugDrainMeshCoverDilationTraceJSON() {
    auto& slot = meshCoverTraceSlot();
    if (!slot) {
        return "null";
    }
    std::string result = std::move(*slot);
    slot.reset();
    return result;
}

std::string RenderTerrain::debugMeshTileTiersJSON() const {
    // Debug-only, task N1. Sorted so two runs can be compared line for line.
    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(tilesWithDrawables.size());
    for (const auto& [tileID, demZoom] : tilesWithDrawables) {
        std::ostringstream key;
        key << static_cast<int>(tileID.canonical.z) << "/" << tileID.canonical.x << "/" << tileID.canonical.y;
        const auto it = drawableDemCoords.find(tileID);
        const std::array<float, 4> coords = it != drawableDemCoords.end() ? it->second
                                                                         : std::array<float, 4>{{0, 0, 0, 0}};
        std::ostringstream value;
        value << std::setprecision(9) << "{\"t\":\"" << key.str() << "\",\"demZ\":" << static_cast<int>(demZoom)
              << ",\"dem\":[" << coords[0] << "," << coords[1] << "," << coords[2] << "," << coords[3] << "]}";
        entries.emplace_back(key.str(), value.str());
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& [ignored, value] : entries) {
        (void)ignored;
        if (!first) {
            os << ",";
        }
        first = false;
        os << value;
    }
    os << "]";
    return os.str();
}

std::string RenderTerrain::debugDemTileContentJSON() const {
    // Debug-only, task R1A. See the header doc comment. Sorted so two runs can be compared
    // line for line, exactly like debugMeshTileTiersJSON above.
    struct Entry {
        std::string id;
        int neighbors = 0;
        uint64_t full = 0;
        uint64_t border = 0;
    };
    std::vector<Entry> entries;
    if (demSource) {
        const auto renderTiles = demSource->getRawRenderTiles();
        entries.reserve(renderTiles->size());
        for (const auto& renderTile : *renderTiles) {
            const Tile& tile = renderTile.getTile();
            if (tile.kind != Tile::Kind::RasterDEM) {
                continue;
            }
            const auto& demTile = static_cast<const RasterDEMTile&>(tile);
            const HillshadeBucket* bucket = demTile.getBucket();
            if (!bucket) {
                continue;
            }
            const DEMData& dem = bucket->getDEMData();
            const PremultipliedImage* image = dem.getImage();
            if (!image || !image->valid()) {
                continue;
            }

            std::ostringstream idOs;
            idOs << static_cast<int>(renderTile.id.canonical.z) << "/" << renderTile.id.canonical.x << "/"
                 << renderTile.id.canonical.y;

            const uint8_t* data = image->data.get();
            const uint64_t full = fnv1a64(data, image->bytes());

            // The border ring is every texel outside the tile's own dim x dim interior. The
            // interior is dim x dim texels starting at texel (2, 2) in the stride x stride
            // buffer (DEMData's private idx() is (y+2)*stride + (x+2) for x=y=0..dim-1); this
            // mirrors that layout without needing idx() itself, which is private. Rows above
            // and below the interior are hashed whole; interior rows are hashed only in their
            // left and right padding columns (2 texels each side).
            const int32_t dim = dem.dim;
            const int32_t stride = dem.stride;
            const size_t rowBytes = static_cast<size_t>(stride) * 4;
            const size_t borderColBytes = 2 * 4;
            uint64_t border = 0xcbf29ce484222325ULL;
            for (int32_t y = 0; y < stride; ++y) {
                const uint8_t* row = data + static_cast<size_t>(y) * rowBytes;
                if (y < 2 || y >= 2 + dim) {
                    border = fnv1a64(row, rowBytes, border);
                } else {
                    border = fnv1a64(row, borderColBytes, border);
                    border = fnv1a64(row + static_cast<size_t>(2 + dim) * 4, borderColBytes, border);
                }
            }

            entries.push_back(Entry{idOs.str(), static_cast<int>(demTile.neighboringTiles), full, border});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });

    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& e : entries) {
        if (!first) {
            os << ",";
        }
        first = false;
        os << "{\"id\":\"" << e.id << "\",\"neighbors\":" << e.neighbors << ",\"full\":" << e.full
           << ",\"border\":" << e.border << "}";
    }
    os << "]";
    return os.str();
}

std::vector<CanonicalTileID> RenderTerrain::getLastFrameMeshCoverTileIds() const {
    std::vector<CanonicalTileID> ids;
    ids.reserve(lastFrameMeshCover.size());
    for (const auto& id : lastFrameMeshCover) {
        ids.push_back(id.canonical);
    }
    return ids;
}

std::optional<RenderTerrain::TerrainData> RenderTerrain::getTerrainData(const UnwrappedTileID& tileID) const {
    // Find the DEM texture matching the requested tile, or its closest available ancestor
    const UnwrappedTileID* demTileID = nullptr;
    const DEMTextureEntry* entry = nullptr;
    int bestZoom = -1;
    for (const auto& [candidate, candidateEntry] : demTextures) {
        if ((candidate == tileID || tileID.isChildOf(candidate)) &&
            static_cast<int>(candidate.canonical.z) > bestZoom) {
            bestZoom = candidate.canonical.z;
            demTileID = &candidate;
            entry = &candidateEntry;
        }
    }
    if (!entry || !entry->texture) {
        return std::nullopt;
    }

    // Map tile-local coordinates (0..EXTENT) of the requested tile into
    // normalized coordinates (0..1) of the (possibly ancestor) DEM tile
    const auto off = demSubTileOffset(tileID.canonical, demTileID->canonical);

    return TerrainData{
        .demTexture = entry->texture,
        .demCoords = {{1.0f / (util::EXTENT * off.scale), off.dx / off.scale, off.dy / off.scale, 0.0f}},
        .demDim = static_cast<float>(entry->dim),
    };
}

std::optional<UnwrappedTileID> RenderTerrain::debugDemTileIdForTile(const UnwrappedTileID& tileID) const {
    // DuckMaps fork only, task C7: same resolution loop as getTerrainData above (kept in sync
    // with it deliberately - see this method's own doc comment in the header for why a second
    // copy exists rather than changing getTerrainData's return type), returning the WINNING
    // tile id instead of its texture.
    const UnwrappedTileID* demTileID = nullptr;
    int bestZoom = -1;
    for (const auto& [candidate, candidateEntry] : demTextures) {
        if ((candidate == tileID || tileID.isChildOf(candidate)) &&
            static_cast<int>(candidate.canonical.z) > bestZoom) {
            bestZoom = candidate.canonical.z;
            demTileID = &candidate;
        }
    }
    if (!demTileID) {
        return std::nullopt;
    }
    return *demTileID;
}

const std::shared_ptr<gfx::Texture2D>& RenderTerrain::getPlaceholderDEMTexture(gfx::Context& context) {
    if (!placeholderDEMTexture) {
        auto image = std::make_shared<PremultipliedImage>(Size{1, 1});
        std::memset(image->data.get(), 0, image->bytes());
        placeholderDEMTexture = context.createTexture2D();
        placeholderDEMTexture->setImage(image);
        placeholderDEMTexture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Nearest,
                                                        .wrapU = gfx::TextureWrapType::Clamp,
                                                        .wrapV = gfx::TextureWrapType::Clamp});
    }
    return placeholderDEMTexture;
}

void RenderTerrain::renderDepth(RenderOrchestrator& orchestrator,
                                const RenderTree& renderTree,
                                PaintParameters& parameters) {
    if (!depthLayerGroup || depthLayerGroup->empty()) {
        return;
    }
    prepareDepthTarget(parameters);
    if (!depthRenderTarget) {
        return;
    }
    // The packed-depth output is a function of the camera projection and the terrain mesh set
    // only. When neither changed since the last depth render, the existing depth texture is
    // still correct - skip the whole pass. This is what makes a static scene cheap; the depth
    // is redrawn only on camera movement or a mesh/tile change. (From 604f293; without this
    // gate the pass ran every frame, which is the state that flickered.)
    const mat4& proj = parameters.transformParams.projMatrix;
    const bool cameraMoved = !lastDepthProjMatrix || *lastDepthProjMatrix != proj;
    if (!depthDirty && !cameraMoved) {
        return;
    }

#if MLN_RENDER_BACKEND_OPENGL
    // Instanced depth pass: refresh the per-instance UBO array (camera-dependent transforms)
    // and bind the packed DEM array to the unit the shader's u_dem_array sampler expects. The
    // instanced drawable carries no gfx textures, so nothing else touches this unit during the
    // depth render. (Bind integration is the main on-device shakeout item - Texture2DArray is
    // GL-only and outside the gfx texture abstraction.)
    updateInstancedDepthUBO(parameters);
    // The DEM array is bound by the instanced drawable itself (DrawableGL::setArrayTexture ->
    // bindTextures), so no manual bind here.
#endif
    depthRenderTarget->render(orchestrator, renderTree, parameters);
    lastDepthProjMatrix = proj;
    depthDirty = false;
}

void RenderTerrain::prepareDepthTarget(PaintParameters& parameters) {
    // Called at the start of the render (before the upload phase) as well as from
    // renderDepth, so the depth target already exists when the symbol tweaker binds
    // getDepthTexture() for this frame. Creating it lazily in renderDepth alone left
    // the symbols bound to the far-plane placeholder for that frame - permanently so
    // in single-frame renders like the render tests, where terrain occlusion then
    // never engaged.
    const Size size = parameters.backend.getDefaultRenderable().getSize();
    if (size.isEmpty()) {
        // Early frames can run before the surface has a real size; a degenerate
        // render target here would hand the symbol tweaker a broken texture
        return;
    }
    if (!depthRenderTarget || !depthRenderTarget->getTexture() || depthRenderTarget->getTexture()->getSize() != size) {
        depthRenderTarget = parameters.context.createRenderTarget(
            size, gfx::TextureChannelDataType::UnsignedByte, /*stencil=*/false);
        if (!depthRenderTarget) {
            return;
        }
        // Far plane everywhere the terrain does not cover (unpack_depth(1,1,1,1) ~ 1.0)
        depthRenderTarget->setClearColor(Color::white());
        // The packed digits must not be filtered: blending neighbouring texels'
        // channels decodes to garbage (offscreen textures default to Linear)
        depthRenderTarget->getTexture()->setSamplerConfiguration({.filter = gfx::TextureFilterType::Nearest,
                                                                  .wrapU = gfx::TextureWrapType::Clamp,
                                                                  .wrapV = gfx::TextureWrapType::Clamp});
    }
    // (Re)attach the depth layer group; it may not have existed yet when the
    // target was first created on an early frame
    if (depthLayerGroup) {
        depthRenderTarget->addLayerGroup(depthLayerGroup, /*replace=*/true);
        depthDirty = true; // fresh target must be drawn
    }
}

const std::shared_ptr<gfx::Texture2D>& RenderTerrain::getDepthTexture(gfx::Context& context) {
    if (depthRenderTarget && depthRenderTarget->getTexture()) {
        return depthRenderTarget->getTexture();
    }
    if (!placeholderDepthTexture) {
        // Far-plane packed depth: symbols compare as visible until the pass runs
        auto image = std::make_shared<PremultipliedImage>(Size{1, 1});
        std::memset(image->data.get(), 0xFF, image->bytes());
        placeholderDepthTexture = context.createTexture2D();
        placeholderDepthTexture->setImage(image);
        placeholderDepthTexture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Nearest,
                                                          .wrapU = gfx::TextureWrapType::Clamp,
                                                          .wrapV = gfx::TextureWrapType::Clamp});
    }
    return placeholderDepthTexture;
}

float RenderTerrain::getExaggeration() const {
    return impl->exaggeration;
}

const std::string& RenderTerrain::getSourceID() const {
    return impl->sourceID;
}

bool RenderTerrain::isEnabled() const {
    return !impl->sourceID.empty();
}

const RenderTerrain::TerrainMesh& RenderTerrain::getMesh(gfx::Context& context) {
    if (!mesh) {
        generateMesh(context);
    }
    return *mesh;
}

const RenderTerrain::TerrainMesh& RenderTerrain::getDepthMesh(gfx::Context& context) {
    // The instanced depth pass reuses the full terrain mesh; the source PR's coarser
    // depth-only mesh (getDepthMesh/buildMesh) is a separable optimization not pulled here.
    return getMesh(context);
}

void RenderTerrain::generateMesh(gfx::Context& /*context*/) {
    // A regular grid mesh (reused for every tile, displaced by the DEM in the
    // vertex shader) plus a skirt: each tile edge is duplicated into a curtain
    // that the shader drops by u_ele_delta, hiding the cracks between neighbouring
    // tiles at different zoom levels. Ported from maplibre-gl-js Terrain
    // getTerrainMesh()/_buildSkirts(). TerrainSkirtLength::None builds the bare grid.
    const size_t gridSize = MESH_SIZE;
    const size_t vps = gridSize + 1; // vertices per side
    const float step = static_cast<float>(util::EXTENT) / static_cast<float>(gridSize);

    std::vector<int16_t> vertices;
    std::vector<uint16_t> indices;

    // Each vertex is 4 shorts: x, y, skirt flag (0 = surface, 1 = skirt),
    // unused. uv is derived from x,y in the shader, so the 3rd/4th shorts are free
    // to carry the skirt flag (the native analog of gl-js Pos3d.z).
    const auto addVert = [&](float x, float y, int16_t skirt) {
        vertices.push_back(static_cast<int16_t>(x));
        vertices.push_back(static_cast<int16_t>(y));
        vertices.push_back(skirt);
        vertices.push_back(0);
    };

    // Surface grid
    for (size_t y = 0; y < vps; ++y) {
        for (size_t x = 0; x < vps; ++x) {
            addVert(x * step, y * step, 0);
        }
    }
    for (size_t y = 0; y < gridSize; ++y) {
        for (size_t x = 0; x < gridSize; ++x) {
            const uint16_t topLeft = static_cast<uint16_t>(y * vps + x);
            const uint16_t topRight = static_cast<uint16_t>(topLeft + 1);
            const uint16_t bottomLeft = static_cast<uint16_t>((y + 1) * vps + x);
            const uint16_t bottomRight = static_cast<uint16_t>(bottomLeft + 1);
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // Skirts are baked into the shared mesh; TerrainSkirtLength::None leaves the bare grid.
    if (meshSkirtLength != TerrainSkirtLength::None) {
        // Top/bottom skirt rows (reference the grid's top/bottom edge rows)
        const auto extent = static_cast<float>(util::EXTENT);
        const uint16_t offsetTop = static_cast<uint16_t>(vertices.size() / 4);
        const uint16_t offsetTopEdge = 0;
        const uint16_t offsetBottom = static_cast<uint16_t>(offsetTop + vps);
        const uint16_t offsetBottomEdge = static_cast<uint16_t>(vps * gridSize);
        for (size_t x = 0; x < vps; ++x) {
            addVert(x * step, 0.0f, 1);
        }
        for (size_t x = 0; x < vps; ++x) {
            addVert(x * step, extent, 1);
        }
        for (uint16_t x = 0; x < gridSize; ++x) {
            indices.insert(indices.end(),
                           {static_cast<uint16_t>(offsetBottomEdge + x),
                            static_cast<uint16_t>(offsetBottom + x),
                            static_cast<uint16_t>(offsetBottom + x + 1),
                            static_cast<uint16_t>(offsetBottomEdge + x),
                            static_cast<uint16_t>(offsetBottom + x + 1),
                            static_cast<uint16_t>(offsetBottomEdge + x + 1),
                            static_cast<uint16_t>(offsetTopEdge + x),
                            static_cast<uint16_t>(offsetTop + x + 1),
                            static_cast<uint16_t>(offsetTop + x),
                            static_cast<uint16_t>(offsetTopEdge + x),
                            static_cast<uint16_t>(offsetTopEdge + x + 1),
                            static_cast<uint16_t>(offsetTop + x + 1)});
        }

        // Left/right skirt frames (self-contained strips of paired surface/skirt verts)
        const uint16_t offsetLeft = static_cast<uint16_t>(vertices.size() / 4);
        const uint16_t offsetRight = static_cast<uint16_t>(offsetLeft + vps * 2);
        for (int edge = 0; edge <= 1; ++edge) {
            for (size_t y = 0; y < vps; ++y) {
                for (int16_t z = 0; z <= 1; ++z) {
                    addVert(static_cast<float>(edge) * extent, y * step, z);
                }
            }
        }
        for (uint16_t y = 0; y < gridSize * 2; y += 2) {
            indices.insert(indices.end(),
                           {static_cast<uint16_t>(offsetLeft + y),
                            static_cast<uint16_t>(offsetLeft + y + 1),
                            static_cast<uint16_t>(offsetLeft + y + 3),
                            static_cast<uint16_t>(offsetLeft + y),
                            static_cast<uint16_t>(offsetLeft + y + 3),
                            static_cast<uint16_t>(offsetLeft + y + 2),
                            static_cast<uint16_t>(offsetRight + y),
                            static_cast<uint16_t>(offsetRight + y + 3),
                            static_cast<uint16_t>(offsetRight + y + 1),
                            static_cast<uint16_t>(offsetRight + y),
                            static_cast<uint16_t>(offsetRight + y + 2),
                            static_cast<uint16_t>(offsetRight + y + 3)});
        }
    }

    mesh = TerrainMesh{nullptr, // vertexBuffer - created when building the drawable
                       nullptr, // indexBuffer - created when building the drawable
                       vertices.size() / 4,
                       indices.size(),
                       std::move(vertices),
                       std::move(indices)};
}

std::shared_ptr<gfx::Texture2D> RenderTerrain::createDEMTexture(gfx::Context& context, const DEMData& demData) {
    // Get the DEM image data
    const auto& imagePtr = demData.getImagePtr();
    if (!imagePtr || imagePtr->size.isEmpty()) {
        Log::Warning(Event::Render, "DEM data has no image");
        return nullptr;
    }

    // Create a new texture
    auto texture = context.createTexture2D();
    if (!texture) {
        Log::Error(Event::Render, "Failed to create DEM texture");
        return nullptr;
    }

    // Set the image data
    texture->setImage(imagePtr);

    // Nearest filtering: the packed Terrain-RGB/Terrarium DEM cannot be hardware
    // interpolated (blending the encoded bytes does not blend the decoded
    // elevations), so shaders decode each texel and interpolate in meters via
    // get_elevation(). This matches maplibre-gl-js, which binds the DEM NEAREST.
    texture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Nearest,
                                      .wrapU = gfx::TextureWrapType::Clamp,
                                      .wrapV = gfx::TextureWrapType::Clamp});

    return texture;
}

std::unique_ptr<gfx::Drawable> RenderTerrain::createDrawableForTile(gfx::Context& context,
                                                                    gfx::ShaderRegistry& shaders,
                                                                    const OverscaledTileID& tileID,
                                                                    std::shared_ptr<gfx::Texture2D> demTexture,
                                                                    std::shared_ptr<gfx::Texture2D> mapTexture,
                                                                    bool depthPass) {
    // Ensure mesh is generated
    const auto& terrainMesh = getMesh(context);

    if (terrainMesh.vertices.empty() || terrainMesh.indices.empty()) {
        Log::Error(Event::Render, "Terrain mesh is empty, cannot create drawable");
        return nullptr;
    }

    // Get terrain shader
    auto terrainShader = context.getGenericShader(shaders, depthPass ? "TerrainDepthShader" : "TerrainShader");
    if (!terrainShader) {
        // The depth shader is not registered on all backends yet; symbols
        // then sample the far-plane placeholder and stay visible
        if (!depthPass) {
            Log::Error(Event::Render, "Terrain shader not found");
        }
        return nullptr;
    }

    // Create drawable builder
    auto builder = context.createDrawableBuilder(depthPass ? "terrain-depth-tile" : "terrain-tile");
    if (!builder) {
        Log::Error(Event::Render, "Failed to create drawable builder for terrain tile");
        return nullptr;
    }

    // The drape pass uses the Translucent render pass because it renders in
    // forward order (high index = front), unlike Opaque which renders reversed.
    builder->setShader(terrainShader);
    builder->setRenderPass(RenderPass::Translucent);
    if (depthPass) {
        // The depth pass renders packed depth with real depth testing so the
        // nearest surface wins, into the terrain depth target (renderDepth)
        builder->setDepthType(gfx::DepthMaskType::ReadWrite);
        builder->setColorMode(gfx::ColorMode::unblended());
        builder->setEnableDepth(true);
        builder->setIs3D(true);
    } else {
        // The terrain surface is 3D geometry, so it tests and writes depth: nearer
        // terrain occludes farther terrain, and - crucially - occludes the skirt
        // curtains hanging below each tile edge, so the skirts only show through
        // the cracks they exist to fill rather than drawing over the surface.
        //
        // Symbols (and other layers that occlude against terrain via the depth
        // texture) must not main-depth-test against this surface, or they would be
        // culled by the terrain they sit on - the symbol tweaker disables their
        // depth test while terrain is enabled.
        // Match maplibre-gl-js / Mapbox: the terrain surface is opaque 3D geometry
        // drawn with a depth test+write (LEQUAL, ReadWrite), not the earlier
        // depth-off / "2D for now" hack. On tiled GPUs (this device is PowerVR) opaque
        // depth-tested geometry is eligible for hidden-surface removal, so occluded
        // fragments skip the drape sample instead of always running it.
        builder->setDepthType(gfx::DepthMaskType::ReadWrite);
        builder->setColorMode(gfx::ColorMode::unblended());
        builder->setEnableDepth(true);
        builder->setIs3D(true);
    }

    // Set vertex data - copy vertices to raw buffer
    std::vector<uint8_t> vertexData(terrainMesh.vertices.size() * sizeof(int16_t));
    std::memcpy(vertexData.data(), terrainMesh.vertices.data(), vertexData.size());
    builder->setRawVertices(std::move(vertexData), terrainMesh.vertexCount, gfx::AttributeDataType::Short4);

    // Set index data and segments
    // Create a single segment covering the entire terrain mesh
    SegmentVector segments;
    segments.emplace_back(0,                       // vertex offset
                          0,                       // index offset
                          terrainMesh.vertexCount, // vertex count
                          terrainMesh.indexCount); // index count

    std::vector<uint16_t> indexData = terrainMesh.indices;
    builder->setSegments(gfx::Triangles(), std::move(indexData), segments.data(), segments.size());

    // Set the DEM texture
    if (demTexture) {
        builder->setTexture(demTexture, 0); // Texture index 0 for DEM
    }

    // The depth pass samples only the DEM and writes packed depth, so it has no
    // map texture by design; only the draped pass binds the drape render target
    if (!depthPass) {
        if (mapTexture) {
            builder->setTexture(mapTexture, 1); // Texture index 1 for map
        } else {
            Log::Warning(Event::Render, "No drape texture for terrain tile " + util::toString(tileID));
        }
    }

    // Flush to create the drawable
    builder->flush(context);

    // Get the drawable
    auto drawables = builder->clearDrawables();
    if (drawables.empty()) {
        Log::Error(Event::Render, "Failed to create terrain drawable for tile");
        return nullptr;
    }

    // Set tile ID on the drawable
    auto& drawable = drawables[0];
    drawable->setTileID(tileID);

    return std::move(drawable);
}

void RenderTerrain::activateLayerGroup(bool activate, UniqueChangeRequestVec& changes) {
    if (layerGroup) {
        if (activate) {
            changes.emplace_back(std::make_unique<AddLayerGroupRequest>(layerGroup));
        } else {
            changes.emplace_back(std::make_unique<RemoveLayerGroupRequest>(layerGroup));
        }
    }
}

void RenderTerrain::deactivate(UniqueChangeRequestVec& changes) {
    // depthLayerGroup / depthRenderTarget are owned by this RenderTerrain and
    // released with it; only the mesh layerGroup is registered separately with
    // the orchestrator, so that is all we need to unregister here.
    activateLayerGroup(false, changes);
}

#if MLN_RENDER_BACKEND_OPENGL
void RenderTerrain::packDEMArrayLayer(gfx::Context& context, const UnwrappedTileID& id, const DEMData& demData) {
    const auto& imagePtr = demData.getImagePtr();
    if (!imagePtr || imagePtr->size.isEmpty()) {
        return;
    }
    if (!demTextureArray) {
        demTextureArray = std::make_unique<gl::Texture2DArray>(static_cast<gl::Context&>(context));
    }
    // All DEM tiles from one source share a size, so this allocates once and no-ops after.
    demTextureArray->allocate(imagePtr->size, maxDEMArrayLayers);
    if (!demTextureArray->valid()) {
        return;
    }

    uint32_t layer = 0;
    if (const auto it = demArrayLayer.find(id); it != demArrayLayer.end()) {
        layer = it->second; // re-upload into the tile's existing slot
    } else if (!demArrayFreeLayers.empty()) {
        layer = demArrayFreeLayers.back();
        demArrayFreeLayers.pop_back();
        demArrayLayer[id] = layer;
    } else if (demArrayNextLayer < maxDEMArrayLayers) {
        layer = demArrayNextLayer++;
        demArrayLayer[id] = layer;
    } else {
        return; // array full - tile keeps its per-tile texture, just not instanced
    }
    demTextureArray->uploadLayer(layer, imagePtr->data.get());
}

void RenderTerrain::freeDEMArrayLayer(const UnwrappedTileID& id) {
    if (const auto it = demArrayLayer.find(id); it != demArrayLayer.end()) {
        demArrayFreeLayers.push_back(it->second);
        demArrayLayer.erase(it);
    }
}

// Build the single instanced depth drawable covering the current depthInstances: the shared
// depth mesh drawn N times, with a_instance = [0..N-1] selecting each tile's slot in the
// TerrainDepthInstanceUBO array (filled per frame in updateInstancedDepthUBO). No tile id is
// set, so the terrain tweaker skips it; no gfx textures, since the DEM array is bound manually
// in renderDepth. Called only when the tile set changes.
void RenderTerrain::rebuildInstancedDepthDrawable(gfx::Context& context, gfx::ShaderRegistry& shaders) {
    auto* depthLg = static_cast<LayerGroup*>(depthLayerGroup.get());
    if (!depthLg) {
        return;
    }
    depthLg->clearDrawables();
    const std::size_t n = depthInstances.size();
    if (n == 0 || !demTextureArray || !demTextureArray->valid()) {
        return;
    }

    const auto& depthMeshRef = getDepthMesh(context);
    if (depthMeshRef.vertices.empty() || depthMeshRef.indices.empty()) {
        return;
    }
    auto shader = context.getGenericShader(shaders, "TerrainDepthShader");
    if (!shader) {
        return;
    }
    auto builder = context.createDrawableBuilder("terrain-depth-instanced");
    if (!builder) {
        return;
    }
    builder->setShader(shader);
    builder->setRenderPass(RenderPass::Translucent);
    builder->setDepthType(gfx::DepthMaskType::ReadWrite);
    builder->setColorMode(gfx::ColorMode::unblended());
    builder->setEnableDepth(true);
    builder->setIs3D(true);

    std::vector<uint8_t> vtx(depthMeshRef.vertices.size() * sizeof(int16_t));
    std::memcpy(vtx.data(), depthMeshRef.vertices.data(), vtx.size());
    builder->setRawVertices(std::move(vtx), depthMeshRef.vertexCount, gfx::AttributeDataType::Short4);

    SegmentVector segs;
    segs.emplace_back(0, 0, depthMeshRef.vertexCount, depthMeshRef.indexCount);
    std::vector<uint16_t> idx = depthMeshRef.indices;
    builder->setSegments(gfx::Triangles(), std::move(idx), segs.data(), segs.size());

    // Per-instance index attribute (divisor 1). Its element count is the instance count the
    // GL backend draws (drawInstanced uses instanceAttrs->getMinCount()).
    auto instAttrs = context.createVertexAttributeArray();
    if (const auto& a = instAttrs->set(shaders::idTerrainInstanceVertexAttribute)) {
        for (std::size_t i = 0; i < n; ++i) {
            a->set(i, static_cast<float>(i));
        }
    }
    builder->setInstanceAttributes(std::move(instAttrs));

    builder->flush(context);
    auto drawables = builder->clearDrawables();
    if (!drawables.empty()) {
        // Bind the packed DEM array as u_dem_array (slot idTerrainDEMArrayTexture); DrawableGL
        // binds it in bindTextures() with the program active, using the shader sampler location.
        static_cast<gl::DrawableGL&>(*drawables[0])
            .setArrayTexture(demTextureArray.get(), shaders::idTerrainDEMArrayTexture);
        depthLg->addDrawable(std::move(drawables[0]));
    }
}

// Refresh the per-instance UBO array every frame (the transform depends on the camera) and
// bind it + the shared props UBO directly on the instanced drawable, so the terrain tweaker's
// layer-level TerrainDrawableUBO consolidation does not clobber it. Bound at idTerrainDrawableUBO
// as the array the shader indexes by a_instance.
void RenderTerrain::updateInstancedDepthUBO(PaintParameters& parameters) {
    auto* depthLg = static_cast<LayerGroup*>(depthLayerGroup.get());
    const std::size_t n = depthInstances.size();

    if (!depthLg || n == 0) {
        return;
    }
    auto& context = parameters.context;

    // The shader declares the block as a fixed array u_inst[TERRAIN_MAX_INSTANCES]
    // (== maxDepthInstances), so GLES requires the bound buffer/range to be at least
    // that full static size (sizeof(UBO) * maxDepthInstances). Allocate the whole block
    // and fill only the first n entries; the rest stay zero-initialized. Sizing the
    // buffer to n instead triggers "Bound buffer is too small" and the draw is dropped.
    std::vector<shaders::TerrainDepthInstanceUBO> arr(maxDepthInstances);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& inst = depthInstances[i];
        mat4 m = parameters.matrixForTile(inst.tileID.toUnwrapped());
#if !MLN_RENDER_BACKEND_OPENGL
        m[2] = 0.5 * (m[2] + m[3]);
        m[6] = 0.5 * (m[6] + m[7]);
        m[10] = 0.5 * (m[10] + m[11]);
        m[14] = 0.5 * (m[14] + m[15]);
#endif
        arr[i].matrix = util::cast<float>(m);
        arr[i].dem_coords = inst.demCoords;
        arr[i].dem_layer = inst.demLayer;
        arr[i].pad1 = arr[i].pad2 = arr[i].pad3 = 0.0f;
    }
    const std::size_t bytes = sizeof(shaders::TerrainDepthInstanceUBO) * maxDepthInstances;
    if (!depthInstanceUBO || depthInstanceUBO->getSize() < bytes) {
        depthInstanceUBO = context.createUniformBuffer(arr.data(), bytes, false, true);
    } else {
        depthInstanceUBO->update(arr.data(), bytes);
    }

    // Shared evaluated props (unpack / exaggeration / skirt offset), same as the tweaker.
    const auto zoom = std::max(parameters.state.getZoom(), 0.0);
    const float elevationOffset = static_cast<float>(util::M2PI * util::EARTH_RADIUS_M / std::pow(2.0, zoom) / 5.0);
    const shaders::TerrainEvaluatedPropsUBO propsUBO = {.unpack = getDEMUnpackVector(),
                                                        .exaggeration = getExaggeration(),
                                                        .elevation_offset = elevationOffset,
                                                        .pad1 = 0.0f,
                                                        .pad2 = 0.0f};

    depthLg->visitDrawables([&](gfx::Drawable& drawable) {
        auto& u = drawable.mutableUniformBuffers();
        u.set(shaders::idTerrainDrawableUBO, depthInstanceUBO);
        u.createOrUpdate(shaders::idTerrainEvaluatedPropsUBO, &propsUBO, context);
    });
}
#endif

} // namespace mln
