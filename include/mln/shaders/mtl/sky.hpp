#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

#include <array>

namespace mln {
namespace shaders {

// DuckMaps fork only, task T3: full-screen sky gradient, driven by the style spec's `sky` root
// property (https://maplibre.org/maplibre-style-spec/sky/). Ported verbatim from
// maplibre-gl-js's own sky vertex/fragment shaders (vendored at
// container/server/app/map/vendor/maplibre-gl-6.mjs in the server checkout - the exact source
// quoted in the task that added this file), including their uniform-value formulas (see
// style::Sky::Impl for the seven evaluated properties this feeds from, and
// Renderer::Impl::render's sky pass for where u_horizon/u_horizon_normal/u_sky_horizon_blend
// get computed from TransformState).
//
// Drawn once per frame as a raw full-screen quad, NOT through the tile drawable/tweaker system
// every other layer shader in this engine uses (TerrainContourShader's per-drawable UBO array,
// RenderTerrainContourLayer's bucket etc.) - there is no tile, no bucket, and nothing to
// instance. This mirrors ClippingMaskProgram's own direct, hand-built pipeline instead
// (clipping_mask.hpp's ClipUBO bound via Context::renderTileClippingMasks) - see
// mtl::Context::renderSky() for the equivalent immediate draw this shader is used from.
//
// Two things this port had to get right that the upstream source does not have to worry about,
// both explained at the point they matter below and in the task's own final report:
//  1. GLSL's gl_FragCoord has its origin at the bottom-left with y growing upward; Metal's
//     fragment [[position]] has its origin at the top-left with y growing downward. The
//     fragment shader below flips y once, explicitly, rather than rederive the (verbatim)
//     upstream formula for the opposite convention.
//  2. WebGL/GLSL ES guarantees an output variable that is not written on every code path reads
//     back as zero (a spec-mandated anti-information-leak rule) - upstream's fragColor relies on
//     this when blend <= 0.0. MSL gives no such guarantee, so it is made explicit here.
//
// This host-side (C++) SkyUBO and the MSL one declared in the prelude string below must stay
// byte-for-byte identical - the same duplication ClipUBO uses in clipping_mask.hpp, for the same
// reason: this struct crosses the C++/MSL boundary as raw bytes (see
// Renderer::Impl::render's sky pass, which builds one of these directly, and
// Context::renderSky(), which uploads it as-is).
struct alignas(16) SkyUBO {
    /*  0 */ std::array<float, 4> sky_color;
    /* 16 */ std::array<float, 4> horizon_color;
    /* 32 */ std::array<float, 2> horizon;
    /* 40 */ std::array<float, 2> horizon_normal;
    /* 48 */ float sky_horizon_blend;
    /* 52 */ float sky_blend;
    /* 56 */ float viewport_height;
    /* 60 */ float pad0;
    /* 64 */
};
static_assert(sizeof(SkyUBO) == 4 * 16, "wrong size");

constexpr auto skyShaderPrelude = R"(

enum {
    idSkyUBO = idDrawableReservedFragmentOnlyUBO,
    skyUBOCount = drawableReservedUBOCount
};

struct alignas(16) SkyUBO {
    /*  0 */ float4 sky_color;
    /* 16 */ float4 horizon_color;
    /* 32 */ float2 horizon;
    /* 40 */ float2 horizon_normal;
    /* 48 */ float sky_horizon_blend;
    /* 52 */ float sky_blend;
    // Device-pixel framebuffer height, used only for the GL-to-Metal fragment coordinate
    // conversion (see this file's own header comment, point 1) - not part of the upstream
    // uniform set, which needs no such conversion under GLSL's own convention.
    /* 56 */ float viewport_height;
    /* 60 */ float pad0;
    /* 64 */
};
static_assert(sizeof(SkyUBO) == 4 * 16, "wrong size");

)";

template <>
struct ShaderSource<BuiltIn::SkyShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "SkyShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 0> textures;

    static constexpr auto prelude = skyShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    // Already clip-space NDC (-1..1), NOT tile-local EXTENT space (compare
    // ClippingMaskProgram's tile-vertex attribute, which IS in EXTENT space and needs a real
    // matrix) - see Context::renderSky()'s own dedicated vertex buffer and its comment on why
    // RenderStaticData's shared tile buffer could not be reused for it.
    float2 position [[attribute(0)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
};

// Ported verbatim from maplibre-gl-js's `void main() { gl_Position = vec4(a_pos, 1.0, 1.0); }`.
FragmentStage vertex vertexMain(VertexStage in [[stage_in]]) {
    return { float4(in.position, 1.0, 1.0) };
}

// Ported verbatim from maplibre-gl-js's sky fragment shader - see this file's own header
// comment for the source and the two conversions this port makes.
half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            device const SkyUBO& sky [[buffer(idSkyUBO)]]) {
    // Conversion 2 (see header comment): GLSL ES/WebGL zero-initializes an output variable
    // that some code path leaves unwritten; upstream's fragColor is only ever assigned inside
    // `if (blend > 0.0)` below, and depends on reading back as vec4(0) otherwise. MSL has no
    // such rule, so it is made explicit.
    float4 fragColor = float4(0.0);

    float x = in.position.x;
    // Conversion 1 (see header comment): flip Metal's top-left-origin, y-grows-down
    // [[position]] into GLSL's bottom-left-origin, y-grows-up gl_FragCoord convention, which is
    // what the (otherwise untouched) formula below is written for.
    //
    // The failure mode this line exists to prevent: without the flip the sky band lands at the
    // BOTTOM of the frame instead of above the terrain silhouette. The measured horizon row
    // against the row predicted from getMercatorHorizon() is recorded in the commit body.
    float y = sky.viewport_height - in.position.y;

    float blend = (y - sky.horizon.y) * sky.horizon_normal.y + (x - sky.horizon.x) * sky.horizon_normal.x;
    if (blend > 0.0) {
        if (blend < sky.sky_horizon_blend) {
            fragColor = mix(sky.sky_color, sky.horizon_color, pow(1.0 - blend / sky.sky_horizon_blend, 2.0));
        } else {
            fragColor = sky.sky_color;
        }
    }
    // u_sky_blend is projectionTransition upstream, 0 in mercator - the only projection this
    // engine has - so this mix() is a no-op today. Kept because it is part of the verbatim
    // upstream formula and costs nothing.
    fragColor = mix(fragColor, float4(float3(0.0), 0.0), sky.sky_blend);

    return half4(fragColor);
}
)";
};

} // namespace shaders
} // namespace mln
