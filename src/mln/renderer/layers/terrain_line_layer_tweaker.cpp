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

// Task 2.2b, occlusion: the frame's own conversion from "metres of terrain depth" to "NDC z per
// unit clip-w-squared" - the single constant occlusion_far the fragment shader divides by its
// own fragment's w^2 (min(occlusion_eps, occlusion_far / w^2), matching the web's identical
// min-of-two-margins form, routes3d.js:473, ground-web-engine.md section 1). This is a literal
// port of the web's occlusionFar(m) (routes3d.js:1381-1394), operating on WORLD mercator-PIXEL
// coordinates at this frame's own scale (the same space computeReferenceClipW's worldXY/worldZ
// above already uses) rather than gl-js's normalised [0,1] mercator space - the two derivations
// are the same projection-space calculus, just re-expressed in this engine's own coordinate
// convention.
//
// Derivation (units at each step):
//   - For a perspective projection, clip z and clip w are both AFFINE in world position, so
//     moving a real-world distance t (metres) along the view axis unit vector u from a reference
//     point (world position p0, clip z0/w0 there): z_clip(t) = z0 + a*t, w_clip(t) = w0 + b*t,
//     where a = dz_clip/dmetre and b = dw_clip/dmetre are FRAME CONSTANTS along that one
//     direction (units: clip units per metre).
//   - z_ndc(t) = z_clip(t) / w_clip(t). Its derivative wrt t (units: NDC-z per metre) is
//     [a*w_clip(t) - z_clip(t)*b] / w_clip(t)^2 - and substituting z_clip(t)=z0+a*t,
//     w_clip(t)=w0+b*t, the a*t and b*t cross terms cancel algebraically, leaving
//     [a*w0 - z0*b] / w_clip(t)^2: the NUMERATOR is the same real number at every point along
//     the whole view axis, not just at the reference point - that is the "k" computed once below
//     and stored as occlusion_far / OCCLUSION_EPS_M_DEFAULT, and dividing it by any OTHER
//     fragment's own w^2 in the shader reproduces d(z_ndc)/dmetre AT THAT FRAGMENT for free.
//   - a and b are read directly off state.getProjectionMatrix()'s own rows (units: clip units
//     per world-mercator-pixel), the same convention TransformState::matrixFor / gl-js's
//     mainMatrix both use (matrix::transformMat4's out[3] = m[3]*x+m[7]*y+m[11]*z+m[15]*w is
//     exactly gl-js's mat4 layout too), scaled from "per world-pixel" to "per metre" by
//     1/metresPerPixel (the same metresPerPixel computeReferenceClipW/execute() already compute
//     from Projection::getMetersPerPixelAtLatitude - reused here rather than recomputed).
//   - The view axis direction u is taken as the gradient of clip w wrt world position,
//     normalised - the direction w changes fastest, i.e. the camera's own view axis, with no
//     assumption about near/far planes (matching the web's own comment, routes3d.js:1370-1380).
//   - z0/w0 are evaluated at the map centre at world Z = 0 (sea level, NOT the centre's own
//     terrain elevation - occlusion_far is a pure projection-geometry constant, unlike
//     reference_w above which deliberately does sample the centre's real elevation for its own,
//     unrelated purpose).
//   - occlusion_far = k * OCCLUSION_EPS_M_DEFAULT (metres): the final unit is "NDC-z per
//     clip-w-squared", i.e. exactly what occlusion_far / w^2 must be in the shader to yield an
//     NDC-z margin equivalent to OCCLUSION_EPS_M_DEFAULT real metres of terrain depth at that
//     fragment's own distance from the camera.
constexpr double OCCLUSION_EPS_M_DEFAULT = 60.0; // metres of terrain depth; web's OCCLUSION_EPS_M_DEFAULT (routes3d.js:173)

float computeOcclusionFar(const PaintParameters& parameters, double metresPerPixel) {
    if (!(metresPerPixel > 0.0)) {
        return 0.0f;
    }
    const auto& state = parameters.state;
    const mat4& m = state.getProjectionMatrix();
    const double mz = 1.0 / metresPerPixel; // world-mercator-pixel units per metre

    const double gx = m[3], gy = m[7], gz = m[11];
    const double g = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (!(g > 0.0)) {
        return 0.0f;
    }
    const double ux = gx / g, uy = gy / g, uz = gz / g;
    const double a = (m[2] * ux + m[6] * uy + m[10] * uz) * mz; // dz_clip/dmetre along the view axis
    const double b = g * mz;                                   // dw_clip/dmetre along the view axis

    const LatLng center = state.getLatLng();
    const Point<double> worldXY = Projection::project(center, state.getScale());
    const double zc0 = m[2] * worldXY.x + m[6] * worldXY.y + m[14]; // clip z at the centre, world Z = 0
    const double w0 = m[3] * worldXY.x + m[7] * worldXY.y + m[15];  // clip w at the centre, world Z = 0
    const double k = std::abs(a * w0 - zc0 * b);
    return static_cast<float>(k * OCCLUSION_EPS_M_DEFAULT);
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

    // Task 2.2b, occlusion + distance fade: frame constants, computed once and duplicated into
    // every drawable's UBO below (the same pattern reference_w above already uses) rather than
    // recomputed per tile.
    const auto& state = parameters.state;
    const LatLng frameCenter = state.getLatLng();
    const double metresPerPixel = Projection::getMetersPerPixelAtLatitude(frameCenter.latitude(), state.getZoom());
    const float metresPerPixelF = static_cast<float>(metresPerPixel);
    const float occlusionFar = computeOcclusionFar(parameters, metresPerPixel);
    const Point<double> centerWorldXY = Projection::project(frameCenter, state.getScale());

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

        // 2.2b DECISION: keep depth TEST off while terrain is on (unchanged from the first cut),
        // now for a reason rather than as a deferred debt. The terrain surface writes depth at
        // the resolution of its own coarse (128x128) triangulated mesh; this ribbon is elevated
        // per vertex from the DEM directly (get_elevation, full DEM resolution), so at any given
        // screen pixel the ribbon's own sampled height and the mesh's coarsely-interpolated
        // triangle height can differ by a small amount even where the ribbon is genuinely lying
        // on the surface it follows - depth-testing against the mesh's own buffer would
        // self-occlude/flicker the ribbon against its own host surface. Hiding the ribbon behind
        // real hills in front of it is instead the occlusion test's job below, against the
        // DEDICATED depth texture and WITH the eps margin precisely because that margin is
        // designed to tolerate this kind of surface-vs-mesh disagreement without producing false
        // occlusion. Depth stays on without terrain (no terrain surface underneath to conflict
        // with either way).
        drawable.setEnableDepth(!terrainEnabled);

        // Bind the terrain occlusion depth texture exactly like idSymbolDepthTexture
        // (symbol_layer_tweaker.cpp:182): always a valid sampler (falls back to the far-plane
        // placeholder when terrain is off or has not rendered a depth pass yet), so the shader's
        // sampler declaration is always satisfied even though drawable.depth_enabled gates
        // whether it is ever actually sampled.
        if (parameters.terrain) {
            drawable.setTexture(parameters.terrain->getDepthTexture(context), idTerrainLineDepthTexture);
        } else {
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainLineDepthTexture);
        }

        const auto dashPeriod = computeDashPeriodExtent(dasharray, widthPxAtAnchorZoom, tileID.canonical);

        // Task 2.2b, distance fade: this tile's own placement in world-pixel space, relative to
        // the map centre - kept as a delta (see TerrainLineDrawableUBO::origin_offset_x's
        // comment for why), computed the same way TransformState::matrixFor derives a tile's own
        // world-pixel origin and scale (src/mln/map/transform_state.cpp:115-124), reusing that
        // tile's already-computed matrix scale would require unpacking it back out, so it is
        // recomputed directly here from the same inputs (tileID, state.getScale()).
        const uint64_t tileScale = uint64_t(1) << tileID.canonical.z;
        const double worldPxPerTile = Projection::worldSize(state.getScale()) / static_cast<double>(tileScale);
        const double tileOriginX = (static_cast<double>(tileID.canonical.x) +
                                    static_cast<double>(tileID.wrap) * static_cast<double>(tileScale)) *
                                   worldPxPerTile;
        const double tileOriginY = static_cast<double>(tileID.canonical.y) * worldPxPerTile;
        const float worldPxPerExtent = static_cast<float>(worldPxPerTile / util::EXTENT);
        const float originOffsetX = static_cast<float>(tileOriginX - centerWorldXY.x);
        const float originOffsetY = static_cast<float>(tileOriginY - centerWorldXY.y);

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
            .occlusion_far = occlusionFar,
            .metres_per_pixel = metresPerPixelF,
            .origin_offset_x = originOffsetX,
            .origin_offset_y = originOffsetY,
            .world_px_per_extent = worldPxPerExtent,
            .depth_enabled = terrainEnabled ? 1.0f : 0.0f,
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
