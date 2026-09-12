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
#include <mln/util/tile_coordinate.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <tuple>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

// DuckMaps fork only, task C7: same env-var gate as
// terrain_contour_layer_tweaker.cpp's contourTraceEnabled() - see that file's comment.
bool lineTraceEnabled() {
    static const bool enabled = [] {
        const char* path = std::getenv("DUCKMAPS_ELEVATION_TRACE");
        return path && *path;
    }();
    return enabled;
}

// DuckMaps fork only, task C7: same contract as terrain_contour_layer_tweaker.cpp's
// contourUboEntriesSlot() - filled during execute()'s per-drawable loop, drained (and cleared)
// once per frame by Renderer::Impl::render via debugDrainLineDrawableUBOEntries.
std::vector<DebugDrawableUBOEntry>& lineUboEntriesSlot() {
    static std::vector<DebugDrawableUBOEntry> slot;
    return slot;
}

// DuckMaps fork only, task 2.4f (width-shortfall investigation, 12 Sept 2026): this layer's own
// half of terrain_contour_layer_tweaker.cpp's contourTraceSlot()/debugDrainContourReferenceTraceJSON
// - same contract, same DUCKMAPS_ELEVATION_TRACE gate, same "one frame's record, drained once by
// Renderer::Impl::render" shape. UNLIKE contour (one terrain-contour layer per style), a style
// carries MANY terrain-line layers (one execute() call each: topo-trail-path-sac-hiking,
// waterway-river, ...), so this slot is keyed by layer id rather than holding a single string -
// task C6's contract of one string covering the whole frame does not fit a layer type with more
// than one instance. Task 2.4e's own residual-gap note named exactly the numbers it is missing:
// referenceW alone (already visible via the drawable UBO hash) does not say whether it was
// evaluated against a tile at the SAME zoom as the ribbon it scales, nor what CSS-pixel width the
// style evaluated before any of the frame's own scaling was applied.
std::map<std::string, std::string>& lineTraceSlot() {
    static std::map<std::string, std::string> slot;
    return slot;
}

std::string lineTileIdDebugKey(const UnwrappedTileID& id) {
    std::ostringstream os;
    os << id.wrap << "/" << static_cast<int>(id.canonical.z) << "/" << id.canonical.x << "/" << id.canonical.y;
    return os.str();
}

// Task 2.2b: the map centre's own position expressed in tileID's EXTENT-unit local coordinate
// space - the same units a_pos/a_other carry, unclamped (the point need not fall inside this
// tile's own footprint). Identical in spirit to terrain_contour_layer_tweaker.cpp's own
// tileLocalPosition() (two tiles at the same zoom differ only by a translation in tile units, so
// this stays exact for whichever tile's own EXTENT space the caller uses), duplicated here rather
// than shared because that file's copy lives in its own anonymous namespace.
//
// This is also fade_ref: the web anchors its fade to its own near-far anchor (state.nearFarAnchor,
// held still through small pans - routes3d.js:930-934), a hand-over point between its 3D ribbon
// and a 2D flat drawing beyond it. This engine has no such hand-over (there is no 2D fallback
// drawing to hand over to), so fade_ref anchors to the live map centre instead - the same centre
// computeReferenceClipW below already reads via state.getLatLng().
Point<double> tileLocalPosition(const UnwrappedTileID& tileID, const LatLng& latLng) {
    const TileCoordinate coord = TileCoordinate::fromLatLng(static_cast<double>(tileID.canonical.z), latLng);
    const double tileScale = std::exp2(static_cast<double>(tileID.canonical.z));
    return {(coord.p.x - static_cast<double>(tileID.canonical.x) - tileID.wrap * tileScale) * util::EXTENT,
            (coord.p.y - static_cast<double>(tileID.canonical.y)) * util::EXTENT};
}

// The web engine's referenceClipW() (routes3d.js:1223-1231): the clip-space w at the ground
// under the map centre, at the centre's own sampled elevation. Scaling the ribbon's half-width
// by (this / this vertex's own w) is what keeps its on-screen width constant at the map centre
// while it grows or shrinks with depth away from it, the way a real-world object would as the
// camera tilts.
//
// FAULT 2 FIX (this task): this function used to rebuild its own world-pixel position via
// Projection::project(center, state.getScale()) and multiply it by state.getProjectionMatrix()
// directly, converting the centre's elevation from metres to world pixels BY HAND first
// (dividing by Projection::getMetersPerPixelAtLatitude). That divide was a second, spurious
// conversion: every matrix built off TransformState::getProjMatrix already scales whatever z it
// is given by pixelsPerMeter INTERNALLY, unconditionally - see Camera::getWorldToCamera's own
// comment ("Height value (z) of renderables is in meters. Scale z coordinate by pixelsPerMeter")
// and this shader's own vertex stage, which passes the DEM's decoded elevation straight through
// in metres with no conversion at all (get_elevation, terrain_line.vertex.glsl). Pre-dividing by
// metresPerPixel and then letting the matrix divide by it again (pixelsPerMeter = 1 /
// metresPerPixel) scaled the elevation's contribution to clip-w by a second, spurious factor of
// pixelsPerMeter - a factor that grows with zoom (pixelsPerMeter roughly doubles each zoom
// level), which is exactly why the reported error grew with zoom rather than staying constant:
// at the fixed reference LATITUDE the elevation term dominates the wrong way as pixelsPerMeter
// grows, dragging referenceW away from what every vertex's own w0 (computed correctly, via the
// per-tile matrix and raw-metres elevation) expects, until widthScale = referenceW / w0 collapses
// toward zero at zoom 16.5-17.
//
// This is exactly the fault terrain_contour_layer_tweaker.cpp's own computeReferenceClipW found
// and fixed first (task 2.4c, journal 2026-09-11) - that fix was never ported to this sibling
// file, which still carried the original, buggy pattern until now. Fixed the same way: drop the
// manual conversion entirely, pass elevationM straight through in metres, and evaluate it with
// the SAME kind of matrix a drawable's own geometry uses (a per-tile matrix, not
// state.getProjectionMatrix() alone) at the reference point expressed in that tile's own
// EXTENT-unit local coordinates (tileLocalPosition below) rather than at a separately-computed
// world-pixel position. Two same-zoom tiles' matrices differ only by a translation, so any tile
// gives the identical, exact w - execute() below picks one deterministically.
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

// Task 2.2b: metres per EXTENT unit at THIS TILE's own zoom (tileID.z - which, unlike the
// frame-level facts above, can differ per tile once overzoomed tiles are in play). One EXTENT
// unit is 1/util::EXTENT of one full tile width, and one tile always spans util::tileSize_D
// world-pixel units in the mercator projection at that tile's own zoom.
// Projection::getMetersPerPixelAtLatitude(lat, zoom) converts world-pixel units at a given zoom to
// metres; multiplying by (tileSize_D / EXTENT) finishes the conversion down to one EXTENT unit.
// This is the same "which zoom, which exponent" question computeDashPeriodExtent's own comment
// warns about, but simpler here: fade_ref/fade_k are computed directly in this tile's own EXTENT
// space, so only tileID.z is ever in play, never a second anchor zoom.
float metresPerExtentUnit(const LatLng& center, const CanonicalTileID& tileID) {
    const double metresPerPixel =
        Projection::getMetersPerPixelAtLatitude(center.latitude(), static_cast<double>(tileID.z));
    return static_cast<float>(metresPerPixel * (util::tileSize_D / static_cast<double>(util::EXTENT)));
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

// Task 2.2: routes3d.js's OCCLUSION_EPS_DEFAULT (:133) and OCCLUSION_EPS_M_DEFAULT (:173) - a
// fixed NDC-z tolerance alone buys unbounded metres of terrain at distance, so the shader takes
// the smaller of this constant and a metres-based margin converted to NDC z at each fragment.
// Declared in terrain_line_layer_tweaker.hpp (not anonymous-namespaced any more) so
// terrain_contour_layer_tweaker.cpp's own depth-texture occlusion test can share the identical
// tuned constants rather than holding a second copy of them.
//
// Ported from the web engine's occlusionFar(m) (routes3d.js:1381-1395): the frame's own
// conversion from "metres of terrain depth" to the single NDC-z-per-(1/w^2) constant the
// fragment shader divides by v_center.w^2 (terrain_line.hpp's fragmentMain). Computed ONCE PER
// FRAME (it depends only on the projection matrix and the map centre, not on any tile), unlike
// dash_period/dash_on which are per-tile.
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
// mercator convention with the SAME projection matrix, which differs from the web's only by a
// constant scale baked consistently into both the matrix and the positions, so the derivation
// carries over unchanged). Unlike computeReferenceClipW above, this stays in world-pixel space
// deliberately rather than a per-tile local frame - z here is metres of TERRAIN DEPTH along the
// view axis, a frame-level rate with no tile of its own to be local to, not a position to
// transform:
//   1. the projection matrix m       - parameters.transformParams.projMatrix (paint_parameters.hpp)
//   2. the map centre in mercator    - Projection::project(state.getLatLng(), state.getScale())
//                                      (util/projection.hpp), at z = 0 (sea level, matching the
//                                      web's own zc0/w0 - see routes3d.js:1389-1392)
//   3. metres-to-mercator factor mz  - 1 / Projection::getMetersPerPixelAtLatitude(lat, zoom)
//                                      (util/projection.hpp), i.e. world-pixel units per metre,
//                                      at the SAME latitude/zoom
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

    // computeReferenceClipW needs one drawable's own (tileID, matrix) pair to be exact - see
    // that function's comment. WHICH tile is not a free choice: visiting layerGroup drawables in
    // insertion order (tile load order) would pick a different reference tile from one run to
    // the next even for byte-identical frames, and any two same-zoom tiles' matrices differ by a
    // translation, so a different tile is not a rounding error, it is a different, equally
    // valid-looking w - see terrain_contour_layer_tweaker.cpp's identical fix (task C6) for the
    // measured effect this had there. The reference point IS the map centre, so the exact tile
    // to evaluate it in is the tile that CONTAINS the map centre, at the deepest zoom present -
    // a property of the frame, not of load order. Where no drawable covers the centre (the
    // centre is off this layer's own cover), fall back to the lowest tile key present, equally
    // arbitrary in appearance and equally deterministic.
    std::optional<UnwrappedTileID> referenceTile;
    const LatLng referenceCentre = parameters.state.getLatLng();
    const auto tileHoldsCentre = [&](const UnwrappedTileID& id) {
        const Point<double> local = tileLocalPosition(id, referenceCentre);
        return local.x >= 0.0 && local.x <= util::EXTENT && local.y >= 0.0 && local.y <= util::EXTENT;
    };
    const auto tileKey = [](const UnwrappedTileID& id) {
        return std::tuple{id.wrap, id.canonical.z, id.canonical.x, id.canonical.y};
    };
    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }
        const UnwrappedTileID candidate = drawable.getTileID()->toUnwrapped();
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
            if (candidate.canonical.z > referenceTile->canonical.z ||
                (candidate.canonical.z == referenceTile->canonical.z && tileKey(candidate) < tileKey(*referenceTile))) {
                referenceTile = candidate;
            }
        } else if (tileKey(candidate) < tileKey(*referenceTile)) {
            referenceTile = candidate;
        }
    });

    float referenceW = 1e-4f;
    mat4 debugReferenceMatrix{};
    double debugReferenceElevationM = 0.0;
    Point<double> debugReferenceLocal{};
    if (referenceTile) {
        const mat4 referenceMatrix = parameters.matrixForTile(*referenceTile);
        debugReferenceMatrix = referenceMatrix;
        referenceW = computeReferenceClipW(parameters, *referenceTile, referenceMatrix);
        debugReferenceElevationM = (parameters.terrain && parameters.terrain->isEnabled())
                                       ? parameters.terrain->getElevationForLatLng(parameters.state.getLatLng())
                                       : 0.0;
        debugReferenceLocal = tileLocalPosition(*referenceTile, parameters.state.getLatLng());
    }

    // Task 2.2b: the fade's reference point and scale. The centre is a frame-level fact (read
    // once here, like referenceW above); fade_ref/fade_k are still PER-TILE (tileLocalPosition and
    // metresPerExtentUnit both depend on tileID), computed inside the per-drawable lambda below,
    // the same way dash_period/dash_on are.
    const LatLng fadeCenter = parameters.state.getLatLng();
    const float fadeDistance = evaluated.get<TerrainLineFadeDistance>();

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
    // The halo's own half-width, same FAULT 1 FIX convention as half_px above: terrain-line-
    // halo-width is CSS pixels, halved the same way, no pixelRatio multiply. 0 (the default,
    // when the style never sets this) means no halo - see terrain_line.hpp's fragmentMain.
    const float haloHalfPx = evaluated.get<TerrainLineHaloWidth>() / 2.0f;
    // FEATHER FIX (task: feather-vs-geometry settlement, 12 Sept 2026). u_edge_px feeds the
    // shader's coverage ramp AND extends the extruded quad by the same amount on each side
    // (terrain_line.vertex.glsl / mtl/terrain_line.hpp: `ext = halfPx + edge_px`,
    // `a = clamp((half_px - d) / edge_px + 0.5, 0, 1)`) - it IS this layer's antialiasing edge,
    // the direct analogue of the ordinary flat `line` layer's own `ANTIALIASING = 1.0 /
    // DEVICE_PIXEL_RATIO / 2.0` (line.vertex.glsl). That flat-line constant is computed in the
    // SHADER, which is compiled per pixelRatio (DEVICE_PIXEL_RATIO is a compile-time #define set
    // from parameters.pixelRatio - program_parameters.cpp) and so is pinned to exactly half a
    // DEVICE pixel of feather at any pixelRatio. terrain-line-blur/-halo-blur, by contrast, used
    // to be passed straight through from a style-spec CONSTANT (0.5 CSS px) that cannot know
    // which device it will render on: at pixelRatio 3 (an iPhone) that fixed CSS-point number
    // became 1.5 DEVICE pixels of feather - three times the flat line's own - which is exactly
    // the "3x wider feather at dpr 3" defect item 1 of this task's brief asked to be settled
    // against the geometry (settled: the extrusion geometry itself is exact, this feather is
    // the whole defect - see the geometry trace fields just above).
    //
    // Fix: this layer's paint properties keep their CSS-pixel meaning and now default to 0 (see
    // TerrainLineBlur/TerrainLineHaloBlur::defaultValue()), i.e. "no EXTRA blur"; a
    // pixelRatio-correct antialiasing minimum, computed here where parameters.pixelRatio is a
    // live per-frame fact rather than a style-spec constant, is added on top - the direct port
    // of the flat line's own ANTIALIASING formula into CSS-pixel space (u_edge_px's own
    // coordinate space, the same one u_units_to_pixels/u_half_px already operate in): dividing
    // by pixelRatio converts the flat line's DEVICE-pixel constant into that space, exactly as
    // FAULT 1's fix above converts halfPx the other direction. A style that sets
    // terrain-line-blur/-halo-blur (native_lines.py passes the flat style's own line-blur
    // through for the halo, e.g. 2 CSS px) gets that amount ADDED to the minimum, matching the
    // flat line's own `blur2 = blur + 1.0 / DEVICE_PIXEL_RATIO` (its blur property is additive to
    // its own built-in antialiasing edge, never a replacement for it) - terrain-line-blur now
    // means the same thing terrain-line-width already does: a deliberate style choice on top of
    // an engine-owned minimum, not a way to set that minimum.
    const float pixelRatio = std::max(parameters.pixelRatio, 1e-4f);
    const float antialiasingEdgePx = 1.0f / pixelRatio / 2.0f;
    const float edgePx = evaluated.get<TerrainLineBlur>() + antialiasingEdgePx;
    const float haloEdgePx = evaluated.get<TerrainLineHaloBlur>() + antialiasingEdgePx;
    const auto dasharray = evaluated.get<TerrainLineDasharray>();
    // The dash calculation anchors its width lookup at a fixed zoom (DASH_ANCHOR_ZOOM), never
    // at the current frame zoom above - see computeDashPeriodExtent()'s comment.
    const float widthPxAtAnchorZoom = evaluateWidthAtAnchorZoom(properties);

    // DuckMaps fork only, task 2.4f: the reference-w trace lineTraceSlot() feeds - see that
    // function's own comment for why this is keyed by layer id. Entirely skipped (including the
    // drawable-tile-id collection) when DUCKMAPS_ELEVATION_TRACE is unset, so this costs nothing
    // in a normal run, same discipline as terrain_contour_layer_tweaker.cpp's own traceOn block.
    if (lineTraceEnabled()) {
        std::vector<UnwrappedTileID> traceTileIds;
        visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
            if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
                return;
            }
            traceTileIds.push_back(drawable.getTileID()->toUnwrapped());
        });
        std::ostringstream os;
        os << std::setprecision(9);
        os << "{\"referenceTile\":";
        if (referenceTile) {
            os << "\"" << lineTileIdDebugKey(*referenceTile) << "\"";
        } else {
            os << "null";
        }
        os << ",\"referenceTileZ\":" << (referenceTile ? static_cast<int>(referenceTile->canonical.z) : -1)
           << ",\"referenceW\":" << referenceW
           << ",\"referenceMatrixRow3\":[" << debugReferenceMatrix[3] << "," << debugReferenceMatrix[7]
           << "," << debugReferenceMatrix[11] << "," << debugReferenceMatrix[15] << "]"
           << ",\"referenceElevationM\":" << debugReferenceElevationM
           << ",\"referenceLocal\":[" << debugReferenceLocal.x << "," << debugReferenceLocal.y << "]"
           << ",\"mapZoom\":" << parameters.state.getZoom()
           << ",\"widthPxStyle\":" << widthPx
           << ",\"halfPx\":" << halfPx
           << ",\"widthPxAtAnchorZoom\":" << widthPxAtAnchorZoom
           << ",\"pixelRatio\":" << parameters.pixelRatio
           << ",\"logicalSize\":[" << parameters.state.getSize().width << "," << parameters.state.getSize().height << "]"
           // DuckMaps fork only, task: settle geometry against measurement (12 Sept 2026). The
           // vertex shader (terrain_line.vertex.glsl / mtl/terrain_line.hpp vertexMain) extrudes
           // BOTH opposite side vertices of a segment by offsetPx = n * (side * ext), a CSS-point
           // magnitude (ext = halfPx + edgePx, widthScale pinned to 1.0), then clip-space-encodes
           // that as (offsetPx / u_units_to_pixels) * w0 added to p0.xy while leaving p0.w == w0
           // untouched. Differencing the two opposite-side vertices' CLIP-SPACE positions BEFORE
           // the perspective divide and then dividing by their shared w (the perspective divide
           // itself) therefore always yields exactly 2 * offsetPx / u_units_to_pixels in NDC,
           // for ANY vertex, at ANY depth: w0 cancels algebraically, so this ratio owes nothing
           // to which vertex, which tile, or which frame's camera - it is fixed by ext and
           // u_units_to_pixels alone. u_units_to_pixels itself converts NDC to LOGICAL (CSS-
           // point) pixels (it is 1 / pixelsToGLUnits = state.getSize() / 2, and state.getSize()
           // is the map's logical size in points, not the device framebuffer - see FAULT 1's
           // comment above), so one further multiply by pixelRatio converts that CSS-point
           // figure to device pixels, the same conversion u_units_to_pixels->device-pixel
           // reasoning FAULT 1 already established. The two fields below are exactly that
           // computation, done here on the CPU from the same ext/pixelRatio values the vertex
           // shader itself consumes - algebraically identical to reading back two real GPU
           // vertices' clip-space xyzw and dividing by w, without needing a GPU-side readback
           // path this engine does not otherwise have. "core" is the pure-colour half of the
           // quad (no feather, a==1 region: 2*halfPx), "total" is the full extruded quad
           // including both edges' AA feather (2*(halfPx+edgePx)) - the two different quantities
           // item 3 of this task's brief asks every subsequent measurement to keep separate.
           << ",\"edgePx\":" << edgePx
           << ",\"haloHalfPx\":" << haloHalfPx
           << ",\"haloEdgePx\":" << haloEdgePx
           << ",\"coreWidthDevicePx\":" << (2.0f * halfPx * parameters.pixelRatio)
           << ",\"totalWidthDevicePx\":" << (2.0f * (halfPx + edgePx) * parameters.pixelRatio)
           << ",\"haloCoreWidthDevicePx\":" << (2.0f * haloHalfPx * parameters.pixelRatio)
           << ",\"haloTotalWidthDevicePx\":" << (2.0f * (haloHalfPx + haloEdgePx) * parameters.pixelRatio)
           << ",\"drawableTileZs\":[";
        bool firstZ = true;
        for (const auto& id : traceTileIds) {
            if (!firstZ) {
                os << ",";
            }
            firstZ = false;
            os << static_cast<int>(id.canonical.z);
        }
        os << "],\"drawableTileIds\":[";
        bool firstId = true;
        for (const auto& id : traceTileIds) {
            if (!firstId) {
                os << ",";
            }
            firstId = false;
            os << "\"" << lineTileIdDebugKey(id) << "\"";
        }
        os << "]}";
        lineTraceSlot()[layerGroup.getName()] = os.str();
    }

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const TerrainLineEvaluatedPropsUBO evaluatedPropsUBO = {
            .color = evaluated.get<TerrainLineColor>(),
            .opacity = evaluated.get<TerrainLineOpacity>(),
            .half_px = halfPx,
            .edge_px = edgePx,
            .rail_offset = evaluated.get<TerrainLineOffset>(),
            .depth_bias = 0.00002f, // DEPTH_BIAS, routes3d.js:100 - a constant, not a property
            .ghost_opacity = evaluated.get<TerrainLineGhostOpacity>(),
            .fade = evaluated.get<TerrainLineFade>(),
            .fade_distance = evaluated.get<TerrainLineFadeDistance>(),
            .pad1 = 0,
            .pad2 = 0,
            .halo_half_px = haloHalfPx,
            .halo_edge_px = haloEdgePx,
            .halo_color = evaluated.get<TerrainLineHaloColor>()};
        context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &evaluatedPropsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idTerrainLineEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

    // DuckMaps fork only, task C7: see terrain_contour_layer_tweaker.cpp's identical comment on
    // its own evaluatedPropsUBOForTrace - same reason (this struct is only rebuilt on frames the
    // properties change, but the trace needs it on every traced frame), same "kept in sync by
    // hand" caveat.
    const bool traceOn = lineTraceEnabled();
    TerrainLineEvaluatedPropsUBO evaluatedPropsUBOForTrace{};
    if (traceOn) {
        evaluatedPropsUBOForTrace = {
            .color = evaluated.get<TerrainLineColor>(),
            .opacity = evaluated.get<TerrainLineOpacity>(),
            .half_px = halfPx,
            .edge_px = edgePx,
            .rail_offset = evaluated.get<TerrainLineOffset>(),
            .depth_bias = 0.00002f,
            .ghost_opacity = evaluated.get<TerrainLineGhostOpacity>(),
            .fade = evaluated.get<TerrainLineFade>(),
            .fade_distance = evaluated.get<TerrainLineFadeDistance>(),
            .pad1 = 0,
            .pad2 = 0,
            .halo_half_px = haloHalfPx,
            .halo_edge_px = haloEdgePx,
            .halo_color = evaluated.get<TerrainLineHaloColor>()};
    }

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

        // DuckMaps fork only, task E-vanish part 2: which DEM candidate this specific drawable
        // binds. drawable.getType() packs quadrantIndex*2 + passType (set in
        // RenderTerrainLineLayer::update()) - almost always quadrantIndex 0, the tile's own DEM or
        // its closest ancestor, exactly as getTerrainData() alone always returned before this
        // task. It is only ever nonzero when this tile needed more than one covering descendant
        // (see RenderTerrain::getAllTerrainData's own comment) and update() built one drawable
        // pair per candidate; each such drawable binds ONE of those candidates, and the shader's
        // own in-bounds test (terrain_line.vertex.glsl/mtl) draws only the ground that candidate's
        // DEM texture actually covers, discarding the rest so the other candidates' drawables can
        // cover it correctly instead of this one guessing at it.
        const std::size_t quadrantIndex = drawable.getType() / 2;

        // Bind the covering DEM tile so the vertex shader can elevate both ends of every
        // sub-segment onto the terrain (RenderTerrain::getTerrainData, see
        // symbol_layer_tweaker.cpp:170-234's identical pattern). terrainEnabled itself is
        // computed once per frame above; only the per-tile DEM lookup happens here.
        std::optional<RenderTerrain::TerrainData> terrainData;
        std::optional<UnwrappedTileID> terrainDataTileID;
        if (terrainEnabled) {
            const auto allData = parameters.terrain->getAllTerrainData(tileID);
            if (quadrantIndex < allData.size()) {
                terrainDataTileID = allData[quadrantIndex].first;
                terrainData = allData[quadrantIndex].second;
            }
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

        // Two-pass halo: which pass this drawable is, set by RenderTerrainLineLayer::update() via
        // gfx::Drawable::setType(TerrainLinePassType) when it built this drawable - see this
        // header's TerrainLinePassType comment for why the value has to be duplicated into BOTH
        // TerrainLineDrawableUBO (vertex-only) and TerrainLineTilePropsUBO (fragment-only) below
        // rather than read from one shared place.
        // DuckMaps fork only, task E-vanish part 2: pass type is now the LOW bit of the packed
        // type (see quadrantIndex above) - drawable.getType() % 2, not the raw value.
        const float haloPass = (drawable.getType() % 2) == static_cast<std::size_t>(TerrainLinePassType::Halo)
                                    ? 1.0f
                                    : 0.0f;

        // Task 2.2b: this tile's own fade reference point and scale - see tileLocalPosition() and
        // metresPerExtentUnit() above. fade_k is 0 whenever terrain-line-fade-distance is not
        // positive, which the vertex shader's own ft/fade formula turns into "no fade" regardless
        // of terrain-line-fade's value - the same "far > 0" gate the web applies on the CPU side
        // (routes3d.js:1037).
        const Point<double> fadeRefLocal = tileLocalPosition(tileID, fadeCenter);
        const float fadeK = fadeDistance > 0.0f
                                ? metresPerExtentUnit(fadeCenter, tileID.canonical) / fadeDistance
                                : 0.0f;

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
            .fade_ref = {{static_cast<float>(fadeRefLocal.x), static_cast<float>(fadeRefLocal.y)}},
            .fade_k = fadeK,
            .halo_pass = haloPass,
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
            .halo_pass = haloPass,
        };

        // DuckMaps fork only, task C7: see terrain_contour_layer_tweaker.cpp's identical block
        // for the full reasoning (hashing the CPU-side struct is correct under
        // MLN_UBO_CONSOLIDATION too, same binding-index order argument applies here with
        // idTerrainLineDrawableUBO/idTerrainLineTilePropsUBO/idTerrainLineEvaluatedPropsUBO in
        // place of terrain-contour's own three ids). Two textures this time, in binding order:
        // idTerrainLineDEMTexture then idTerrainLineDepthTexture.
        if (traceOn) {
#if MLN_UBO_CONSOLIDATION
            const TerrainLineDrawableUBO& drawableUBORef = drawableUBOVector[i];
            const TerrainLineTilePropsUBO& tilePropsUBORef = tilePropsUBOVector[i];
#else
            const TerrainLineDrawableUBO& drawableUBORef = drawableUBO;
            const TerrainLineTilePropsUBO& tilePropsUBORef = tilePropsUBO;
#endif
            uint64_t uboHash = debugFnv1a64(&drawableUBORef, sizeof(drawableUBORef));
            uboHash = debugFnv1a64(&tilePropsUBORef, sizeof(tilePropsUBORef), uboHash);
            uboHash = debugFnv1a64(&evaluatedPropsUBOForTrace, sizeof(evaluatedPropsUBOForTrace), uboHash);

            // DuckMaps fork only, task E-vanish part 2: identity comes from terrainDataTileID,
            // resolved above for THIS drawable's own quadrantIndex, not a fresh
            // debugDemTileIdForTile(tileID) lookup - that call knows nothing about which candidate
            // this particular drawable bound and would always name candidate 0's tile even when
            // this drawable is showing a different quadrant's DEM.
            std::string demTexIdentity;
            if (parameters.terrain) {
                if (terrainData) {
                    if (terrainDataTileID) {
                        demTexIdentity = "dem:" + lineTileIdDebugKey(*terrainDataTileID);
                    } else {
                        demTexIdentity = "dem:resolved-but-untraced";
                    }
                } else {
                    demTexIdentity = "dem:placeholder";
                }
            } else {
                demTexIdentity = "dem:terrain-off-placeholder";
            }
            // The depth texture is one shared frame-level render target, not a per-tile
            // resource - there is no tile id to hang an identity off, and (see
            // RenderTerrain::debugDemTileIdForTile's own doc comment on why source bytes are not
            // reachable here) no cheap CPU-side buffer to hash either, so this identifies it by
            // size/real-vs-placeholder only. That is enough to see a drawable bound to a
            // DIFFERENT depth texture object (a resize, or real vs placeholder), but NOT enough
            // to see the SAME texture's content changing frame to frame - RenderTerrain's own
            // depth-render-skip gate (renderDepth's cameraMoved/depthDirty check) means the
            // object is normally stable for the whole life of a still camera, so this is a
            // deliberate, reported limitation rather than an invented stand-in identity.
            const std::string depthTexIdentity = std::string("depth:") + (hasRealDepthTexture ? "real:" : "placeholder:") +
                                                 std::to_string(depthSize.width) + "x" + std::to_string(depthSize.height);

            DebugDrawableUBOEntry entry;
            entry.layer = layerGroup.getName();
            entry.id = lineTileIdDebugKey(tileID);
            entry.pass = drawable.getDrawPriority();
            entry.ubo = uboHash;
            entry.tex = {demTexIdentity, depthTexIdentity};
            entry.wrap = tileID.wrap;
            entry.z = tileID.canonical.z;
            entry.x = tileID.canonical.x;
            entry.y = tileID.canonical.y;
            lineUboEntriesSlot().push_back(std::move(entry));
        }

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

// DuckMaps fork only, task C7: see the .hpp declaration's own comment. Drained (and cleared)
// exactly once per frame by Renderer::Impl::render, the same call site as
// TerrainContourLayerTweaker::debugDrainContourDrawableUBOEntries.
std::vector<DebugDrawableUBOEntry> TerrainLineLayerTweaker::debugDrainLineDrawableUBOEntries() {
    auto& slot = lineUboEntriesSlot();
    std::vector<DebugDrawableUBOEntry> result = std::move(slot);
    slot.clear();
    return result;
}

// DuckMaps fork only, task 2.4f: drained (and cleared) once per frame by Renderer::Impl::render,
// alongside debugDrainContourReferenceTraceJSON - see lineTraceSlot()'s own comment for why this
// returns one JSON OBJECT keyed by layer id rather than terrain_contour_layer_tweaker.cpp's
// single string. "{}" when the trace is off or no terrain-line layer executed this frame.
std::string TerrainLineLayerTweaker::debugDrainLineReferenceTraceJSON() {
    auto& slot = lineTraceSlot();
    if (slot.empty()) {
        return "{}";
    }
    std::ostringstream os;
    os << "{";
    bool first = true;
    for (const auto& [layerId, json] : slot) {
        if (!first) {
            os << ",";
        }
        first = false;
        os << "\"" << layerId << "\":" << json;
    }
    os << "}";
    slot.clear();
    return os.str();
}

} // namespace mln
