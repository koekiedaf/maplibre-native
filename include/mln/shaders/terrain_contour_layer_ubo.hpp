#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only, task 2.4a. Per-tile data for the terrain-contour shader, filled by
// TerrainContourLayerTweaker::execute() every frame - the same dem_* binding pattern
// symbol_layer_tweaker.cpp:170-234 and TerrainLineLayerTweaker::execute use (see
// docs/plans/2026-09-11-engine-layer-plumbing.md).
//
// Split into a vertex-only UBO (this one) and a fragment-only UBO (TerrainContourTilePropsUBO,
// below) rather than one struct read from both stages - task 2.4b found the shader's fragment
// stage reading this SAME struct from idTerrainContourDrawableUBO, which is
// idDrawableReservedVertexOnlyUBO: mtl::UniformBufferArray::bindMtl
// (src/mln/mtl/uniform_buffer.cpp:39-51) binds a buffer at that reserved id to the VERTEX stage
// only (skips bindFragment whenever id == idDrawableReservedVertexOnlyUBO, by design - see
// hillshade/symbol/line/terrain, which all keep their own vertex-only matrix UBO strictly
// separate from a same-named "TilePropsUBO" at idDrawableReservedFragmentOnlyUBO for exactly
// this reason). The fragment stage was therefore reading an unbound buffer - Metal's argument
// table has nothing there - so `dem_enabled` (and everything else in the struct) read back as
// 0.0 for every fragment, regardless of tile: get_elevation()'s `dem_enabled == 0.0` early-out
// forced `e` to a constant 0.0 for every fragment, so every gradient (`fwidth(e / interval)`)
// was exactly 0.0 too, and terrain_contour_coverage()'s own flat-ground guard (`grad < 1e-4`)
// discarded every fragment on every tile, at every pitch - matching the reported symptom (zero
// contour pixels at pitch 0, 45 and 60) exactly. The dem_* fields the fragment stage needs are
// therefore duplicated into TerrainContourTilePropsUBO below rather than shared with this one.
struct alignas(16) TerrainContourDrawableUBO {
    /*   0 */ std::array<float, 4 * 4> matrix;

    /*  64 */ std::array<float, 4> dem_coords;
    /*  80 */ std::array<float, 4> dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;
    /* 108 */ float pad0;
    /* 112 */
};
static_assert(sizeof(TerrainContourDrawableUBO) == 7 * 16);

// Fragment-only per-tile data - see TerrainContourDrawableUBO's comment above for why this is a
// separate struct/buffer rather than the fragment stage reading that one. Bound at
// idDrawableReservedFragmentOnlyUBO, filled in lockstep with TerrainContourDrawableUBO (same
// index i, same tile, every frame) by TerrainContourLayerTweaker::execute.
struct alignas(16) TerrainContourTilePropsUBO {
    /*  0 */ std::array<float, 4> dem_coords;
    /* 16 */ std::array<float, 4> dem_unpack;
    /* 32 */ float dem_dim;
    /* 36 */ float dem_exaggeration;
    /* 40 */ float dem_enabled;

    // Clip-space w at the ground under the map centre, at the centre's own sampled elevation -
    // contours3d.js's referenceClipW() (lines 896-904), mirrored in
    // TerrainContourLayerTweaker::computeReferenceClipW (identical to
    // TerrainLineLayerTweaker's own copy of the same web-engine function). Frame-constant, not
    // tile-dependent, but carried per-drawable like terrain-line's own reference_w so the
    // MLN_UBO_CONSOLIDATION per-drawable-array path needs no special case.
    /* 44 */ float reference_w;

    // Task: the terrain depth-texture occlusion test, ported onto terrain-contour from
    // terrain-line's own TerrainLineTilePropsUBO (task 2.2) - see terrain_line_layer_ubo.hpp's
    // comment on the same four fields for the full derivation (routes3d.js:425-480 for the
    // shader, :111-175 for the metres-based margin, :1365-1395 for occlusionFar()).
    //
    // Contour keeps its own hardware depth test against RenderTerrain's coarse mesh (LEQUAL,
    // DEPTH_BIAS/SLOPE_BIAS below win it reliably because contour draws on that SAME mesh's own
    // vertices - unlike terrain-line, whose independently-elevated ribbon vertices would
    // self-occlude against the mesh's coarser triangulation, see
    // TerrainLineLayerTweaker::execute's comment on setEnableDepth). That hardware test is what
    // SLOPE_BIAS exists to win at a near edge-on triangle (a cirque wall viewed along its own
    // strike) - but the same unbounded slope-scaled pull can, at exactly that kind of grazing
    // angle, push a hidden fragment's depth closer than a genuinely nearer ridge's own true
    // (unbiased) depth, defeating the hardware test it was never meant to fight. This depth-
    // texture test is independent of that bias entirely - it reads the SAME pre-rendered terrain
    // depth pass ribbons already use, which carries no slope bias - so a fragment behind a ridge
    // is discarded here regardless of what the biased hardware test decided.
    /* 48 */ float occlusion_eps;
    /* 52 */ float occlusion_far;
    /* 56 */ std::array<float, 2> depth_texel;
    /* 64 */ float depth_enabled;
    /* 68 */ float pad1;
    /* 72 */ float pad2;
    /* 76 */ float pad3;
    /* 80 */
};
static_assert(sizeof(TerrainContourTilePropsUBO) == 5 * 16);

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
