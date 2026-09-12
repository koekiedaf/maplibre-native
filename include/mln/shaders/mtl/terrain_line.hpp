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
// Task 2.2: the terrain depth-texture occlusion test is now ported (u_depth/u_depth_texel/
// u_occlusion_eps/u_occlusion_far/u_ghost in the web shader - routes3d.js:425-480 for the shader,
// :111-175 for the argument for a metres-based margin, :1365-1395 for occlusionFar()). Still left
// out: the distance fade (u_fade_ref/u_fade_k/u_fade_amount) and every debug early-out.
//
// NOTE: this file is intentionally never run through clang-format. Its raw string literals are
// Metal shader source, not C++; formatting it here has previously corrupted comment text inside
// the shader source (a wrapped hyphenated word broke a // comment mid-line and desynced the
// raw-string content from being treated as a plain string, since clang-format's comment reflow
// does not know it is looking at an embedded language). Hand-indent any edits instead.
constexpr auto terrainLineShaderPrelude = R"(

enum {
    idTerrainLineDrawableUBO = idDrawableReservedVertexOnlyUBO,
    idTerrainLineTilePropsUBO = idDrawableReservedFragmentOnlyUBO,
    idTerrainLineEvaluatedPropsUBO = drawableReservedUBOCount,
    terrainLineUBOCount
};

// Vertex-only. See terrain_line_layer_ubo.hpp's comment on TerrainLineDrawableUBO for why
// dash_period/dash_on were moved out of this struct into TerrainLineTilePropsUBO below - the
// fragment shader used to read them straight out of this buffer, which is bound to the vertex
// stage only on Metal.
struct alignas(16) TerrainLineDrawableUBO {
    /*   0 */ float4x4 matrix;

    /*  64 */ float4 dem_coords;
    /*  80 */ float4 dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;

    /* 108 */ float reference_w;
    /* 112 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 7 * 16, "wrong size");

// Fragment-only, bound at idDrawableReservedFragmentOnlyUBO. Filled in lockstep with
// TerrainLineDrawableUBO (same index, same tile, every frame).
//
// Task 2.2: occlusion_eps/occlusion_far/depth_texel/depth_enabled are the terrain depth-texture
// occlusion test (routes3d.js:425-480, reasoning at :111-175, occlusionFar() at :1365-1395).
// They live here, NOT in TerrainLineDrawableUBO above, because that struct is bound at
// idDrawableReservedVertexOnlyUBO, which is bound to the VERTEX stage only on Metal - see this
// struct's own top-of-file comment for the dash-period bug that exact mistake caused.
struct alignas(16) TerrainLineTilePropsUBO {
    /*  0 */ float dash_period;
    /*  4 */ float dash_on;
    /*  8 */ float occlusion_eps;
    /* 12 */ float occlusion_far;
    /* 16 */ float2 depth_texel;
    /* 24 */ float depth_enabled;
    /* 28 */ float pad1;
    /* 32 */
};
static_assert(sizeof(TerrainLineTilePropsUBO) == 2 * 16, "wrong size");

struct alignas(16) TerrainLineEvaluatedPropsUBO {
    /*  0 */ float4 color;
    /* 16 */ float opacity;
    /* 20 */ float half_px;
    /* 24 */ float edge_px;
    /* 28 */ float rail_offset;
    /* 32 */ float depth_bias;
    // Task 2.2: ghost_opacity is now read by the fragment shader's occlusion test below -
    // `if (ghost <= 0) discard; else alpha *= ghost` (routes3d.js:474).
    /* 36 */ float ghost_opacity;
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
    static const std::array<TextureInfo, 2> textures;

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
                            device const TerrainLineTilePropsUBO* tilePropsVector [[buffer(idTerrainLineTilePropsUBO)]],
                            device const TerrainLineEvaluatedPropsUBO& props [[buffer(idTerrainLineEvaluatedPropsUBO)]],
                            texture2d<float, access::sample> depthTexture [[texture(1)]],
                            sampler depthSampler [[sampler(1)]]) {
#if defined(OVERDRAW_INSPECTOR)
    return half4(1.0);
#endif

    device const TerrainLineTilePropsUBO& tileProps = tilePropsVector[uboIndex];

    // dash_period == 0 (an empty/undefined dasharray) means "draw solid".
    if (tileProps.dash_period > 0.0 && fract(in.dist / tileProps.dash_period) > tileProps.dash_on) {
        discard_fragment();
    }

    float alpha = props.opacity;

    // Task 2.2: terrain occlusion, ported from the web engine's fragment shader (routes3d.js:
    // 454-475; the argument for the metres-based margin is at :111-175; occlusionFar() is at
    // :1365-1395). Gated on depth_enabled (TerrainLineLayerTweaker::execute sets it to 0 whenever
    // there is no terrain, or terrain is on but the depth pass has not produced a real texture
    // yet) so the test costs nothing and changes nothing with terrain off.
    if (tileProps.depth_enabled > 0.5) {
        const float3 c = in.center.xyz / in.center.w;
        // common.hpp's depth_opacity() samples the depth texture at (uv.x, 1.0 - uv.y) - our own
        // y convention is flipped relative to clip space. Match it here for the same reason.
        const float2 uv = float2(c.x * 0.5 + 0.5, 1.0 - (c.y * 0.5 + 0.5));
        // The texture is at CSS-pixel size, nearest-sampled: on a steep face the texel under the
        // centreline can belong to a neighbouring pixel whose terrain is metres nearer. The
        // farthest of the 3x3 texels around the centreline decides, which leaks through a ridge
        // by about one pixel and nothing more (routes3d.js:456-460).
        float terrain = 0.0;
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                const float4 rgba = depthTexture.sample(depthSampler,
                                                        uv + float2(float(i), float(j)) * tileProps.depth_texel);
                terrain = max(terrain, unpack_depth(rgba));
            }
        }
        // Round 14 phase 3 (routes3d.js:467-473): the margin, in NDC z, is the smaller of the
        // constant that has shipped since round 10 (occlusion_eps) and occlusion_far_m metres of
        // terrain converted to NDC z at THIS fragment's own depth (occlusion_far / w^2).
        const float eps = min(tileProps.occlusion_eps,
                              tileProps.occlusion_far / max(in.center.w * in.center.w, 1e-6));
        if (terrain + eps < c.z) {
            if (props.ghost_opacity <= 0.0) {
                discard_fragment();
            }
            alpha *= props.ghost_opacity;
        }
    }

    const float d = abs(in.side) * (in.half_px + props.edge_px);
    const float a = clamp((in.half_px - d) / props.edge_px + 0.5, 0.0, 1.0);

    // Premultiplied alpha, matching the web shader's `fragColor = v_color * (a * alpha)`.
    return half4(props.color * (a * alpha));
}
)";
};

} // namespace shaders
} // namespace mln
