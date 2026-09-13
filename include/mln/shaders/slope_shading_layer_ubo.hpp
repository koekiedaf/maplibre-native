#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only, task 2.6. Per-tile data for the slope-shading shader, filled by
// SlopeShadingLayerTweaker::execute() every frame - the same dem_* binding pattern
// terrain_contour_layer_ubo.hpp's own structs use (see that header's long comment for why a
// vertex-only and a fragment-only struct are kept separate rather than one struct read from
// both stages: mtl::UniformBufferArray::bindMtl binds a buffer at idDrawableReservedVertexOnlyUBO
// to the VERTEX stage only, so a fragment shader reading it sees an unbound argument-table slot).
//
// Unlike terrain-contour, this layer has no screen-space-referenced pixel width (an area fill,
// not a line) and no depth-texture occlusion test of its own - it draws over the identical mesh,
// tested with the identical hardware depth test (LEQUAL, read-only, SLOPE_BIAS-adjusted) that
// wins terrain-contour's own test at a grazing angle, and an area fill has no per-pixel width to
// occlude more precisely than that. So this struct carries only what positioning and slope/aspect
// actually need: the DEM binding (duplicated into the fragment-only struct below, same reason as
// terrain-contour's own duplication) and, new here, m_per_extent - the metres one EXTENT unit of
// this tile's own mesh spans at its own latitude (contours3d.js's tileMetres(), the only thing
// that turns a difference in metres of elevation into a slope angle - see
// SlopeShadingLayerTweaker::metersPerTile for the derivation).
struct alignas(16) SlopeShadingDrawableUBO {
    /*   0 */ std::array<float, 4 * 4> matrix;

    /*  64 */ std::array<float, 4> dem_coords;
    /*  80 */ std::array<float, 4> dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;
    /* 108 */ float pad0;
    /* 112 */
};
static_assert(sizeof(SlopeShadingDrawableUBO) == 7 * 16);

// Fragment-only per-tile data - see this header's own comment above for why this is a separate
// struct/buffer from SlopeShadingDrawableUBO rather than the fragment stage reading that one.
// Bound at idDrawableReservedFragmentOnlyUBO, filled in lockstep with SlopeShadingDrawableUBO
// (same index i, same tile, every frame) by SlopeShadingLayerTweaker::execute.
struct alignas(16) SlopeShadingTilePropsUBO {
    /*  0 */ std::array<float, 4> dem_coords;
    /* 16 */ std::array<float, 4> dem_unpack;
    /* 32 */ float dem_dim;
    /* 36 */ float dem_exaggeration;
    /* 40 */ float dem_enabled;
    /* 44 */ float m_per_extent;
    /* 48 */
};
static_assert(sizeof(SlopeShadingTilePropsUBO) == 3 * 16);

/// Evaluated (per-layer, zoom-evaluated) properties that do not depend on the tile. Both paint
/// properties (opacity and preset) are non-data-driven (PropertyValue<float>), matching
/// terrain-contour's own all-PropertyValue paint set - see docs/plans/2026-09-11-engine-layer-
/// plumbing.md. "preset" never reaches the shader: it only selects which lookup texture
/// SlopeShadingLayerTweaker builds and binds (see idSlopeShadingLutTexture), so it is not in this
/// struct.
struct alignas(16) SlopeShadingEvaluatedPropsUBO {
    /*  0 */ float opacity;
    // Constants, not paint properties - contours3d.js DEPTH_BIAS/SLOPE_BIAS, same re-derivation
    // for Metal's [0,1] clip convention as terrain-contour's own (see
    // TerrainContourEvaluatedPropsUBO's comment on depth_bias for the derivation).
    /*  4 */ float depth_bias;
    /*  8 */ float slope_bias;
    /* 12 */ float pad0;
    /* 16 */
};
static_assert(sizeof(SlopeShadingEvaluatedPropsUBO) == 1 * 16);

} // namespace shaders
} // namespace mln
