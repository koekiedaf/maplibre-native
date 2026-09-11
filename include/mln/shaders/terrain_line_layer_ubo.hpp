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
    /* 120 */ float pad1;
    /* 124 */ float pad2;
    /* 128 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 8 * 16);

/// Evaluated (per-layer, zoom-evaluated) properties that do not depend on the tile. All nine
/// paint properties are non-data-driven (PropertyValue<T>, never DataDrivenPropertyValue<T>), so
/// every one of them is a plain evaluated constant here - there are no vertex attributes/binders
/// for any terrain-line paint property (see docs/plans/2026-09-11-engine-layer-plumbing.md).
struct alignas(16) TerrainLineEvaluatedPropsUBO {
    /*  0 */ Color color;
    /* 16 */ float opacity;
    // terrain-line-width * pixelRatio / 2, in device pixels. Doubles as u_cap_px (the web's
    // drawFamilyPasses sets cap = halfPx too, routes3d.js:1120-1121) - square caps extend the
    // ribbon by the same half width, so it is not a separate property, just this value reused.
    /* 20 */ float half_px;
    /* 24 */ float edge_px;     // terrain-line-blur, the AA feather (u_edge_px)
    /* 28 */ float rail_offset; // terrain-line-offset, in device pixels (u_rail_offset)
    /* 32 */ float depth_bias;  // constant DEPTH_BIAS = 0.00002, matching routes3d.js:100
                                // 2.2b fields: parsed and evaluated now so 2.2b is shader-only, but not yet read by the
                                // shader (no terrain occlusion test or distance fade in this first cut).
    /* 36 */ float ghost_opacity;
    /* 40 */ float fade;
    /* 44 */ float fade_distance;
    /* 48 */ float pad1;
    /* 52 */ float pad2;
    /* 56 */ float pad3;
    /* 60 */ float pad4;
    /* 64 */
};
static_assert(sizeof(TerrainLineEvaluatedPropsUBO) == 4 * 16);

} // namespace shaders
} // namespace mln
