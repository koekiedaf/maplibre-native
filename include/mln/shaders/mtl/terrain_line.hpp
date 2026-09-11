#pragma once

#include <mln/shaders/terrain_line_layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only. Ported from the web engine's routes3d.js VS/FS (quoted in full at
// docs/plans/grounding/ground-web-engine.md section 1, source routes3d.js:375-489), with the
// changes docs/plans/2026-09-11-engine-layer-plumbing.md's task brief calls for: tile-local 2D
// positions elevated via get_elevation() for both this vertex and the "other" endpoint (instead
// of the web's already-3D mercator a_pos/a_other), one ribbon/one colour/one width/one dash per
// drawable instance (no per-feature data-driven attributes - all nine paint properties are
// PropertyValue<T>, evaluated once per layer per frame into TerrainLineEvaluatedPropsUBO), and
// no raw u_viewport uniform (see the vertex shader comment below for why).
//
// Left out of this first cut, deferred to 2.2b: the terrain depth-texture occlusion test
// (u_depth/u_depth_texel/u_depth_test/u_ghost/u_occlusion_* in the web shader), the distance
// fade (u_fade_ref/u_fade_k/u_fade_amount), and every debug early-out. No depth texture is
// bound here.
//
// NOTE: this file is intentionally never run through clang-format. Its raw string literals are
// Metal shader source, not C++; formatting it here has previously corrupted comment text inside
// the shader source (a wrapped hyphenated word broke a // comment mid-line and desynced the
// raw-string content from being treated as a plain string, since clang-format's comment reflow
// does not know it is looking at an embedded language). Hand-indent any edits instead.
constexpr auto terrainLineShaderPrelude = R"(

enum {
    idTerrainLineDrawableUBO = idDrawableReservedVertexOnlyUBO,
    idTerrainLineEvaluatedPropsUBO = drawableReservedUBOCount,
    terrainLineUBOCount
};

struct alignas(16) TerrainLineDrawableUBO {
    /*   0 */ float4x4 matrix;

    /*  64 */ float4 dem_coords;
    /*  80 */ float4 dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;

    /* 108 */ float reference_w;

    /* 112 */ float dash_period;
    /* 116 */ float dash_on;
    /* 120 */ float pad1;
    /* 124 */ float pad2;
    /* 128 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 8 * 16, "wrong size");

struct alignas(16) TerrainLineEvaluatedPropsUBO {
    /*  0 */ float4 color;
    /* 16 */ float opacity;
    /* 20 */ float half_px;
    /* 24 */ float edge_px;
    /* 28 */ float rail_offset;
    /* 32 */ float depth_bias;
    /* 36 */ float ghost_opacity; // 2.2b, unused so far
    /* 40 */ float fade;          // 2.2b, unused so far
    /* 44 */ float fade_distance; // 2.2b, unused so far
    /* 48 */ float pad1;
    /* 52 */ float pad2;
    /* 56 */ float pad3;
    /* 60 */ float pad4;
    /* 64 */
};
static_assert(sizeof(TerrainLineEvaluatedPropsUBO) == 4 * 16, "wrong size");

)";

template <>
struct ShaderSource<BuiltIn::TerrainLineShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "TerrainLineShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 4> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 1> textures;

    static constexpr auto prelude = terrainLineShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    short2 pos [[attribute(0)]];
    short2 other [[attribute(1)]];
    short2 flag [[attribute(2)]];
    float dist [[attribute(3)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float4 center;
    float dist;
    float side;
    float half_px;
};

FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                device const GlobalPaintParamsUBO& paintParams [[buffer(idGlobalPaintParamsUBO)]],
                                device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                device const TerrainLineDrawableUBO* drawableVector [[buffer(idTerrainLineDrawableUBO)]],
                                device const TerrainLineEvaluatedPropsUBO& props [[buffer(idTerrainLineEvaluatedPropsUBO)]],
                                texture2d<float, access::sample> demTexture [[texture(0)]],
                                sampler demSampler [[sampler(0)]]) {

    device const TerrainLineDrawableUBO& drawable = drawableVector[uboIndex];

    const float2 pos = float2(vertx.pos);
    const float2 other = float2(vertx.other);

    // Both ends are sampled independently and elevated onto the terrain here, in the vertex
    // shader - the whole point of an elevated (renderToTerrain=false) layer type rather than a
    // draped one (see RenderTerrainLineLayer / docs/plans/2026-09-11-engine-layer-plumbing.md A4).
    const float eleA = get_elevation(pos, demTexture, demSampler, drawable.dem_coords, drawable.dem_unpack,
                                     drawable.dem_dim, drawable.dem_exaggeration, drawable.dem_enabled);
    const float eleB = get_elevation(other, demTexture, demSampler, drawable.dem_coords, drawable.dem_unpack,
                                     drawable.dem_dim, drawable.dem_exaggeration, drawable.dem_enabled);

    float4 p0 = drawable.matrix * float4(pos, eleA, 1.0);
    float4 p1 = drawable.matrix * float4(other, eleB, 1.0);
    const float4 center = p0;

    const float w0 = max(p0.w, 1e-4);
    const float w1 = max(p1.w, 1e-4);

    // Screen space (device pixel) direction and normal for this sub-segment. The web shader
    // carries a raw u_viewport uniform, multiplying by half the viewport size to convert into
    // that pixel space, then, after extruding by a pixel amount, dividing back out by the same
    // half-viewport factor to return to clip space: a pixel space corrected for aspect ratio,
    // and its exact inverse. paintParams.units_to_pixels (GlobalPaintParamsUBO, already
    // bound to every shader) is precisely that same "0.5 * viewport" factor. It is defined as
    // 1 / PaintParameters::pixelsToGLUnits, and pixelsToGLUnits is 2 / framebuffer size
    // (paint_parameters.cpp). So dividing by it here and multiplying by it again below
    // reproduces the web's round trip exactly, with no separate viewport uniform needed.
    float2 s0 = (p0.xy / w0) * paintParams.units_to_pixels;
    float2 s1 = (p1.xy / w1) * paintParams.units_to_pixels;
    float2 d = s1 - s0;
    const float len = length(d);
    d = (len > 1e-3) ? (d / len) : float2(1.0, 0.0);
    d *= float(vertx.flag.x);
    const float2 n = float2(-d.y, d.x);

    // widthScale keeps the ribbon's on-screen width constant at the map centre (where
    // reference_w equals w0) while it scales naturally with depth away from it - see
    // TerrainLineDrawableUBO::reference_w's comment.
    const float widthScale = max(drawable.reference_w / w0, 0.0);
    const float halfPx = props.half_px * widthScale;
    const float ext = halfPx + props.edge_px;
    // Square caps extend the quad forward and back by the same half width (u_cap_px equals
    // u_half_px in the web engine, routes3d.js:1120-1121), so it is not a separate property;
    // props.half_px is reused directly here rather than adding one.
    const float2 offsetPx = n * (float(vertx.flag.y) * ext + props.rail_offset * widthScale) -
                            d * (float(vertx.flag.x) * props.half_px * widthScale);

    p0.xy += (offsetPx / paintParams.units_to_pixels) * w0;
    // Pull the ribbon toward the camera by a small constant fraction of its own depth, so it
    // does not z-fight with the terrain surface it sits directly on top of.
    p0.z -= props.depth_bias * w0;

    return {
        .position = p0,
        .center   = center,
        .dist     = vertx.dist,
        .side     = float(vertx.flag.y),
        .half_px  = halfPx,
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                            device const TerrainLineDrawableUBO* drawableVector [[buffer(idTerrainLineDrawableUBO)]],
                            device const TerrainLineEvaluatedPropsUBO& props [[buffer(idTerrainLineEvaluatedPropsUBO)]]) {
#if defined(OVERDRAW_INSPECTOR)
    return half4(1.0);
#endif

    device const TerrainLineDrawableUBO& drawable = drawableVector[uboIndex];

    // dash_period == 0 (an empty/undefined dasharray) means "draw solid".
    if (drawable.dash_period > 0.0 && fract(in.dist / drawable.dash_period) > drawable.dash_on) {
        discard_fragment();
    }

    // 2.2b adds the terrain depth-texture occlusion test and the distance fade here, both keyed
    // off in.center and props.ghost_opacity/fade/fade_distance - neither is read in this cut.
    const float alpha = props.opacity;

    const float d = abs(in.side) * (in.half_px + props.edge_px);
    const float a = clamp((in.half_px - d) / props.edge_px + 0.5, 0.0, 1.0);

    // Premultiplied alpha, matching the web shader's `fragColor = v_color * (a * alpha)`.
    return half4(props.color * (a * alpha));
}
)";
};

} // namespace shaders
} // namespace mln
