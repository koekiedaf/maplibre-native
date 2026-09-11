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

#include <algorithm>
#include <cmath>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

// contours3d.js's referenceClipW() (lines 896-904): the clip-space w at the ground under the
// map centre, at the centre's own sampled elevation. This is the same computation
// TerrainLineLayerTweaker::computeReferenceClipW makes (see that file's comment for the full
// derivation) - duplicated rather than shared because the two tweakers are otherwise
// independent and neither is a natural home for a shared helper this small.
float computeReferenceClipW(const PaintParameters& parameters) {
    const auto& state = parameters.state;
    const LatLng center = state.getLatLng();
    const double elevationM = (parameters.terrain && parameters.terrain->isEnabled())
                                  ? parameters.terrain->getElevationForLatLng(center)
                                  : 0.0;
    const double metresPerPixel = Projection::getMetersPerPixelAtLatitude(center.latitude(), state.getZoom());
    const Point<double> worldXY = Projection::project(center, state.getScale());
    const double worldZ = metresPerPixel > 0.0 ? elevationM / metresPerPixel : 0.0;

    vec4 worldPos = {{worldXY.x, worldXY.y, worldZ, 1.0}};
    vec4 clip;
    matrix::transformMat4(clip, worldPos, state.getProjectionMatrix());
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

    const float referenceW = computeReferenceClipW(parameters);
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
