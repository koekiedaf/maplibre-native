#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

// Per-tile/per-drawable data for the terrain-line ribbon shader. Filled by
// TerrainLineLayerTweaker::execute() every frame, mirroring
// symbol_layer_tweaker.cpp:170-234's dem_* binding pattern (see
// docs/plans/2026-09-11-engine-layer-plumbing.md section A2).
struct alignas(16) TerrainLineDrawableUBO {
    /*   0 */ std::array<float, 4 * 4> matrix;

    // 3D terrain elevation; see RenderTerrain::getTerrainData. Sampled in the vertex shader via
    // get_elevation() for both the vertex's own position and the other endpoint, so the whole
    // ribbon sits on the terrain surface rather than being draped onto it.
    /*  64 */ std::array<float, 4> dem_coords;
    /*  80 */ std::array<float, 4> dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;

    // Clip-space w at the ground under the map centre, at the centre's own sampled elevation
    // (TerrainLineLayerTweaker::computeReferenceClipW, mirroring the web engine's
    // referenceClipW(), routes3d.js:1223-1231). Scaling u_half_px by (this / this vertex's own
    // w) is what keeps the ribbon's on-screen width constant at the map centre while it grows or
    // shrinks with depth away from it, like a real object would as the camera tilts.
    /* 108 */ float reference_w;

    // Dash period/on-fraction for THIS drawable's tile, already converted from the paint
    // property's width-units dasharray into this tile's EXTENT-unit distance space - see
    // TerrainLineLayerTweaker::computeDashPeriodExtent's comment for the exact conversion.
    // dash_period == 0 means "no dashing, draw solid" (also the default: an empty dasharray).
    /* 112 */ float dash_period;
    /* 116 */ float dash_on;

    // 2.2b occlusion: the frame-constant numerator of the metres-derived occlusion margin (see
    // TerrainLineLayerTweaker::computeOcclusionFar's comment) - the fragment shader divides this
    // by ITS OWN fragment's clip w^2 to reproduce a roughly constant real-world-metres occlusion
    // tolerance across distance, matching the web's min(NDC-constant, metres-form/w^2) margin
    // (routes3d.js:133-173, ground-web-engine.md section 1).
    /* 120 */ float occlusion_far;
    // Real-world metres per world-pixel at the map centre (Projection::getMetersPerPixelAtLatitude),
    // a frame constant duplicated per-drawable like reference_w above. Used by the vertex shader
    // to convert the distance-fade anchor's world-pixel distance into metres, matching
    // terrain-line-fade-distance's own units.
    /* 124 */ float metres_per_pixel;

    // 2.2b distance fade: this tile's own placement in world-pixel space, relative to the map
    // centre (TerrainLineLayerTweaker::execute's fade-geometry comment) - kept as a delta from
    // the centre, not an absolute world-pixel coordinate, so the float stays small (the map
    // centre itself can be an enormous world-pixel value at high zoom, well past float32's
    // precision for sub-pixel differences). The vertex shader adds a_pos.xy * world_px_per_extent
    // to this to get each vertex's own world-pixel delta from the centre, then multiplies by
    // metres_per_pixel to get the ground distance the web's own v_fade smoothstep operates on
    // (routes3d.js:56-58) - ours anchored at the map centre rather than the web's near/far
    // anchor, the honest native equivalent per the task brief.
    /* 128 */ float origin_offset_x;
    /* 132 */ float origin_offset_y;
    // (tile size in world pixels at this tile's own zoom) / EXTENT - converts a_pos.xy (EXTENT
    // units) into a world-pixel delta from this tile's own origin.
    /* 136 */ float world_px_per_extent;
    // 2.2b occlusion: whether terrain is on for this frame at all (a REAL depth texture is
    // bound, not the 1x1 far-plane placeholder) - deliberately NOT the same condition as
    // dem_enabled above. dem_enabled is per-tile ("did THIS tile's own DEM texture load"), so a
    // tile whose DEM has not arrived yet would otherwise wrongly skip the occlusion test even
    // though terrain is genuinely on and other tiles' ribbons must already be tested against it.
    // This mirrors symbol's own explicit `depth_enabled` field (symbol_layer_ubo.hpp), which
    // carries the identical comment for the identical reason - terrain-line copies the pattern
    // rather than reusing dem_enabled.
    /* 140 */ float depth_enabled;
    /* 144 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 9 * 16);

/// Evaluated (per-layer, zoom-evaluated) properties that do not depend on the tile. All nine
/// paint properties are non-data-driven (PropertyValue<T>, never DataDrivenPropertyValue<T>), so
/// every one of them is a plain evaluated constant here - there are no vertex attributes/binders
/// for any terrain-line paint property (see docs/plans/2026-09-11-engine-layer-plumbing.md).
struct alignas(16) TerrainLineEvaluatedPropsUBO {
    /*  0 */ Color color;
    /* 16 */ float opacity;
    // terrain-line-width / 2, in CSS pixels (points) - see TerrainLineLayerTweaker::execute's
    // FAULT 1 FIX comment: this is NOT device pixels, and must not be multiplied by pixelRatio.
    // Doubles as u_cap_px (the web's drawFamilyPasses sets cap = halfPx too, routes3d.js:
    // 1120-1121) - square caps extend the ribbon by the same half width, so it is not a separate
    // property, just this value reused.
    /* 20 */ float half_px;
    /* 24 */ float edge_px;     // terrain-line-blur, the AA feather, CSS pixels (u_edge_px)
    /* 28 */ float rail_offset; // terrain-line-offset, CSS pixels (u_rail_offset)
    /* 32 */ float depth_bias;  // constant DEPTH_BIAS = 0.00002, matching routes3d.js:100
    // 2.2b occlusion/ghost/fade: terrain-line-ghost-opacity (alpha multiplier when occluded,
    // instead of discarding - default 0, so the default behaviour is still a discard), and the
    // distance-fade amount/distance pair (terrain-line-fade, terrain-line-fade-distance), all
    // read by the shader now (terrain_line.fragment.glsl / .vertex.glsl).
    /* 36 */ float ghost_opacity;
    /* 40 */ float fade;
    /* 44 */ float fade_distance;
    // NDC-z occlusion margin constant, routes3d.js OCCLUSION_EPS_DEFAULT (:133) - not a paint
    // property, a fixed constant like depth_bias above, carried in the UBO for the same reason.
    /* 48 */ float occlusion_eps;
    /* 52 */ float pad2;
    /* 56 */ float pad3;
    /* 60 */ float pad4;
    /* 64 */
};
static_assert(sizeof(TerrainLineEvaluatedPropsUBO) == 4 * 16);

} // namespace shaders
} // namespace mln
