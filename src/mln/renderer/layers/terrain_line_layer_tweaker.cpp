#include <mln/renderer/layers/terrain_line_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/terrain_line_layer_ubo.hpp>
#include <mln/style/layers/terrain_line_layer_properties.hpp>
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

// The web engine's referenceClipW() (routes3d.js:1223-1231): the clip-space w at the ground
// under the map centre, at the centre's own sampled elevation. Scaling the ribbon's half-width
// by (this / this vertex's own w) is what keeps its on-screen width constant at the map centre
// while it grows or shrinks with depth away from it, the way a real-world object would as the
// camera tilts.
//
// TransformState::getProjectionMatrix() operates on WORLD mercator-pixel coordinates (the same
// space TransformState::matrixFor() converts tile-local EXTENT coordinates into -
// TransformState::getCameraToTileDistance() is the same pattern for one tile's centre instead of
// the map centre). World Z is expressed in that same pixel unit, converted from metres exactly
// as TransformState::setCenterAltitude() does (metres / metresPerPixelAtLatitude).
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
    return static_cast<float>(clip[3]);
}

// Converts the evaluated terrain-line-dasharray + terrain-line-width paint properties into a
// dash period expressed in THIS TILE's EXTENT-unit distance space, matching a_dist's own units
// (see TerrainLineLayout, which accumulates distance directly in EXTENT-unit coordinate deltas).
//
// The web's groundDashPeriod (routes3d.js:1174-1187) computes a period in *normalised mercator
// world units* - a fraction of the whole world circumference, independent of zoom - because its
// own a_dist is mercator. Ours is EXTENT units for one specific tile at zoom tileZ, and a tile at
// zoom z spans 1/2^z of the normalised world, so: period_extent = period_mercator * 2^tileZ *
// EXTENT. Getting this factor right matters: it is invisible at the zoom the ribbon happened to
// be authored/tested at and wrong everywhere else.
//
// One deliberate simplification versus the web: groundDashPeriod evaluates width at a *fixed*
// anchor zoom (DASH_ANCHOR_ZOOM = 15) so the dash pattern never subdivides or rescales as the
// camera zooms. We evaluate width at the CURRENT frame zoom instead (the same width already
// evaluated for u_half_px), which is simpler but means the dash pattern is not zoom-stable the
// way the web's is. Flagged here rather than silently matched, since it is a real behavioural
// difference, not just an implementation detail.
struct DashPeriod {
    float periodExtent = 0.0f;
    float on = 1.0f;
};

DashPeriod computeDashPeriodExtent(const std::array<float, 2>& dasharray,
                                   float widthPx,
                                   const CanonicalTileID& tileID) {
    const float units = dasharray[0] + dasharray[1];
    if (!(units > 0.0f)) {
        return {};
    }
    const float on = dasharray[0] / units;
    // Normalised-mercator-world-unit period at the current zoom: `units` (in line-width
    // multiples) * widthPx gives a dash length in device pixels; dividing by the world's total
    // pixel span at this zoom (util::tileSize_D * 2^zoom, the same WORLD_PX * 2^zoom the web
    // uses) turns that into a fraction of the whole world - independent of which tile it lands
    // in, until the next step converts it back into this one tile's own EXTENT units.
    const double worldPxAtZoom = util::tileSize_D * std::exp2(static_cast<double>(tileID.z));
    const double periodMercator = worldPxAtZoom > 0.0 ? (static_cast<double>(units) * widthPx) / worldPxAtZoom : 0.0;
    const double periodExtent = periodMercator * std::exp2(static_cast<double>(tileID.z)) * util::EXTENT;
    return {static_cast<float>(periodExtent), on};
}

} // namespace

void TerrainLineLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    auto& context = parameters.context;
    const auto& evaluated = static_cast<const TerrainLineLayerProperties&>(*evaluatedProperties).evaluated;

#ifndef NDEBUG
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    const float referenceW = computeReferenceClipW(parameters);
    // pixelRatio isn't in our own UBO (see u_half_px's derivation): the tweaker bakes the
    // device-pixel half-width straight from the evaluated CSS-pixel width, once per frame.
    const float widthPx = evaluated.get<TerrainLineWidth>();
    const float halfPx = widthPx * parameters.pixelRatio / 2.0f;
    const auto dasharray = evaluated.get<TerrainLineDasharray>();

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const TerrainLineEvaluatedPropsUBO evaluatedPropsUBO = {
            .color = evaluated.get<TerrainLineColor>(),
            .opacity = evaluated.get<TerrainLineOpacity>(),
            .half_px = halfPx,
            .edge_px = evaluated.get<TerrainLineBlur>(),
            .rail_offset = evaluated.get<TerrainLineOffset>(),
            .depth_bias = 0.00002f, // DEPTH_BIAS, routes3d.js:100 - a constant, not a property
            .ghost_opacity = evaluated.get<TerrainLineGhostOpacity>(),
            .fade = evaluated.get<TerrainLineFade>(),
            .fade_distance = evaluated.get<TerrainLineFadeDistance>(),
            .pad1 = 0,
            .pad2 = 0,
            .pad3 = 0,
            .pad4 = 0};
        context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &evaluatedPropsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idTerrainLineEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    std::vector<TerrainLineDrawableUBO> drawableUBOVector(layerGroup.getDrawableCount());
#endif

    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }
        const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();

        constexpr std::array<float, 2> noTranslation{0.f, 0.f};
        const auto matrix = getTileMatrix(tileID,
                                          parameters,
                                          noTranslation,
                                          style::TranslateAnchorType::Viewport,
                                          /*nearClipped=*/false,
                                          /*inViewportPixelUnits=*/false,
                                          drawable,
                                          /*aligned=*/false,
                                          /*renderToTerrain=*/false);

        // Bind the covering DEM tile so the vertex shader can elevate both ends of every
        // sub-segment onto the terrain (RenderTerrain::getTerrainData, see
        // symbol_layer_tweaker.cpp:170-234's identical pattern).
        const bool terrainEnabled = parameters.terrain && parameters.terrain->isEnabled();
        std::optional<RenderTerrain::TerrainData> terrainData;
        if (terrainEnabled) {
            terrainData = parameters.terrain->getTerrainData(tileID);
        }
        if (parameters.terrain) {
            drawable.setTexture(
                terrainData ? terrainData->demTexture : parameters.terrain->getPlaceholderDEMTexture(context),
                idTerrainLineDEMTexture);
        } else {
            // Keep the declared DEM sampler bound for Metal API validation (never sampled).
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainLineDEMTexture);
        }

        // The terrain surface writes depth, at the resolution of its own coarse (128x128)
        // triangulated mesh; this ribbon is elevated onto the terrain from the DEM directly,
        // per vertex, and would self-occlude against the mesh's own approximation of the same
        // surface if depth-tested against it. Depth stays on without terrain (there being no
        // terrain surface underneath to conflict with). No depth-texture occlusion test in this
        // first cut (2.2b) - the ribbon simply draws on top of the terrain while one is active,
        // exactly like circle (docs/plans/2026-09-11-engine-layer-plumbing.md A2 point 4).
        drawable.setEnableDepth(!terrainEnabled);

        const auto dashPeriod = computeDashPeriodExtent(dasharray, widthPx, tileID.canonical);

#if MLN_UBO_CONSOLIDATION
        drawableUBOVector[i] = {
#else
        const TerrainLineDrawableUBO drawableUBO = {
#endif
            .matrix = util::cast<float>(matrix),
            .dem_coords = terrainData ? terrainData->demCoords : std::array<float, 4>{{0, 0, 0, 0}},
            .dem_unpack = parameters.terrain ? parameters.terrain->getDEMUnpackVector()
                                             : std::array<float, 4>{{0, 0, 0, 0}},
            .dem_dim = terrainData ? terrainData->demDim : 0.0f,
            .dem_exaggeration = parameters.terrain ? parameters.terrain->getExaggeration() : 0.0f,
            .dem_enabled = terrainData ? 1.0f : 0.0f,
            .reference_w = referenceW,
            .dash_period = dashPeriod.periodExtent,
            .dash_on = dashPeriod.on,
            .pad1 = 0,
            .pad2 = 0,
        };
#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(i++);
#else
        auto& drawableUniforms = drawable.mutableUniformBuffers();
        drawableUniforms.createOrUpdate(idTerrainLineDrawableUBO, &drawableUBO, context);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    const size_t drawableUBOVectorSize = sizeof(TerrainLineDrawableUBO) * drawableUBOVector.size();
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < drawableUBOVectorSize) {
        drawableUniformBuffer = context.createUniformBuffer(
            drawableUBOVector.data(), drawableUBOVectorSize, false, true);
    } else {
        drawableUniformBuffer->update(drawableUBOVector.data(), drawableUBOVectorSize);
    }
    layerUniforms.set(idTerrainLineDrawableUBO, drawableUniformBuffer);
#endif
}

} // namespace mln
