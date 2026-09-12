#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

// Per-tile/per-drawable VERTEX-ONLY data for the terrain-line ribbon shader. Filled by
// TerrainLineLayerTweaker::execute() every frame, mirroring
// symbol_layer_tweaker.cpp:170-234's dem_* binding pattern (see
// docs/plans/2026-09-11-engine-layer-plumbing.md section A2).
//
// Task 2.2c: this struct is read ONLY by the vertex stage. It used to also carry dash_period/
// dash_on, which the FRAGMENT shader read straight out of this same buffer at
// idTerrainLineDrawableUBO == idDrawableReservedVertexOnlyUBO - but
// mtl::UniformBufferArray::bindMtl (src/mln/mtl/uniform_buffer.cpp:39-51) binds a buffer at that
// reserved id to the VERTEX stage only (by design: see hillshade/symbol/line/terrain-contour,
// which all keep this exact vertex-only/fragment-only split). The fragment stage therefore read
// an unbound Metal argument-table slot for dash_period/dash_on - always 0.0 - so the shader's own
// `dash_period > 0.0` dash test never fired and every dasharray rendered solid, regardless of
// value. Those two fields now live in TerrainLineTilePropsUBO below, bound at
// idDrawableReservedFragmentOnlyUBO and filled in lockstep with this struct (same index i, same
// tile, every frame) by the tweaker - the same fix terrain-contour's TilePropsUBO split applied
// for dem_enabled/reference_w in task 2.4b.
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
    // shrinks with depth away from it, like a real object would as the camera tilts. Read by the
    // vertex stage only.
    /* 108 */ float reference_w;

    // Task 2.2b: this tile's own distance-fade reference point and scale - see
    // shaders/mtl/terrain_line.hpp's top-of-file comment for why these live here (vertex-only)
    // rather than in TerrainLineEvaluatedPropsUBO below, and terrain_line_layer_tweaker.cpp's
    // tileLocalPosition()/metresPerExtentUnit() for how each is computed. fade_ref is the map
    // centre expressed in THIS TILE's own EXTENT-unit local coordinates (matching a_pos/a_other's
    // own units, mln::TileCoordinate); fade_k is (metres per EXTENT unit at this tile's own zoom)
    // divided by terrain-line-fade-distance, so the vertex shader's own
    // `length(pos - fade_ref) * fade_k` needs one multiply and no divide.
    /* 112 */ std::array<float, 2> fade_ref;
    /* 120 */ float fade_k;
    /* 124 */ float pad2;
    /* 128 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 8 * 16);

// Fragment-only per-tile data - see TerrainLineDrawableUBO's comment above for why this is a
// separate struct/buffer rather than the fragment stage reading that one. Bound at
// idDrawableReservedFragmentOnlyUBO, filled in lockstep with TerrainLineDrawableUBO (same index
// i, same tile, every frame) by TerrainLineLayerTweaker::execute.
//
// Task 2.2: the fields below occlusion_eps onward are the terrain depth-texture occlusion test
// (the web engine's u_depth/u_depth_texel/u_occlusion_eps/u_occlusion_far, routes3d.js:425-480 for
// the shader, :111-175 for why a metres-based margin exists at all, :1365-1395 for occlusionFar()).
// They MUST live here, not in TerrainLineDrawableUBO: that struct is bound at
// idDrawableReservedVertexOnlyUBO, which mtl::UniformBufferArray::bindMtl binds to the VERTEX
// stage only - the exact mistake that made every dash render solid until engine 20aa9f479705 (see
// TerrainLineDrawableUBO's own comment above). ghost_opacity is NOT duplicated here: it is a
// per-layer (not per-tile) evaluated paint property and already lives in
// TerrainLineEvaluatedPropsUBO below, filled once per layer per frame.
//
// depth_texel/occlusion_far/depth_enabled are computed ONCE PER FRAME by
// TerrainLineLayerTweaker::execute (not per tile/drawable - the depth texture, the frame's
// projection matrix and whether terrain is on are all frame-level facts) and copied into every
// tile's TilePropsUBO entry, the same way dash_period/dash_on are computed per-tile today.
struct alignas(16) TerrainLineTilePropsUBO {
    // Dash period/on-fraction for THIS drawable's tile, already converted from the paint
    // property's width-units dasharray into this tile's EXTENT-unit distance space - see
    // TerrainLineLayerTweaker::computeDashPeriodExtent's comment for the exact conversion.
    // dash_period == 0 means "no dashing, draw solid" (also the default: an empty dasharray).
    /*  0 */ float dash_period;
    /*  4 */ float dash_on;
    // NDC-z occlusion margin constant, routes3d.js's OCCLUSION_EPS_DEFAULT = 0.002 (:133).
    /*  8 */ float occlusion_eps;
    // occlusionFar(m) (routes3d.js:1381-1395): OCCLUSION_EPS_M_DEFAULT (60 m of terrain) converted
    // once per frame into the NDC-z-per-(1/w^2) constant the fragment shader divides by
    // v_center.w^2 - see TerrainLineLayerTweaker's occlusionFarNDC() for the derivation and where
    // each of the web function's inputs comes from in this engine.
    /* 12 */ float occlusion_far;
    // 1 / depth texture width, height (CSS-pixel sized, nearest-sampled) - the 3x3 max read's
    // texel step, matching the web's u_depth_texel.
    /* 16 */ std::array<float, 2> depth_texel;
    // 0 when there is no terrain, or terrain is on but the depth pass has not produced a real
    // texture yet (still the far-plane placeholder) - gates the whole occlusion test off so it
    // costs nothing and changes nothing with terrain off.
    /* 24 */ float depth_enabled;
    /* 28 */ float pad1;
    /* 32 */
};
static_assert(sizeof(TerrainLineTilePropsUBO) == 2 * 16);

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
                                // Task 2.2: ghost_opacity is now read by the fragment shader's terrain occlusion test
                                // (TerrainLineTilePropsUBO's comment above) - `if (ghost <= 0) discard; else alpha *=
                                // ghost` (routes3d.js:474).
    /* 36 */ float ghost_opacity;
    // Task 2.2b: fade (terrain-line-fade, the amount) is read by the VERTEX shader, even though
    // this buffer's other fields are fragment-facing, because that buffer is already bound to
    // both stages (unlike TerrainLineDrawableUBO above, vertex-only on Metal) - see
    // shaders/mtl/terrain_line.hpp's top-of-file comment. fade_distance (terrain-line-fade-
    // distance, the metres boundary) is not read directly by either shader stage; it is folded
    // into TerrainLineDrawableUBO::fade_k once per tile instead, so the shader needs no divide.
    /* 40 */ float fade;
    /* 44 */ float fade_distance; // folded into TerrainLineDrawableUBO::fade_k, not read directly
    /* 48 */ float pad1;
    /* 52 */ float pad2;
    /* 56 */ float pad3;
    /* 60 */ float pad4;
    /* 64 */
};
static_assert(sizeof(TerrainLineEvaluatedPropsUBO) == 4 * 16);

} // namespace shaders
} // namespace mln
