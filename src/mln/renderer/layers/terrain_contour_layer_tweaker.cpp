#include <mln/renderer/layers/terrain_contour_layer_tweaker.hpp>

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

namespace mln {

using namespace style;
using namespace shaders;

namespace {

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

// contours3d.js's REF_RATIO (2): u_minor_w/u_index_w/u_fade_lo/u_fade_hi are calibrated as RAW
// pixels at a render ratio of 2 - see that constant's own comment in contours3d.js for why (the
// gallery it was tuned against always renders at dpr 2). Scaled by (live pixelRatio / 2) here so
// the calibrated CSS-pixel appearance holds at any render ratio, exactly as the web does.
constexpr float kReferenceRatio = 2.0f;

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

    // Computed lazily below, from the first drawable this frame supplies: computeReferenceClipW
    // needs one drawable's own (tileID, matrix) pair to be exact - see that function's comment.
    // "Any one drawable's tile" is exact because same-zoom tiles' matrices differ only by a
    // translation in tile units; the tile cover here is overwhelmingly uniform-zoom, and being
    // off by one tile's translation on the rare mixed-LOD edge changes the reference by far less
    // than the bug this replaces.
    bool haveReferenceW = false;
    float referenceW = 1e-4f;
    const float pixelScale = parameters.pixelRatio / kReferenceRatio;

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

        if (!haveReferenceW) {
            referenceW = computeReferenceClipW(parameters, tileID, matrix);
            haveReferenceW = true;
        }

        // Bind the covering DEM tile so the vertex/fragment shaders can sample the terrain
        // elevation directly - RenderTerrain::getTerrainData, the same call
        // symbol_layer_tweaker.cpp:170-234 and TerrainLineLayerTweaker::execute make.
        std::optional<RenderTerrain::TerrainData> terrainData;
        if (parameters.terrain) {
            terrainData = parameters.terrain->getTerrainData(tileID);
            drawable.setTexture(
                terrainData ? terrainData->demTexture : parameters.terrain->getPlaceholderDEMTexture(context),
                idTerrainContourDEMTexture);
        } else {
            // Keep the declared DEM sampler bound for Metal API validation (never sampled -
            // update() already tears this layer's drawables down whenever terrain is off).
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainContourDEMTexture);
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
        };
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

} // namespace mln
