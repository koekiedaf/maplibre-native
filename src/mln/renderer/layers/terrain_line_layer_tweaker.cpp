#include <mln/renderer/layers/terrain_line_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/property_evaluation_parameters.hpp>
#include <mln/renderer/property_evaluator.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/terrain_line_layer_ubo.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>
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

// The web's DASH_ANCHOR_ZOOM (routes3d.js:1171, groundDashPeriod at :1174-1187): the dash
// pattern's period is computed from the line width evaluated at this FIXED zoom, never at the
// zoom the camera happens to be at, so the dash never re-subdivides or rescales while zooming.
constexpr double DASH_ANCHOR_ZOOM = 15.0;

// Evaluates terrain-line-width at the fixed DASH_ANCHOR_ZOOM rather than the current frame zoom,
// for the dash calculation only (the width used for u_half_px stays at the frame zoom - see
// halfPx in execute()). This is the same two-step PropertyValue evaluation the style engine
// itself uses to turn an unevaluated property into a zoom-baked one (compare
// RenderTerrainLineLayer::evaluate(), which does the same thing at the frame zoom via
// unevaluated.evaluate(...)): PropertyEvaluator<float> replays terrain-line-width's raw
// PropertyValue (from the layer's own Impl, not the already-frame-baked `evaluated`) against an
// arbitrary PropertyEvaluationParameters zoom.
float evaluateWidthAtAnchorZoom(const TerrainLineLayerProperties& properties) {
    const PropertyEvaluationParameters anchorParameters(static_cast<float>(DASH_ANCHOR_ZOOM));
    const PropertyEvaluator<float> evaluator(anchorParameters, TerrainLineWidth::defaultValue());
    return properties.layerImpl().paint.get<TerrainLineWidth>().value.evaluate(evaluator);
}

// Converts the terrain-line-dasharray + terrain-line-width paint properties into a dash period
// expressed in THIS TILE's EXTENT-unit distance space, matching a_dist's own units (see
// TerrainLineLayout, which accumulates distance directly in EXTENT-unit coordinate deltas).
//
// The web's groundDashPeriod (routes3d.js:1174-1187) computes a period in *normalised mercator
// world units* - a fraction of the whole world circumference, independent of zoom - because its
// own a_dist is mercator, and it anchors the width lookup at the fixed DASH_ANCHOR_ZOOM (15)
// rather than the live camera zoom so the pattern never re-subdivides while zooming. Ours is
// EXTENT units for one specific tile at zoom tileZ, and a tile at zoom z spans 1/2^z of the
// normalised world, so a second, DIFFERENT exponent is needed to place the period back into that
// tile's own EXTENT space. The two exponents must not be collapsed into one - they answer two
// unrelated questions ("how wide is the world at the anchor zoom, in pixels" vs. "how much of the
// normalised world does this tile's own zoom cover"):
//
//   widthPxAtZoom15 = terrain-line-width evaluated at the fixed anchor zoom 15 (not tileID.z,
//                     not the frame zoom) - see evaluateWidthAtAnchorZoom() above.
//   period_mercator = (dasharray[0] + dasharray[1]) * widthPxAtZoom15 / (tileSize * 2^15)
//                     -- the ANCHOR zoom's world pixel span (util::tileSize_D * 2^15, the web's
//                     WORLD_PX * 2^DASH_ANCHOR_ZOOM) turns the dash length in device pixels into
//                     a fraction of the whole normalised mercator world, fixed regardless of the
//                     tile or the live camera zoom.
//   period_extent   = period_mercator * 2^tileZ * EXTENT
//                     -- tileZ is THIS TILE's own zoom (tileID.z, varies per tile/frame): a tile
//                     at zoom z spans 1/2^z of the normalised world, so multiplying the
//                     world-fraction period by 2^tileZ * EXTENT converts it into this tile's own
//                     EXTENT-unit distance space, matching a_dist.
//
// Getting the anchor-vs-tile distinction right matters: collapsing the two exponents into one
// (using 2^tileZ for both, as an earlier version of this function did) makes the pattern
// invisible at whichever zoom it happened to be authored/tested at and wrong everywhere else -
// the dash silently resubdivides or rescales as the camera zooms.
struct DashPeriod {
    float periodExtent = 0.0f;
    float on = 1.0f;
};

// terrain-line-dasharray is std::vector<float> (task 2.2b, matching line-dasharray's own
// evaluated type - see scripts/style-spec.mjs's comment on this property for why). Only the
// first two entries are meaningful here (on, off); a vector of length 0 or 1 has no "off" length
// to speak of and is treated the same as [0, 0] - no dash pattern, draw solid - rather than
// rejected, since an empty/short dasharray is a legitimate (if unusual) style value, not a style
// error, and the shader's own dash_period == 0 convention already means exactly that.
DashPeriod computeDashPeriodExtent(const std::vector<float>& dasharray,
                                   float widthPxAtAnchorZoom,
                                   const CanonicalTileID& tileID) {
    if (dasharray.size() < 2) {
        return {};
    }
    const float on0 = dasharray[0];
    const float off0 = dasharray[1];
    const float units = on0 + off0;
    if (!(units > 0.0f)) {
        return {};
    }
    const float on = on0 / units;
    const double worldPxAtAnchorZoom = util::tileSize_D * std::exp2(DASH_ANCHOR_ZOOM);
    const double periodMercator = worldPxAtAnchorZoom > 0.0
                                       ? (static_cast<double>(units) * widthPxAtAnchorZoom) / worldPxAtAnchorZoom
                                       : 0.0;
    const double periodExtent = periodMercator * std::exp2(static_cast<double>(tileID.z)) * util::EXTENT;
    return {static_cast<float>(periodExtent), on};
}

} // namespace

void TerrainLineLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    auto& context = parameters.context;
    const auto& properties = static_cast<const TerrainLineLayerProperties&>(*evaluatedProperties);
    const auto& evaluated = properties.evaluated;

#ifndef NDEBUG
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    const float referenceW = computeReferenceClipW(parameters);
    // FAULT 1 FIX (task 2.2b): terrain-line-width/-blur/-offset are all in CSS pixels (points),
    // NOT device pixels, and so is the shader's own "pixel" space. The vertex shader converts
    // to/from that space via u_units_to_pixels, which is 1 / PaintParameters::pixelsToGLUnits,
    // and pixelsToGLUnits is 2 / state.getSize() (paint_parameters.cpp:96) - state.getSize() is
    // the map's LOGICAL size in points, not the framebuffer's device-pixel size. Multiplying the
    // evaluated CSS-pixel width by parameters.pixelRatio here (as this line used to) therefore
    // made every ribbon `pixelRatio` times too wide on screen - e.g. 3x on a dpr-3 device - since
    // it double-counted a device-pixel conversion the shader's own coordinate space never
    // performs. u_half_px must stay in the SAME CSS-pixel space u_units_to_pixels already
    // operates in, so no pixelRatio multiply belongs here at all.
    const float widthPx = evaluated.get<TerrainLineWidth>();
    const float halfPx = widthPx / 2.0f;
    const auto dasharray = evaluated.get<TerrainLineDasharray>();
    // The dash calculation anchors its width lookup at a fixed zoom (DASH_ANCHOR_ZOOM), never
    // at the current frame zoom above - see computeDashPeriodExtent()'s comment.
    const float widthPxAtAnchorZoom = evaluateWidthAtAnchorZoom(properties);

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
    std::vector<TerrainLineTilePropsUBO> tilePropsUBOVector(layerGroup.getDrawableCount());
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

        const auto dashPeriod = computeDashPeriodExtent(dasharray, widthPxAtAnchorZoom, tileID.canonical);

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
        };
        // Fragment-only tile props (dash_period/dash_on) - see TerrainLineDrawableUBO's comment
        // in terrain_line_layer_ubo.hpp for why the fragment stage cannot read the struct above.
#if MLN_UBO_CONSOLIDATION
        tilePropsUBOVector[i] = {
#else
        const TerrainLineTilePropsUBO tilePropsUBO = {
#endif
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
        drawableUniforms.createOrUpdate(idTerrainLineTilePropsUBO, &tilePropsUBO, context);
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

    const size_t tilePropsUBOVectorSize = sizeof(TerrainLineTilePropsUBO) * tilePropsUBOVector.size();
    if (!tilePropsUniformBuffer || tilePropsUniformBuffer->getSize() < tilePropsUBOVectorSize) {
        tilePropsUniformBuffer = context.createUniformBuffer(
            tilePropsUBOVector.data(), tilePropsUBOVectorSize, false, true);
    } else {
        tilePropsUniformBuffer->update(tilePropsUBOVector.data(), tilePropsUBOVectorSize);
    }
    layerUniforms.set(idTerrainLineTilePropsUBO, tilePropsUniformBuffer);
#endif
}

} // namespace mln
