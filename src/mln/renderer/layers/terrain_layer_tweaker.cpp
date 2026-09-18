#include <mln/renderer/layers/terrain_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/math/angles.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_orchestrator.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/shaders/terrain_layer_ubo.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/style/sky.hpp>
#include <mln/style/sky_impl.hpp>
#include <mln/util/color.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/mat4.hpp>
#include <mln/util/logging.hpp>

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace mln {

using namespace shaders;

void TerrainLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty() || !terrain || !orchestrator) {
        return;
    }

    auto& context = parameters.context;

#if defined(DEBUG)
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    // Get terrain properties; per the style spec, exaggeration is applied as-is
    // (default 1.0 renders true-scale elevation)
    const float exaggeration = terrain->getExaggeration();

    // Skirt depth (u_ele_delta): the shader drops the mesh's skirt vertices by
    // this many metres into a curtain that hides the cracks between neighbouring
    // tiles at different zoom levels. ~1/5 of the tile's world width at this zoom,
    // matching maplibre-gl-js Terrain.getSkirtLength().
    const auto zoom = std::max(parameters.state.getZoom(), 0.0);
    const float elevationOffset = static_cast<float>(util::M2PI * util::EARTH_RADIUS_M / std::pow(2.0, zoom) / 5.0);

    // DuckMaps fork only: maplibre-gl-js's own terrain ground fog uniforms (search the bundle
    // for `u_fog_ground_blend_opacity:`). `sky` is RenderOrchestrator::getSky() - std::nullopt
    // when the style carries no `sky` root property at all, in which case every value below
    // takes the web's own "no sky" default. Those defaults make the fragment shader's blend
    // `if` false (fog_ground_blend_opacity == 0), so a style with no sky renders identically to
    // before this task.
    const std::optional<Immutable<style::Sky::Impl>>& sky = orchestrator->getSky();
    Color fogColor = Color::white();
    Color horizonColor = Color::white();
    float fogGroundBlend = 1.0f;
    float fogGroundBlendOpacity = 0.0f;
    float horizonFogBlend = 1.0f;
    double fogNear = parameters.state.getFogNearZ();
    if (sky) {
        fogColor = (*sky)->fogColor;
        horizonColor = (*sky)->horizonColor;
        fogGroundBlend = (*sky)->fogGroundBlend;
        horizonFogBlend = (*sky)->horizonFogBlend;
        // Sky::calculateFogBlendOpacity ports the web's own pitch<60/pitch<70 ramp verbatim -
        // see its doc comment. It takes DEGREES, matching the web's `transform.pitch`;
        // TransformState::getPitch() is radians, hence the conversion here.
        const double pitchDegrees = util::rad2deg(parameters.state.getPitch());
        fogGroundBlendOpacity = style::Sky::calculateFogBlendOpacity(pitchDegrees);
        // Round 10 (build 20), the Haze dial: strength s = level / 5. The style's values are s = 1.
        // fog-ground-blend is where the ground stops being fogged (1 = nowhere): 1 - (1 - base) * s,
        // so 0 is no fog and 10 pulls the haze to the camera; horizon-fog-blend scales with s.
        const double s = std::clamp(orchestrator->getHazeLevel() / 5.0, 0.0, 2.0);
        fogGroundBlend = static_cast<float>(std::clamp(1.0 - (1.0 - fogGroundBlend) * s, 0.0, 1.0));
        horizonFogBlend = static_cast<float>(std::clamp(horizonFogBlend * s, 0.0, 1.0));
        if (s <= 0.0) {
            fogGroundBlendOpacity = 0.0f;
        }
        // Above 5 the fog's near plane itself comes forward, from the web's camera-to-sea-level
        // distance (measured at Gavarnie z12.4 pitch 80: 1.6 times the centre distance, so the
        // web's fog only touches the horizon band) to a quarter of the centre distance at 10.
        if (s > 1.0) {
            const double c2c = parameters.state.getCameraToCenterDistance();
            fogNear = fogNear + (c2c * 0.25 - fogNear) * (s - 1.0);
        }
    }


    // DuckMaps fork only: the base (tile-independent) fog matrix for this frame -
    // TransformState::getFogMatrix, multiplied per-tile below exactly as
    // `parameters.matrixForTile` does for the main projection matrix. See that method's own
    // comment for why it must stay in the OpenGL clip convention and never receive the
    // Metal/Vulkan/WebGPU z-remap applied to `matrix` below.
    mat4 fogMatrixBase;
    parameters.state.getFogMatrix(fogMatrixBase);

    // Populate layer-level UBO with terrain properties
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    const TerrainEvaluatedPropsUBO propsUBO = {.unpack = terrain->getDEMUnpackVector(),
                                               .exaggeration = exaggeration,
                                               .elevation_offset = elevationOffset,
                                               .fog_near = static_cast<float>(fogNear),
                                               .fog_far = static_cast<float>(parameters.state.getFarZ()),
                                               .fog_color = fogColor,
                                               .horizon_color = horizonColor,
                                               .fog_ground_blend = fogGroundBlend,
                                               .fog_ground_blend_opacity = fogGroundBlendOpacity,
                                               .horizon_fog_blend = horizonFogBlend,
                                               .pad3 = 0.0f};
    layerUniforms.createOrUpdate(idTerrainEvaluatedPropsUBO, &propsUBO, context);

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    std::vector<TerrainDrawableUBO> drawableUBOVector(layerGroup.getDrawableCount());
#endif

    // Visit each drawable to populate per-drawable UBOs
    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID()) {
            return;
        }

        const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();

        // Calculate transformation matrix for this terrain tile
        // This uses the same matrix calculation as other layers. The metres->world
        // pixels elevation scale (pixelsPerMeter) is baked into the projection matrix
        // in TransformState::getProjMatrix, so it applies here and to the elevated
        // symbol / circle layers consistently, matching maplibre-gl-js.
        mat4 matrix = parameters.matrixForTile(tileID);

#if !MLN_RENDER_BACKEND_OPENGL
        // matrixForTile builds a GL-convention projection (clip z in [-1, 1]); Vulkan,
        // Metal and WebGPU clip to [0, 1]. Remap clip z from [-1, 1] to [0, 1] the usual
        // way, z' = (z + w) / 2, so terrain that projects into the near half of the GL
        // clip volume (z < 0) is not clipped away on those backends. Matches
        // LayerTweaker::getTileMatrix and clipMatrixForTile, which remap the draped / RTT
        // matrices for the same reason. Monotonic, so skirt-vs-surface depth ordering and
        // the packed depth texture used for symbol occlusion are preserved; GL unchanged.
        matrix[2] = 0.5 * (matrix[2] + matrix[3]);
        matrix[6] = 0.5 * (matrix[6] + matrix[7]);
        matrix[10] = 0.5 * (matrix[10] + matrix[11]);
        matrix[14] = 0.5 * (matrix[14] + matrix[15]);
#endif

        // DuckMaps fork only: this tile's own fog matrix, `fogMatrixBase` (the OpenGL-clip-
        // convention, untouched-by-the-remap-above base fog matrix) with the tile's local
        // matrix multiplied in - exactly the pattern `parameters.matrixForTile` uses for the
        // main projection matrix (mat4 tileMatrix; state.matrixFor(tileMatrix, tileID);
        // matrix::multiply(out, projMatrixBase, tileMatrix);). Deliberately NOT run through the
        // `#if !MLN_RENDER_BACKEND_OPENGL` remap above - see TransformState::getFogMatrix's own
        // comment for why.
        mat4 tileMatrix;
        parameters.state.matrixFor(tileMatrix, tileID);
        mat4 fogMatrix;
        matrix::multiply(fogMatrix, fogMatrixBase, tileMatrix);

#if !MLN_UBO_CONSOLIDATION
        auto& drawableUniforms = drawable.mutableUniformBuffers();
#endif

#if MLN_UBO_CONSOLIDATION
        drawableUBOVector[i] = {
#else
        const TerrainDrawableUBO drawableUBO = {
#endif
            .matrix = util::cast<float>(matrix),
            .dem_coords = terrain->getDrawableDemCoords(*drawable.getTileID()),
            .fog_matrix = util::cast<float>(fogMatrix)
        };

#if !MLN_UBO_CONSOLIDATION
        drawableUniforms.createOrUpdate(idTerrainDrawableUBO, &drawableUBO, context);
#endif

#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(i++);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    const size_t drawableUBOVectorSize = sizeof(TerrainDrawableUBO) * drawableUBOVector.size();
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < drawableUBOVectorSize) {
        drawableUniformBuffer = context.createUniformBuffer(
            drawableUBOVector.data(), drawableUBOVectorSize, false, true);
    } else {
        drawableUniformBuffer->update(drawableUBOVector.data(), drawableUBOVectorSize);
    }

    layerUniforms.set(idTerrainDrawableUBO, drawableUniformBuffer);
#endif
}

} // namespace mln
