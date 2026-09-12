#pragma once

#include <mln/shaders/terrain_layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

namespace mln {
namespace shaders {

constexpr auto terrainShaderPrelude = R"(

enum {
    idTerrainDrawableUBO = idDrawableReservedVertexOnlyUBO,
    idTerrainTilePropsUBO = idDrawableReservedFragmentOnlyUBO,
    idTerrainEvaluatedPropsUBO = drawableReservedUBOCount,
    terrainUBOCount
};

struct alignas(16) TerrainDrawableUBO {
    /*  0 */ float4x4 matrix;
    /* 64 */ float4 dem_coords;
    /* 80 */ float4x4 fog_matrix; // DuckMaps fork only: see terrain_layer_ubo.hpp's own comment.
                                  // Vertex-stage only, correctly placed here in the vertex-only UBO.
    /* 144 */
};
static_assert(sizeof(TerrainDrawableUBO) == 9 * 16, "wrong size");

struct alignas(16) TerrainTilePropsUBO {
    /*  0 */ float2 dem_tl;
    /*  8 */ float dem_scale;
    /* 12 */ float pad1;
    /* 16 */
};
static_assert(sizeof(TerrainTilePropsUBO) == 16, "wrong size");

/// Evaluated properties that do not depend on the tile
struct alignas(16) TerrainEvaluatedPropsUBO {
    /*  0 */ float4 unpack; // DEM unpack vector for the source's encoding
    /* 16 */ float exaggeration;
    /* 20 */ float elevation_offset;
    /* 24 */ float pad1;
    /* 28 */ float pad2;
    // DuckMaps fork only: see terrain_layer_ubo.hpp's own comment. Read by both stages.
    /* 32 */ float4 fog_color;
    /* 48 */ float4 horizon_color;
    /* 64 */ float fog_ground_blend;
    /* 68 */ float fog_ground_blend_opacity;
    /* 72 */ float horizon_fog_blend;
    /* 76 */ float pad3;
    /* 80 */
};
static_assert(sizeof(TerrainEvaluatedPropsUBO) == 80, "wrong size");

)";

template <>
struct ShaderSource<BuiltIn::TerrainShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "TerrainShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 2> textures;

    static constexpr auto prelude = terrainShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    short4 pos [[attribute(0)]]; // xy = tile position, z = skirt flag (1 = skirt)
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float2 uv;
    float elevation;
    float fog_depth; // DuckMaps fork only: see vertexMain's own comment.
};

FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                device const TerrainDrawableUBO* drawableVector [[buffer(idTerrainDrawableUBO)]],
                                device const TerrainEvaluatedPropsUBO& props [[buffer(idTerrainEvaluatedPropsUBO)]],
                                texture2d<float, access::sample> demTexture [[texture(0)]],
                                sampler demSampler [[sampler(0)]]) {

    device const TerrainDrawableUBO& drawable = drawableVector[uboIndex];

    // The mesh was generated with coordinates from 0 to EXTENT (8192)
    float2 pos = float2(vertx.pos.xy);
    float2 uv = pos / 8192.0;

    // Decode the DEM and interpolate in meters via the shared helper (the packed
    // Terrain-RGB/Terrarium DEM cannot be hardware-filtered, so it is sampled
    // NEAREST and interpolated after decoding, matching maplibre-gl-js and the
    // elevated layers). Map into the bound DEM tile; an ancestor tile is bound as
    // a fallback while this tile's own DEM loads. dem_coords.w = DEM dimension.
    float elevation = get_elevation(pos, demTexture, demSampler, drawable.dem_coords, props.unpack,
                                    drawable.dem_coords.w, props.exaggeration, 1.0);

    // Skirt vertices hang below the surface by elevation_offset, forming a curtain
    // that hides the cracks between neighbouring tiles at different zoom levels
    // (maplibre-gl-js u_ele_delta). pos.z carries the skirt flag.
    const float ele_delta = (float(vertx.pos.z) == 1.0) ? props.elevation_offset : 0.0;
    float4 position = drawable.matrix * float4(pos.x, pos.y, elevation - ele_delta, 1.0);

    // DuckMaps fork only: maplibre-gl-js's own terrain ground fog vertex stage (search the
    // bundle for `vec4 pos=u_fog_matrix*vec4(a_pos3d.xy,ele,1.0)`), ported verbatim. Uses
    // `elevation`, the surface height, NOT `elevation - ele_delta` above - the web's own fog
    // vertex uses `ele`, never `ele - ele_delta`, so the skirt curtain drops the drawn surface
    // without perturbing the fog depth the skirt fades into.
    //
    // `drawable.fog_matrix` (TransformState::getFogMatrix, see its own comment) is built and
    // kept in the OpenGL clip convention (z in [-1,1]) ON PURPOSE: it is never used to
    // rasterize anything, only to produce this one depth number, so it must NOT receive the
    // Metal/Vulkan/WebGPU [0,1]-clip-space z-remap that TerrainLayerTweaker::execute applies to
    // `drawable.matrix` above. The `* 0.5 + 0.5` below is therefore exactly the web's own
    // `v_fog_depth = pos.z / pos.w * 0.5 + 0.5`, not a Metal-specific conversion - do NOT "fix"
    // it to match Metal's clip-space convention, and do NOT clamp or nudge the result.
    const float4 fog_pos = drawable.fog_matrix * float4(pos.x, pos.y, elevation, 1.0);
    const float fog_depth = fog_pos.z / fog_pos.w * 0.5 + 0.5;

    return {
        .position  = position,
        .uv        = uv,
        .elevation = elevation,
        .fog_depth = fog_depth,
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            device const TerrainEvaluatedPropsUBO& props [[buffer(idTerrainEvaluatedPropsUBO)]],
                            texture2d<float, access::sample> mapTexture [[texture(1)]],
                            sampler mapSampler [[sampler(1)]]) {
#if defined(OVERDRAW_INSPECTOR)
    return half4(1.0);
#endif

    // Sample the map texture (render-to-texture output) for the surface color.
    // The drape RTT is rendered with the core's GL-convention projection. On Vulkan
    // (NDC Y-down) that stores the RTT already flipped, so its terrain shader samples
    // 1.0 - uv.y. Metal's NDC is Y-up, so the RTT is stored upright and the V must NOT
    // be flipped here - sampling 1.0 - uv.y flips the draped map vertically (north/south
    // swapped about the view). Sample uv.y directly on Metal.
    const float4 surface_color = mapTexture.sample(mapSampler, float2(in.uv.x, in.uv.y));

    // DuckMaps fork only: maplibre-gl-js's own terrain ground fog fragment stage, ported
    // verbatim (search the bundle for `uniform sampler2D u_texture;uniform vec4 u_fog_color;`).
    // This engine has no globe projection, so the web's `!u_is_globe_mode &&` guard - always
    // true in mercator mode there too - is simply omitted rather than adding an
    // always-false uniform for it.
    if (props.fog_ground_blend_opacity > 0.0 && in.fog_depth > props.fog_ground_blend) {
        constexpr float gamma = 2.2;
        const float4 surface_color_linear = pow(surface_color, float4(gamma));
        const float blend_color = smoothstep(
            0.0, 1.0, max((in.fog_depth - props.horizon_fog_blend) / (1.0 - props.horizon_fog_blend), 0.0));
        const float4 fog_horizon_color_linear = mix(
            pow(props.fog_color, float4(gamma)), pow(props.horizon_color, float4(gamma)), blend_color);
        const float factor_fog = max(in.fog_depth - props.fog_ground_blend, 0.0) / (1.0 - props.fog_ground_blend);
        const float4 blended_linear = mix(
            surface_color_linear, fog_horizon_color_linear, pow(factor_fog, 2.0) * props.fog_ground_blend_opacity);
        return half4(pow(blended_linear, float4(1.0 / gamma)));
    }

    return half4(surface_color);
}
)";
};

} // namespace shaders
} // namespace mln
