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

// Task 2.2: routes3d.js's OCCLUSION_EPS_DEFAULT (:133) and OCCLUSION_EPS_M_DEFAULT (:173) - see
// occlusionFarNDC()'s comment below for the argument (routes3d.js:111-175) for why both exist:
// a fixed NDC-z tolerance alone buys unbounded metres of terrain at distance, so the shader takes
// the smaller of this constant and a metres-based margin converted to NDC z at each fragment.
constexpr float OCCLUSION_EPS_DEFAULT = 0.002f;
constexpr float OCCLUSION_EPS_M_DEFAULT = 60.0f;

// Ported from the web engine's occlusionFar(m) (routes3d.js:1381-1395): the frame's own
// conversion from "metres of terrain depth" to the single NDC-z-per-(1/w^2) constant the
// fragment shader divides by v_center.w^2 (terrain_line.hpp's fragmentMain). Computed ONCE PER
// FRAME (it depends only on the projection matrix and the map centre, not on any tile), unlike
// dash_period/dash_on below which are per-tile.
//
// For a perspective projection, clip z and clip w are both affine in position and z_ndc depends
// on w alone: z_ndc = A + B/w, so dz_ndc/dm along the view axis is (a*w - zc*b) / w^2, where a and
// b are clip z's and clip w's own rate per metre along that axis - the same numerator (k below)
// everywhere in the frame, which is exactly the constant wanted. It is read straight off the
// frame's own projection matrix (mln::matrix::transformMat4's column-major convention - verified
// against mat4.cpp's own out[3] = m[3]*x + m[7]*y + m[11]*z + m[15]*w formula, which is exactly
// routes3d.js's `w = m[3]*p[0] + m[7]*p[1] + m[11]*z + m[15]`), evaluated at the map centre at sea
// level, with no assumption about near/far planes: the view axis is the direction clip w grows
// fastest in, which is the matrix's own w row (m[3], m[7], m[11]).
//
// The four inputs, and where this engine gets each one (routes3d.js reads all four off its own
// mainMatrix/state in normalised [0,1] mercator world units; this port uses the SAME world-PIXEL
// mercator convention computeReferenceClipW() above already uses with the SAME projection matrix,
// which differs from the web's only by a constant scale baked consistently into both the matrix
// and the positions, so the derivation carries over unchanged):
//   1. the projection matrix m       - parameters.transformParams.projMatrix (paint_parameters.hpp)
//   2. the map centre in mercator    - Projection::project(state.getLatLng(), state.getScale())
//                                      (util/projection.hpp), at z = 0 (sea level, matching the
//                                      web's own zc0/w0 - see routes3d.js:1389-1392)
//   3. metres-to-mercator factor mz  - 1 / Projection::getMetersPerPixelAtLatitude(lat, zoom)
//                                      (util/projection.hpp), i.e. world-pixel units per metre -
//                                      the exact reciprocal of computeReferenceClipW's own
//                                      metresPerPixel, at the SAME latitude/zoom
//   4. the centre's clip z and w     - zc0 = m[2]*pc.x + m[6]*pc.y + m[14] (no z term - sea level)
//                                      w0  = m[3]*pc.x + m[7]*pc.y + m[15]
float occlusionFarNDC(const PaintParameters& parameters, float occlusionEpsM) {
    const mat4& m = parameters.transformParams.projMatrix;
    const double gx = m[3];
    const double gy = m[7];
    const double gz = m[11];
    const double g = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (!(g > 0.0)) {
        return 0.0f;
    }
    const auto& state = parameters.state;
    const LatLng center = state.getLatLng();
    const double metresPerPixel = Projection::getMetersPerPixelAtLatitude(center.latitude(), state.getZoom());
    const double mz = metresPerPixel > 0.0 ? 1.0 / metresPerPixel : 0.0; // world-pixel units per metre
    const double ux = gx / g;
    const double uy = gy / g;
    const double uz = gz / g;
    const double a = (m[2] * ux + m[6] * uy + m[10] * uz) * mz;
    const double b = g * mz; // clip w per metre along the view axis
    const Point<double> pc = Projection::project(center, state.getScale());
    const double zc0 = m[2] * pc.x + m[6] * pc.y + m[14];
    const double w0 = m[3] * pc.x + m[7] * pc.y + m[15];
    const double k = std::abs(a * w0 - zc0 * b);
    return static_cast<float>(k * occlusionEpsM);
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

    // Task 2.2: the depth texture, the occlusion-far constant and depth_enabled are all
    // FRAME-level facts (the depth texture is one shared render target, the projection matrix and
    // terrain-on/off do not vary per tile) - computed once here, not inside the per-drawable
    // lambda below, and copied into every tile's TilePropsUBO entry the same way reference_w
    // above is. terrainEnabled itself is also frame-level (it does not depend on tileID) even
    // though the DEM-lookup below (which DOES vary per tile) still uses it per drawable.
    const bool terrainEnabled = parameters.terrain && parameters.terrain->isEnabled();
    const std::shared_ptr<gfx::Texture2D> depthTexture = parameters.terrain
                                                             ? parameters.terrain->getDepthTexture(context)
                                                             : nullptr;
    // RenderTerrain::getDepthTexture() falls back to a 1x1 far-plane placeholder
    // (render_terrain.cpp) whenever the depth pass has not produced a real texture yet (terrain
    // just turned on this frame, or terrain is off) - texture size is the only public signal that
    // tells the two apart, so depth_enabled follows it rather than terrainEnabled alone.
    const bool hasRealDepthTexture = depthTexture && depthTexture->getSize() != Size{1, 1};
    const float depthEnabled = (terrainEnabled && hasRealDepthTexture) ? 1.0f : 0.0f;
    const Size depthSize = hasRealDepthTexture ? depthTexture->getSize() : Size{1, 1};
    const std::array<float, 2> depthTexel = {1.0f / static_cast<float>(depthSize.width),
                                             1.0f / static_cast<float>(depthSize.height)};
    const float occlusionFar = occlusionFarNDC(parameters, OCCLUSION_EPS_M_DEFAULT);

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
        // symbol_layer_tweaker.cpp:170-234's identical pattern). terrainEnabled itself is
        // computed once per frame above; only the per-tile DEM lookup happens here.
        std::optional<RenderTerrain::TerrainData> terrainData;
        if (terrainEnabled) {
            terrainData = parameters.terrain->getTerrainData(tileID);
        }
        if (parameters.terrain) {
            drawable.setTexture(
                terrainData ? terrainData->demTexture : parameters.terrain->getPlaceholderDEMTexture(context),
                idTerrainLineDEMTexture);
            // Packed terrain depth for the occlusion test below (same pattern as
            // symbol_layer_tweaker.cpp:182; the texture itself is fetched once per frame above).
            drawable.setTexture(depthTexture, idTerrainLineDepthTexture);
        } else {
            // Keep the declared DEM/depth samplers bound for Metal API validation (never
            // sampled): a missing sampler binding trips it even when depth_enabled is 0.
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainLineDEMTexture);
            drawable.setTexture(context.getPlaceholderTexture2D(), idTerrainLineDepthTexture);
        }

        // The terrain surface writes depth, at the resolution of its own coarse (128x128)
        // triangulated mesh; this ribbon is elevated onto the terrain from the DEM directly, per
        // vertex, and would self-occlude against the mesh's own approximation of the same surface
        // if depth-tested against it. Depth stays on without terrain (there being no terrain
        // surface underneath to conflict with). Occlusion against the REAL terrain surface (not
        // this coarse mesh) is the depth-TEXTURE test in the fragment shader below/in
        // terrain_line.hpp, not this depth-buffer test - matching the web engine, which also uses
        // the depth texture and no depth test (docs/plans/2026-09-11-engine-layer-plumbing.md A2
        // point 4, same reasoning as circle/symbol).
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
        // Fragment-only tile props (dash_period/dash_on plus the terrain occlusion inputs) - see
        // TerrainLineDrawableUBO's comment in terrain_line_layer_ubo.hpp for why the fragment
        // stage cannot read the struct above. occlusion_eps/occlusion_far/depth_texel/
        // depth_enabled are all frame-level (computed once above) and simply copied per tile.
#if MLN_UBO_CONSOLIDATION
        tilePropsUBOVector[i] = {
#else
        const TerrainLineTilePropsUBO tilePropsUBO = {
#endif
            .dash_period = dashPeriod.periodExtent,
            .dash_on = dashPeriod.on,
            .occlusion_eps = OCCLUSION_EPS_DEFAULT,
            .occlusion_far = occlusionFar,
            .depth_texel = depthTexel,
            .depth_enabled = depthEnabled,
            .pad1 = 0,
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
