#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only, task 2.4a. Per-tile data for the terrain-contour shader, filled by
// TerrainContourLayerTweaker::execute() every frame - the same dem_* binding pattern
// symbol_layer_tweaker.cpp:170-234 and TerrainLineLayerTweaker::execute use (see
// docs/plans/2026-09-11-engine-layer-plumbing.md).
struct alignas(16) TerrainContourDrawableUBO {
    /*   0 */ std::array<float, 4 * 4> matrix;

    /*  64 */ std::array<float, 4> dem_coords;
    /*  80 */ std::array<float, 4> dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;

    // Clip-space w at the ground under the map centre, at the centre's own sampled elevation -
    // contours3d.js's referenceClipW() (lines 896-904), mirrored in
    // TerrainContourLayerTweaker::computeReferenceClipW (identical to
    // TerrainLineLayerTweaker's own copy of the same web-engine function). Frame-constant, not
    // tile-dependent, but carried per-drawable like terrain-line's own reference_w so the
    // MLN_UBO_CONSOLIDATION per-drawable-array path needs no special case.
    /* 108 */ float reference_w;
    /* 112 */
};
static_assert(sizeof(TerrainContourDrawableUBO) == 7 * 16);

/// Evaluated (per-layer, zoom-evaluated) properties that do not depend on the tile. All ten
/// paint properties are non-data-driven (PropertyValue<T>) - see
/// docs/plans/2026-09-11-engine-layer-plumbing.md and contours3d.js's own TUNE block. Colours
/// are premultiplied by their own opacity here, on the CPU side, exactly as contours3d.js's
/// render() does before uploading u_minor_color/u_index_color.
struct alignas(16) TerrainContourEvaluatedPropsUBO {
    /*  0 */ Color minor_color; // premultiplied by terrain-contour-minor-opacity
    /* 16 */ Color index_color; // premultiplied by terrain-contour-index-opacity
    /* 32 */ float minor_interval; // metres; 0 = don't draw
    /* 36 */ float index_interval; // metres; 0 = don't draw
    // minor/index width and the fade band are RAW pixels at contours3d.js's own REF_RATIO (2),
    // scaled by (pixelRatio / 2) in the tweaker - see TerrainContourLayerTweaker::execute.
    /* 40 */ float minor_width;
    /* 44 */ float index_width;
    /* 48 */ float fade_lo;
    /* 52 */ float fade_hi;
    // Constants, not paint properties - contours3d.js DEPTH_BIAS/SLOPE_BIAS (lines 102, 131).
    /* 56 */ float depth_bias;
    /* 60 */ float slope_bias;
    /* 64 */
};
static_assert(sizeof(TerrainContourEvaluatedPropsUBO) == 4 * 16);

} // namespace shaders
} // namespace mln
