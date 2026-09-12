#include <mln/renderer/layers/terrain_contour_layer_tweaker.hpp>
#include <mln/renderer/layers/terrain_line_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/terrain_contour_layer_ubo.hpp>
#include <mln/style/layers/terrain_contour_layer_impl.hpp>
#include <mln/style/layers/terrain_contour_layer_properties.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/math.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/tile_coordinate.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

// DuckMaps fork only, task C6: debug-only, off-by-default trace feeding
// TerrainContourLayerTweaker::debugDrainContourReferenceTraceJSON. Same shape as
// DEMElevationProvider's own elevationTraceEnabled()/log: getenv checked once (static), the
// slot holds at most one frame's worth of data and is cleared on drain, so a frame where this
// layer group is empty (execute() returns before this runs) correctly reports "null" rather
// than repeating a stale frame.
bool contourTraceEnabled() {
    static const bool enabled = [] {
        const char* path = std::getenv("DUCKMAPS_ELEVATION_TRACE");
        return path && *path;
    }();
    return enabled;
}

std::optional<std::string>& contourTraceSlot() {
    static std::optional<std::string> slot;
    return slot;
}

// DuckMaps fork only, task C7: the per-drawable UBO/texture trace's own slot, filled by the
// second visitLayerGroupDrawables pass in execute() (the one that builds the real UBOs) and
// drained once per frame by Renderer::Impl::render. Same "static, cleared on drain" contract as
// contourTraceSlot() above - a frame where this layer group is empty leaves the vector empty
// rather than repeating the previous frame's entries, since execute() returns before either
// slot is touched.
std::vector<DebugDrawableUBOEntry>& contourUboEntriesSlot() {
    static std::vector<DebugDrawableUBOEntry> slot;
    return slot;
}

std::string tileIdDebugKey(const UnwrappedTileID& id) {
    std::ostringstream os;
    os << id.wrap << "/" << static_cast<int>(id.canonical.z) << "/" << id.canonical.x << "/" << id.canonical.y;
    return os.str();
}

// Predecessor's hypothesis: a parent and a child mesh tile both carrying contour drawables
// over the same ground. True exactly when, for some pair in the same wrap, one tile's
// canonical coordinates at the OTHER's zoom equal the other's - i.e. one is an ancestor of
// the other in the quadtree. O(n^2) over the tiny (single-digit) per-frame drawable tile
// count, so this being debug-only costs nothing when off and is cheap when on.
int countAncestorPairs(const std::vector<UnwrappedTileID>& tiles) {
    int count = 0;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        for (std::size_t j = i + 1; j < tiles.size(); ++j) {
            const UnwrappedTileID& a = tiles[i];
            const UnwrappedTileID& b = tiles[j];
            if (a.wrap != b.wrap) {
                continue;
            }
            const UnwrappedTileID& shallow = a.canonical.z <= b.canonical.z ? a : b;
            const UnwrappedTileID& deep = a.canonical.z <= b.canonical.z ? b : a;
            if (shallow.canonical.z == deep.canonical.z) {
                continue; // same zoom: neither can be the other's ancestor.
            }
            const int shift = deep.canonical.z - shallow.canonical.z;
            const int deepXAtShallow = deep.canonical.x >> shift;
            const int deepYAtShallow = deep.canonical.y >> shift;
            if (deepXAtShallow == static_cast<int>(shallow.canonical.x) &&
                deepYAtShallow == static_cast<int>(shallow.canonical.y)) {
                ++count;
            }
        }
    }
    return count;
}

// The map centre's own position expressed in tileID's EXTENT-unit local coordinate space -
// the same units a_pos carries, unclamped (the point need not fall inside this tile's own
// footprint). Two tiles at the SAME zoom differ only by a translation in tile units, so this
// stays exact for whichever tile's matrix the caller happens to use as the reference, not only
// for the tile that actually contains the centre.
Point<double> tileLocalPosition(const UnwrappedTileID& tileID, const LatLng& latLng) {
    const TileCoordinate coord = TileCoordinate::fromLatLng(static_cast<double>(tileID.canonical.z), latLng);
    const double tileScale = std::exp2(static_cast<double>(tileID.canonical.z));
    return {(coord.p.x - static_cast<double>(tileID.canonical.x) - tileID.wrap * tileScale) * util::EXTENT,
            (coord.p.y - static_cast<double>(tileID.canonical.y)) * util::EXTENT};
}

// contours3d.js's referenceClipW() (lines 896-904): the clip-space w at the ground under the
// map centre, at the centre's own sampled elevation.
//
// This MUST be computed with the exact matrix a drawable's own geometry is transformed by
// (parameters.matrixForTile(tileID), remapped for non-GL backends exactly as this file's own
// execute() remaps each drawable's matrix below - the remap only touches the z row, so it makes
// no difference to w, but using the identical matrix removes any doubt). The previous version of
// this function instead rebuilt its own world-pixel position via Projection::project(center,
// state.getScale()) and multiplied it by state.getProjectionMatrix() directly - the SAME
// projection matrix component matrixForTile itself composes with state.matrixFor(tileID), so
// that part was not the defect. The real bug was the z it fed in: it converted the centre's
// elevation from metres to world pixels by hand (dividing by
// Projection::getMetersPerPixelAtLatitude), but Camera::getWorldToCamera (src/mln/util/camera.cpp)
// scales whatever z it is given by pixelsPerMeter INTERNALLY, unconditionally, for every matrix
// built off TransformState::getProjMatrix - see TransformState::latLngToScreenCoordinate's own
// comment ("World z is metres here, not pixels") and terrain.vertex.glsl, which passes its
// decoded elevation straight through in metres with no conversion at all. Pre-dividing by
// metresPerPixel and then letting the matrix divide by it again shrank the z contribution to
// clip-w by a second, spurious factor of metresPerPixel - wrong in a pitch- and
// slope-dependent way, which is exactly the shape of both reported defects (crowding that
// varies with the wall's own steepness, and index/minor saturating to the same look).
//
// Fixed by dropping the manual conversion entirely: elevationM is passed straight through, in
// metres, exactly as the vertex shader's own `elevation` does, and multiplied by the SAME
// per-tile matrix a drawable uses, at the centre's position expressed in that tile's own
// EXTENT-unit local coordinates (tileLocalPosition above) rather than at a separately-computed
// world-pixel position.
float computeReferenceClipW(const PaintParameters& parameters, const UnwrappedTileID& tileID, const mat4& matrix) {
    const auto& state = parameters.state;
    const LatLng center = state.getLatLng();
    const double elevationM = (parameters.terrain && parameters.terrain->isEnabled())
                                  ? parameters.terrain->getElevationForLatLng(center)
                                  : 0.0;
    const Point<double> local = tileLocalPosition(tileID, center);

    vec4 localPos = {{local.x, local.y, elevationM, 1.0}};
    vec4 clip;
    matrix::transformMat4(clip, localPos, matrix);
    const float w = static_cast<float>(clip[3]);
    return w > 1e-4f ? w : 1e-4f;
}

Color premultiply(const Color& c, float opacity) {
    const float a = c.a * opacity;
    return {c.r * a, c.g * a, c.b * a, a};
}

} // namespace

void TerrainContourLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    auto& context = parameters.context;
    const auto& properties = static_cast<const TerrainContourLayerProperties&>(*evaluatedProperties);
    const auto& evaluated = properties.evaluated;

#ifndef NDEBUG
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    // computeReferenceClipW needs one drawable's own (tileID, matrix) pair to be exact - see
    // that function's comment. WHICH tile is not a free choice, and taking whichever drawable
    // the layer group happened to supply first was a real defect: the drawables are visited in
    // insertion order, which follows tile load order, so two runs of the identical harness link
    // picked different reference tiles and every contour line in the frame came out a fraction
    // of a pixel wider or narrower. Measured at the Gavarnie wall: five runs, terrain and DEM
    // bindings and mesh cover byte-identical on every instrument, and up to 228 680 of
    // 3 162 132 pixels differing, 92 percent of them by one or two of 255 and all of them on
    // contour ink. With the native families off the same five runs were byte-identical, which
    // is what located it here.
    //
    // The reference point IS the map centre, so the exact tile to evaluate it in is the tile
    // that CONTAINS the map centre, at the deepest zoom present. That is a property of the
    // frame, not of the load order, so it is the same in every run. Where no drawable covers
    // the centre (the centre is off the layer's cover), fall back to the lowest tile key
    // present, which is equally arbitrary in appearance and equally deterministic.
    std::optional<UnwrappedTileID> referenceTile;
    const LatLng referenceCentre = parameters.state.getLatLng();
    const auto tileHoldsCentre = [&](const UnwrappedTileID& id) {
        const Point<double> local = tileLocalPosition(id, referenceCentre);
        return local.x >= 0.0 && local.x <= util::EXTENT && local.y >= 0.0 && local.y <= util::EXTENT;
    };
    const auto tileKey = [](const UnwrappedTileID& id) {
        return std::tuple{id.wrap, id.canonical.z, id.canonical.x, id.canonical.y};
    };
    // DuckMaps fork only, task C6: the trace's own record of every tile this frame has a
    // contour drawable for, gathered here rather than re-visiting the layer group, since this
    // loop already sees every candidate. Only populated when the trace is on.
    std::vector<UnwrappedTileID> traceTileIds;
    const bool traceOn = contourTraceEnabled();
    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }
        const UnwrappedTileID candidate = drawable.getTileID()->toUnwrapped();
        if (traceOn) {
            traceTileIds.push_back(candidate);
        }
        if (!referenceTile) {
            referenceTile = candidate;
            return;
        }
        const bool candidateHolds = tileHoldsCentre(candidate);
        const bool currentHolds = tileHoldsCentre(*referenceTile);
        if (candidateHolds != currentHolds) {
            if (candidateHolds) {
                referenceTile = candidate;
            }
            return;
        }
        if (candidateHolds) {
            // Both cover the centre: the deepest one is the tile the viewer is actually on.
            if (candidate.canonical.z > referenceTile->canonical.z ||
                (candidate.canonical.z == referenceTile->canonical.z && tileKey(candidate) < tileKey(*referenceTile))) {
                referenceTile = candidate;
            }
        } else if (tileKey(candidate) < tileKey(*referenceTile)) {
            referenceTile = candidate;
        }
    });

    float referenceW = 1e-4f;
    if (referenceTile) {
        mat4 referenceMatrix = parameters.matrixForTile(*referenceTile);
#if !MLN_RENDER_BACKEND_OPENGL
        referenceMatrix[2] = 0.5 * (referenceMatrix[2] + referenceMatrix[3]);
        referenceMatrix[6] = 0.5 * (referenceMatrix[6] + referenceMatrix[7]);
        referenceMatrix[10] = 0.5 * (referenceMatrix[10] + referenceMatrix[11]);
        referenceMatrix[14] = 0.5 * (referenceMatrix[14] + referenceMatrix[15]);
#endif
        referenceW = computeReferenceClipW(parameters, *referenceTile, referenceMatrix);
    }

    // DuckMaps fork only, task C6: debug-only, off-by-default trace of the values step 1 of
    // the reference-w investigation needs - see debugDrainContourReferenceTraceJSON's own
    // comment. Entirely skipped (including the elevation query and the sort) when
    // DUCKMAPS_ELEVATION_TRACE is unset, so this costs nothing in a normal run.
    if (traceOn) {
        const double traceElevationM = (parameters.terrain && parameters.terrain->isEnabled())
                                            ? parameters.terrain->getElevationForLatLng(referenceCentre)
                                            : 0.0;
        // DuckMaps fork only, task C6: the RAW iteration order, before the sort below, kept
        // separately - this is the order the second visitLayerGroupDrawables pass below builds
        // per-drawable UBOs in, which (if it also governs GPU draw submission order, not proven
        // here) is a candidate explanation for a run-to-run pixel difference that survives every
        // OTHER input this trace shows to be bit-identical: with ancestorPairCount > 0, more than
        // one tile's contour mesh draws semi-transparent ink over the SAME ground, and alpha
        // blending is order-dependent, so a different submission order alone could account for a
        // uniform few-LSB shift on every contour pixel without moving referenceW, any matrix, or
        // any DEM/mesh input at all.
        std::vector<UnwrappedTileID> traceDrawOrder = traceTileIds;
        std::sort(traceTileIds.begin(), traceTileIds.end(),
                  [&](const UnwrappedTileID& a, const UnwrappedTileID& b) { return tileKey(a) < tileKey(b); });
        std::ostringstream os;
        os << std::setprecision(9);
        os << "{\"referenceTile\":";
        if (referenceTile) {
            os << "\"" << tileIdDebugKey(*referenceTile) << "\"";
        } else {
            os << "null";
        }
        os << ",\"referenceW\":" << referenceW << ",\"referenceWHex\":\"" << std::hexfloat << referenceW
           << std::defaultfloat << "\"" << ",\"centerElevationM\":" << traceElevationM
           << ",\"centerElevationMHex\":\"" << std::hexfloat << traceElevationM << std::defaultfloat << "\""
           << ",\"drawableTileIds\":[";
        bool firstTile = true;
        for (const auto& id : traceTileIds) {
            if (!firstTile) {
                os << ",";
            }
            firstTile = false;
            os << "\"" << tileIdDebugKey(id) << "\"";
        }
        os << "],\"ancestorPairCount\":" << countAncestorPairs(traceTileIds) << ",\"drawOrder\":[";
        bool firstOrder = true;
        for (const auto& id : traceDrawOrder) {
            if (!firstOrder) {
                os << ",";
            }
            firstOrder = false;
            os << "\"" << tileIdDebugKey(id) << "\"";
        }
        os << "]}";
        contourTraceSlot() = os.str();
    }

    // contours3d.js's REF_RATIO: u_minor_w/u_index_w/u_fade_lo/u_fade_hi are calibrated as RAW
    // pixels at one reference render ratio (the gallery David tuned them against always renders
    // at dpr 2). Scaled by (live pixelRatio / that reference) here so the calibrated CSS-pixel
    // appearance holds at any render ratio, exactly as the web does at contours3d.js:775-779.
    //
    // The reference is a STYLE property rather than a constant here, 12 September 2026, and that
    // is deliberate: the same four dials are read by the DuckMaps web engine, which does the
    // identical scaling against its own REF_RATIO, and David's rule is that a tuned value has one
    // definition. Both engines now read style.py's CONTOUR3D_REF_RATIO through
    // /style-tokens.json, so a 2 written in this file would be a second copy of it. Guarded
    // against a zero or negative ratio in a hand-written style, which would otherwise divide by
    // zero and blank every contour in the frame.
    const float referenceRatio = evaluated.get<TerrainContourReferenceRatio>();
    const float pixelScale = referenceRatio > 0.f ? parameters.pixelRatio / referenceRatio : 1.f;

    // Task: the terrain depth-texture occlusion test, ported onto terrain-contour from
    // terrain-line's own TerrainLineLayerTweaker::execute (task 2.2) - see
    // terrain_contour_layer_ubo.hpp's comment on TerrainContourTilePropsUBO for why. All four of
    // these are FRAME-level facts (the depth texture is one shared render target; the projection
    // matrix and terrain-on/off do not vary per tile), computed once here exactly like
    // terrain-line's own copy, and copied into every tile's TilePropsUBO entry below.
    const bool terrainEnabledForOcclusion = parameters.terrain && parameters.terrain->isEnabled();
    const std::shared_ptr<gfx::Texture2D> depthTexture = parameters.terrain
                                                             ? parameters.terrain->getDepthTexture(context)
                                                             : nullptr;
    // RenderTerrain::getDepthTexture() falls back to a 1x1 far-plane placeholder whenever the
    // depth pass has not produced a real texture yet - texture size is the only public signal
    // that tells the two apart, so depth_enabled follows it rather than terrainEnabledForOcclusion
    // alone (same reasoning as terrain-line's own hasRealDepthTexture).
    const bool hasRealDepthTexture = depthTexture && depthTexture->getSize() != Size{1, 1};
    const float depthEnabled = (terrainEnabledForOcclusion && hasRealDepthTexture) ? 1.0f : 0.0f;
    const Size depthSize = hasRealDepthTexture ? depthTexture->getSize() : Size{1, 1};
    const std::array<float, 2> depthTexel = {1.0f / static_cast<float>(depthSize.width),
                                             1.0f / static_cast<float>(depthSize.height)};
    const float occlusionFar = occlusionFarNDC(parameters, OCCLUSION_EPS_M_DEFAULT);

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const TerrainContourEvaluatedPropsUBO evaluatedPropsUBO = {
            .minor_color = premultiply(evaluated.get<TerrainContourMinorColor>(),
                                       evaluated.get<TerrainContourMinorOpacity>()),
            .index_color = premultiply(evaluated.get<TerrainContourIndexColor>(),
                                       evaluated.get<TerrainContourIndexOpacity>()),
            .minor_interval = evaluated.get<TerrainContourMinorInterval>(),
            .index_interval = evaluated.get<TerrainContourIndexInterval>(),
            .minor_width = evaluated.get<TerrainContourMinorWidth>() * pixelScale,
            .index_width = evaluated.get<TerrainContourIndexWidth>() * pixelScale,
            .fade_lo = evaluated.get<TerrainContourFadeLo>() * pixelScale,
            .fade_hi = evaluated.get<TerrainContourFadeHi>() * pixelScale,
            // Constants, not paint properties - contours3d.js DEPTH_BIAS (0.0003) / SLOPE_BIAS
            // (8.0), see that file's own comments for the derivation of both. DEPTH_BIAS is
            // re-derived here, not copied: contours3d.js's 0.0003 is a raw clip-z subtraction
            // calibrated against a GL-convention clip volume of width 2 (z in [-1, 1]), i.e. a
            // pull of 0.0003 / 2 = 0.00015 of the total depth range. This shader's vertex stage
            // applies the SAME subtraction (p.z -= depth_bias * p.w) to a matrix that has already
            // been remapped to Metal's [0, 1] clip convention (see the matrix build above) - a
            // clip volume of width 1. Left at 0.0003, the same absolute subtraction would now
            // consume 0.0003 / 1 of that narrower range, i.e. twice the intended fraction of the
            // depth buffer's precision. Halved to keep the intended fraction (0.00015) constant
            // across the remap.
            .depth_bias = 0.00015f,
            .slope_bias = 8.0f,
        };
        context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &evaluatedPropsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idTerrainContourEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

    // DuckMaps fork only, task C7: the evaluated-props UBO's own bytes, for the per-drawable UBO
    // trace below - rebuilt fresh here (rather than reusing the struct inside the `if
    // (!evaluatedPropsUniformBuffer || propertiesUpdated)` block above) because that struct is
    // only recomputed on the frames the properties actually change, and this trace needs every
    // drawable's full UBO set on every traced frame regardless. Field-for-field identical to the
    // real construction above; the two must be kept in sync by hand since this is a second build
    // of the same struct, not a shared helper - acceptable for a debug-only trace that costs
    // nothing when off. Zero-initialised and skipped when the trace is off.
    TerrainContourEvaluatedPropsUBO evaluatedPropsUBOForTrace{};
    if (traceOn) {
        evaluatedPropsUBOForTrace = {
            .minor_color = premultiply(evaluated.get<TerrainContourMinorColor>(),
                                       evaluated.get<TerrainContourMinorOpacity>()),
            .index_color = premultiply(evaluated.get<TerrainContourIndexColor>(),
                                       evaluated.get<TerrainContourIndexOpacity>()),
            .minor_interval = evaluated.get<TerrainContourMinorInterval>(),
            .index_interval = evaluated.get<TerrainContourIndexInterval>(),
            .minor_width = evaluated.get<TerrainContourMinorWidth>() * pixelScale,
            .index_width = evaluated.get<TerrainContourIndexWidth>() * pixelScale,
            .fade_lo = evaluated.get<TerrainContourFadeLo>() * pixelScale,
            .fade_hi = evaluated.get<TerrainContourFadeHi>() * pixelScale,
            .depth_bias = 0.00015f,
            .slope_bias = 8.0f,
        };
    }

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    std::vector<TerrainContourDrawableUBO> drawableUBOVector(layerGroup.getDrawableCount());
    std::vector<TerrainContourTilePropsUBO> tilePropsUBOVector(layerGroup.getDrawableCount());
#endif

    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }
        const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();

        // This mesh is drawn directly in world space over RenderTerrain's own surface (never
        // draped), exactly like TerrainLayerTweaker's own drawables for the SAME tile and the
        // SAME elevations - so it must be built the same way, not via LayerTweaker::getTileMatrix
        // (which is the draped/2D-layer path: on non-GL backends its non-terrain branch does not
        // remap clip z from GL's [-1, 1] convention to Metal/Vulkan/WebGPU's [0, 1], because 2D
        // layers overwhelmingly project into the far half of the GL clip volume and never notice).
        // A terrain mesh reaches into the near half at ordinary pitch, which is exactly why
        // TerrainLayerTweaker::execute (terrain_layer_tweaker.cpp:67-86) builds its matrix from
        // parameters.matrixForTile() and then applies this remap by hand, with a comment noting
        // that without it "terrain that projects into the near half of the GL clip volume (z < 0)
        // is not clipped away on those backends" - i.e. it IS clipped away without the remap. This
        // layer draws the identical mesh at the identical elevations and was missing that same
        // remap, so its geometry was silently clipped in its entirety on Metal: zero contour
        // pixels at every pitch, including pitch 0, matches full clipping rather than a partial
        // visual defect.
        mat4 matrix = parameters.matrixForTile(tileID);
#if !MLN_RENDER_BACKEND_OPENGL
        matrix[2] = 0.5 * (matrix[2] + matrix[3]);
        matrix[6] = 0.5 * (matrix[6] + matrix[7]);
        matrix[10] = 0.5 * (matrix[10] + matrix[11]);
        matrix[14] = 0.5 * (matrix[14] + matrix[15]);
#endif

        // Bind the covering DEM tile so the vertex/fragment shaders can sample the terrain
        // elevation directly - RenderTerrain::getTerrainData, the same call
        // symbol_layer_tweaker.cpp:170-234 and TerrainLineLayerTweaker::execute make.
        std::optional<RenderTerrain::TerrainData> terrainData;
        if (parameters.terrain) {
            terrainData = parameters.terrain->getTerrainData(tileID);
            drawable.setTexture(
                terrainData ? terrainData->demTexture : parameters.terrain->getPlaceholderDEMTexture(context),
                idTerrainContourDEMTexture);
            // Packed terrain depth for the occlusion test below (same pattern as terrain-line's
            // own binding; the texture itself is fetched once per frame above).
            drawable.setTexture(depthTexture, idTerrainContourDepthTexture);
        } else {
            // Keep the declared DEM/depth samplers bound for Metal API validation (never sampled -
            // update() already tears this layer's drawables down whenever terrain is off).
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainContourDEMTexture);
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainContourDepthTexture);
        }

        const auto demCoords = terrainData ? terrainData->demCoords : std::array<float, 4>{{0, 0, 0, 0}};
        const auto demUnpack = parameters.terrain ? parameters.terrain->getDEMUnpackVector()
                                                  : std::array<float, 4>{{0, 0, 0, 0}};
        const float demDim = terrainData ? terrainData->demDim : 0.0f;
        const float demExaggeration = parameters.terrain ? parameters.terrain->getExaggeration() : 0.0f;
        const float demEnabled = terrainData ? 1.0f : 0.0f;

#if MLN_UBO_CONSOLIDATION
        drawableUBOVector[i] = {
#else
        const TerrainContourDrawableUBO drawableUBO = {
#endif
            .matrix = util::cast<float>(matrix),
            .dem_coords = demCoords,
            .dem_unpack = demUnpack,
            .dem_dim = demDim,
            .dem_exaggeration = demExaggeration,
            .dem_enabled = demEnabled,
            .pad0 = 0.0f,
        };
        // Fragment-only duplicate of the dem_* fields above, plus reference_w - see
        // TerrainContourDrawableUBO's comment in terrain_contour_layer_ubo.hpp for why the
        // fragment stage cannot simply read the struct above.
#if MLN_UBO_CONSOLIDATION
        tilePropsUBOVector[i] = {
#else
        const TerrainContourTilePropsUBO tilePropsUBO = {
#endif
            .dem_coords = demCoords,
            .dem_unpack = demUnpack,
            .dem_dim = demDim,
            .dem_exaggeration = demExaggeration,
            .dem_enabled = demEnabled,
            .reference_w = referenceW,
            .occlusion_eps = OCCLUSION_EPS_DEFAULT,
            .occlusion_far = occlusionFar,
            .depth_texel = depthTexel,
            .depth_enabled = depthEnabled,
            .pad1 = 0.0f,
            .pad2 = 0.0f,
            .pad3 = 0.0f,
        };

        // DuckMaps fork only, task C7: the per-drawable UBO/texture trace entry, built from the
        // exact CPU-side struct values just above rather than reading back the GPU buffer -
        // correct even under MLN_UBO_CONSOLIDATION (see terrain_contour_layer_tweaker.hpp's
        // comment on debugDrainContourDrawableUBOEntries): the consolidated path memcpys these
        // SAME bytes into the layer group's shared buffer at this drawable's uboIndex, so hashing
        // them here is hashing exactly what ends up bound, without needing to know which branch
        // compiled. Hashed in binding-index order: the vertex-only drawable UBO
        // (idTerrainContourDrawableUBO), then the fragment-only tile-props UBO
        // (idTerrainContourTilePropsUBO), then the shared evaluated-props UBO
        // (idTerrainContourEvaluatedPropsUBO) - ascending on this (Metal) backend, see
        // shader_defines.hpp's getEnumValue(packed, unpacked) for non-Vulkan backends resolving
        // to `packed`, which places idTerrainContourEvaluatedPropsUBO at
        // terrainContourLayerSSBOCount == drawableReservedUBOCount, strictly after both
        // reserved per-drawable ids.
        if (traceOn) {
#if MLN_UBO_CONSOLIDATION
            const TerrainContourDrawableUBO& drawableUBORef = drawableUBOVector[i];
            const TerrainContourTilePropsUBO& tilePropsUBORef = tilePropsUBOVector[i];
#else
            const TerrainContourDrawableUBO& drawableUBORef = drawableUBO;
            const TerrainContourTilePropsUBO& tilePropsUBORef = tilePropsUBO;
#endif
            uint64_t uboHash = debugFnv1a64(&drawableUBORef, sizeof(drawableUBORef));
            uboHash = debugFnv1a64(&tilePropsUBORef, sizeof(tilePropsUBORef), uboHash);
            uboHash = debugFnv1a64(&evaluatedPropsUBOForTrace, sizeof(evaluatedPropsUBOForTrace), uboHash);

            std::string texIdentity;
            if (parameters.terrain) {
                if (terrainData) {
                    if (const auto demTileId = parameters.terrain->debugDemTileIdForTile(tileID)) {
                        texIdentity = "dem:" + tileIdDebugKey(*demTileId);
                    } else {
                        // getTerrainData and debugDemTileIdForTile run the identical resolution
                        // loop, so this should not happen; reported rather than silently reused
                        // so a real mismatch would be visible in the trace, not hidden.
                        texIdentity = "dem:resolved-but-untraced";
                    }
                } else {
                    texIdentity = "dem:placeholder";
                }
            } else {
                texIdentity = "dem:terrain-off-placeholder";
            }

            DebugDrawableUBOEntry entry;
            entry.layer = layerGroup.getName();
            entry.id = tileIdDebugKey(tileID);
            entry.pass = drawable.getDrawPriority();
            entry.ubo = uboHash;
            entry.tex = {texIdentity};
            entry.wrap = tileID.wrap;
            entry.z = tileID.canonical.z;
            entry.x = tileID.canonical.x;
            entry.y = tileID.canonical.y;
            contourUboEntriesSlot().push_back(std::move(entry));
        }

#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(i++);
#else
        auto& drawableUniforms = drawable.mutableUniformBuffers();
        drawableUniforms.createOrUpdate(idTerrainContourDrawableUBO, &drawableUBO, context);
        drawableUniforms.createOrUpdate(idTerrainContourTilePropsUBO, &tilePropsUBO, context);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    const size_t drawableUBOVectorSize = sizeof(TerrainContourDrawableUBO) * drawableUBOVector.size();
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < drawableUBOVectorSize) {
        drawableUniformBuffer = context.createUniformBuffer(
            drawableUBOVector.data(), drawableUBOVectorSize, false, true);
    } else {
        drawableUniformBuffer->update(drawableUBOVector.data(), drawableUBOVectorSize);
    }
    layerUniforms.set(idTerrainContourDrawableUBO, drawableUniformBuffer);

    const size_t tilePropsUBOVectorSize = sizeof(TerrainContourTilePropsUBO) * tilePropsUBOVector.size();
    if (!tilePropsUniformBuffer || tilePropsUniformBuffer->getSize() < tilePropsUBOVectorSize) {
        tilePropsUniformBuffer = context.createUniformBuffer(
            tilePropsUBOVector.data(), tilePropsUBOVectorSize, false, true);
    } else {
        tilePropsUniformBuffer->update(tilePropsUBOVector.data(), tilePropsUBOVectorSize);
    }
    layerUniforms.set(idTerrainContourTilePropsUBO, tilePropsUniformBuffer);
#endif
}

// DuckMaps fork only, task C6: drained by Renderer::Impl::render's own DUCKMAPS_ELEVATION_TRACE
// block exactly once per frame, the same way DEMElevationProvider::debugDrainElevationQueries
// is - see that function's comment. Clears the slot so a frame in which this layer group was
// empty (execute() returned at its very first line, before the trace block ever runs) reports
// "null" rather than repeating the previous frame's record.
std::string TerrainContourLayerTweaker::debugDrainContourReferenceTraceJSON() {
    auto& slot = contourTraceSlot();
    if (!slot) {
        return "null";
    }
    std::string result = std::move(*slot);
    slot.reset();
    return result;
}

// DuckMaps fork only, task C7: see the .hpp declaration's own comment. Drained (and cleared)
// exactly once per frame by Renderer::Impl::render, alongside debugDrainContourReferenceTraceJSON
// above - empty when the trace is off or this layer group drew nothing this frame.
std::vector<DebugDrawableUBOEntry> TerrainContourLayerTweaker::debugDrainContourDrawableUBOEntries() {
    auto& slot = contourUboEntriesSlot();
    std::vector<DebugDrawableUBOEntry> result = std::move(slot);
    slot.clear();
    return result;
}

} // namespace mln
