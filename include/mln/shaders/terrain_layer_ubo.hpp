#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

struct alignas(16) TerrainDrawableUBO {
    /*  0 */ std::array<float, 4 * 4> matrix;
    /* 64 */ std::array<float, 4> dem_coords; // scale, x offset, y offset into the bound DEM
                                              // tile ({1,0,0,0} unless an ancestor is bound)
    /* 80 */ std::array<float, 4 * 4> fog_matrix; // DuckMaps fork only: maplibre-gl-js's own
                                                   // terrain ground fog (TransformState::getFogMatrix,
                                                   // multiplied per-tile in TerrainLayerTweaker::execute
                                                   // exactly as `matrix` above is). A vertex-stage-only
                                                   // value, correctly placed here in TerrainDrawableUBO
                                                   // (bound at idTerrainDrawableUBO =
                                                   // idDrawableReservedVertexOnlyUBO) and NOT in
                                                   // TerrainEvaluatedPropsUBO below - Metal binds that
                                                   // reserved id to the vertex stage only. A previous
                                                   // fork task put a fragment-read value in this UBO by
                                                   // mistake and every dash rendered solid; do not repeat
                                                   // it by moving this.
    /* 144 */
};
static_assert(sizeof(TerrainDrawableUBO) == 9 * 16);

// One entry per instance of the instanced GL terrain depth pass. The whole array is bound as
// the TerrainDrawableUBO block and indexed by gl_InstanceID in terrain_depth.vertex. Kept
// separate from TerrainDrawableUBO so the shared struct (and the mtl/vulkan/webgpu layouts
// mirroring it) stay untouched. std140: mat4 then two vec4s -> 6*16 bytes.
struct alignas(16) TerrainDepthInstanceUBO {
    /*  0 */ std::array<float, 4 * 4> matrix;
    /* 64 */ std::array<float, 4> dem_coords; // scale, x offset, y offset, dem_dim (in .w)
    /* 80 */ float dem_layer;                 // sampler2DArray layer of this tile's DEM
    /* 84 */ float pad1;
    /* 88 */ float pad2;
    /* 92 */ float pad3;
    /* 96 */
};
static_assert(sizeof(TerrainDepthInstanceUBO) == 6 * 16);

struct alignas(16) TerrainTilePropsUBO {
    /*  0 */ std::array<float, 2> dem_tl;
    /*  8 */ float dem_scale;
    /* 12 */ float pad1;
    /* 16 */
};
static_assert(sizeof(TerrainTilePropsUBO) == 16);

/// Evaluated properties that do not depend on the tile
struct alignas(16) TerrainEvaluatedPropsUBO {
    /*  0 */ std::array<float, 4> unpack; // DEM unpack vector for the source's encoding
    /* 16 */ float exaggeration;
    /* 20 */ float elevation_offset;
    /* 24 */ float pad1;
    /* 28 */ float pad2;
    // DuckMaps fork only: maplibre-gl-js's own terrain ground fog uniforms (search the bundle
    // for `u_fog_ground_blend_opacity:`). Read by both stages, so - unlike fog_matrix above -
    // these belong in this UBO: idTerrainEvaluatedPropsUBO is drawableReservedUBOCount, bound to
    // both vertex and fragment stages, not one of the reserved single-stage ids. Populated from
    // the style's `sky` root property in TerrainLayerTweaker::execute; when there is no sky at
    // all these take the web's own "no sky" defaults (white fog/horizon colour, blend 1,
    // opacity 0), which make the fragment shader's blend `if` false and leave every fragment
    // exactly as it was before this UBO gained these fields.
    /* 32 */ std::array<float, 4> fog_color;
    /* 48 */ std::array<float, 4> horizon_color;
    /* 64 */ float fog_ground_blend;
    /* 68 */ float fog_ground_blend_opacity;
    /* 72 */ float horizon_fog_blend;
    /* 76 */ float pad3;
    /* 80 */
};
static_assert(sizeof(TerrainEvaluatedPropsUBO) == 80);

} // namespace shaders
} // namespace mln
