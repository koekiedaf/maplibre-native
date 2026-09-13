#pragma once

#include <mln/shaders/slope_shading_layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only, task 2.6. Slope/aspect area fill computed per-fragment from the terrain
// DEM, ported line for line from the web engine's contours3d.js drawSlope()/FS_SLOPE (see
// container/server/app/map/assets/contours3d.js ~336-560, and terrain_contour.hpp's own header
// comment - the same porting notes apply here: get_elevation() replaces the web's hand-rolled
// decodeDem()/eleTex(), gl_FragCoord.w's role is filled directly by Metal's fragment [[position]]
// .w, wherever this shader needed either).
//
// Differences from terrain-contour's own shader, beyond drawing a fill instead of a line:
//  - Four DEM samples per fragment (N, S, E, W, each 1/128 of a tile off pos_extent) rather than
//    one, central-differenced into a real-metres gradient - the elevations are deliberately NOT
//    multiplied by the terrain exaggeration (get_elevation applies exaggeration; this shader
//    divides it back out) so a 40 degree slope reads 40 degrees whatever the map does to the
//    relief, matching the brief's own requirement and contours3d.js's comment on the same point.
//  - The result of that gradient (a slope angle and a downhill compass bearing) is looked up in a
//    256x256 RGBA8 lookup texture built by SlopeShadingLayerTweaker from David's selected preset
//    (avalanche/aspect/flat), NEAREST-sampled so a band edge stays a hard edge - the web engine's
//    own buildLut()/zoneAt(), ported to the tweaker's CPU-side C++ rather than duplicated here.
//  - No screen-space-referenced pixel width and no depth-texture occlusion test - see
//    slope_shading_layer_ubo.hpp's own header comment for why neither applies to an area fill.
//
// NOTE: like terrain_contour.hpp, this file is intentionally never run through clang-format -
// see that file's comment for why. Hand-indent any edits instead.
constexpr auto slopeShadingShaderPrelude = R"(

enum {
    idSlopeShadingDrawableUBO = idDrawableReservedVertexOnlyUBO,
    idSlopeShadingTilePropsUBO = idDrawableReservedFragmentOnlyUBO,
    idSlopeShadingEvaluatedPropsUBO = drawableReservedUBOCount,
    slopeShadingUBOCount
};

struct alignas(16) SlopeShadingDrawableUBO {
    /*   0 */ float4x4 matrix;

    /*  64 */ float4 dem_coords;
    /*  80 */ float4 dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;
    /* 108 */ float pad0;
    /* 112 */
};
static_assert(sizeof(SlopeShadingDrawableUBO) == 7 * 16, "wrong size");

struct alignas(16) SlopeShadingTilePropsUBO {
    /*  0 */ float4 dem_coords;
    /* 16 */ float4 dem_unpack;
    /* 32 */ float dem_dim;
    /* 36 */ float dem_exaggeration;
    /* 40 */ float dem_enabled;
    /* 44 */ float m_per_extent;
    /* 48 */ float dem_unbuilt;
    /* 52 */ float3 pad0;
    /* 64 */
};
static_assert(sizeof(SlopeShadingTilePropsUBO) == 4 * 16, "wrong size");

struct alignas(16) SlopeShadingEvaluatedPropsUBO {
    /*  0 */ float opacity;
    /*  4 */ float depth_bias;
    /*  8 */ float slope_bias;
    /* 12 */ float pad0;
    /* 16 */
};
static_assert(sizeof(SlopeShadingEvaluatedPropsUBO) == 1 * 16, "wrong size");

)";

template <>
struct ShaderSource<BuiltIn::SlopeShadingShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "SlopeShadingShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 2> textures;

    static constexpr auto prelude = slopeShadingShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    // xy = tile-local position (0..EXTENT), z = skirt flag (1 = skirt curtain vertex) - the same
    // RenderTerrain mesh vertex layout terrain-contour's own VertexStage decodes (see that
    // shader's own comment): this layer draws on the SAME mesh (RenderSlopeShadingLayer reuses
    // RenderTerrain::getMesh(), see render_terrain.cpp's generateMesh()).
    short4 pos [[attribute(0)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float2 pos_extent;
    float skirt;
};

struct FragmentOut {
    half4 color [[color(0)]];
    float depth [[depth(any)]];
};

FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                device const SlopeShadingDrawableUBO* drawableVector [[buffer(idSlopeShadingDrawableUBO)]],
                                device const SlopeShadingEvaluatedPropsUBO& props [[buffer(idSlopeShadingEvaluatedPropsUBO)]],
                                texture2d<float, access::sample> demTexture [[texture(0)]],
                                sampler demSampler [[sampler(0)]]) {

    device const SlopeShadingDrawableUBO& drawable = drawableVector[uboIndex];

    const float2 pos = float2(vertx.pos.xy);
    const float elevation = get_elevation(pos, demTexture, demSampler, drawable.dem_coords, drawable.dem_unpack,
                                          drawable.dem_dim, drawable.dem_exaggeration, drawable.dem_enabled);

    float4 p = drawable.matrix * float4(pos, elevation, 1.0);
    // Same constant depth pull as terrain-contour's own vertex stage, toward the camera in NDC z
    // scaled by w - this mesh sits exactly on RenderTerrain's own terrain mesh, drawn BEFORE
    // terrain-contour (see native_lines.py's layer order), so both need to win the same depth
    // test against RenderTerrain's own drawable for the identical reason (see
    // terrain_contour.hpp's vertexMain comment on DEPTH_BIAS/contours3d.js).
    p.z -= props.depth_bias * p.w;

    return {
        .position   = p,
        .pos_extent = pos,
        .skirt      = float(vertx.pos.z),
    };
}

FragmentOut fragment fragmentMain(FragmentStage in [[stage_in]],
                                  device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                  device const SlopeShadingTilePropsUBO* tilePropsVector [[buffer(idSlopeShadingTilePropsUBO)]],
                                  device const SlopeShadingEvaluatedPropsUBO& props [[buffer(idSlopeShadingEvaluatedPropsUBO)]],
                                  texture2d<float, access::sample> demTexture [[texture(0)]],
                                  sampler demSampler [[sampler(0)]],
                                  texture2d<float, access::sample> lutTexture [[texture(1)]],
                                  sampler lutSampler [[sampler(1)]]) {
    FragmentOut out;

    // Slope-scaled depth bias (contours3d.js SLOPE_BIAS = 8.0) - written unconditionally, before
    // any discard, matching terrain-contour's own fragmentMain and the GLSL original it ports
    // (see that shader's comment for why the source order is kept even though MSL has no such
    // requirement).
    out.depth = in.position.z - props.slope_bias * fwidth(in.position.z);

#if defined(OVERDRAW_INSPECTOR)
    out.color = half4(1.0);
    return out;
#endif

    // idSlopeShadingTilePropsUBO, not idSlopeShadingDrawableUBO - see this shader's own prelude
    // comment (and terrain_contour_layer_ubo.hpp's long comment on the identical split) for why
    // the fragment stage needs its own copy of dem_*.
    device const SlopeShadingTilePropsUBO& tileProps = tilePropsVector[uboIndex];

    // Skirt curtains hang below the surface to hide cracks between neighbouring terrain tiles -
    // no business being shaded, exactly as terrain-contour's own fragmentMain drops them.
    if (in.skirt > 0.5) {
        out.color = half4(0.0);
        return out;
    }

    // No DEM texture for this tile means no honest angle to report. Showing nothing is the only
    // safe answer for a layer people read to judge avalanche terrain; a guess drawn in avalanche
    // colours is not (matches the brief and contours3d.js's own u_has_tex < 0.5 guard).
    //
    // dem_unbuilt is the same rule applied one level deeper: a tile CAN have a real DEM texture
    // here and still carry no honest angle, when that texture is our terrain endpoint's flat
    // sea-level filler for ground outside every built region (or past the Map quality cap) rather
    // than real archive relief - see RenderTerrain::TerrainData::unbuilt. Undrawn, not a "no data"
    // colour and not a grey wash: exactly the same discard as the line above, for the same reason.
    if (tileProps.dem_enabled < 0.5 || tileProps.dem_unbuilt > 0.5 || tileProps.m_per_extent <= 0.0) {
        discard_fragment();
        out.color = half4(0.0);
        return out;
    }

    // A fixed step in TILE space (1/128 of a tile either side, so the two samples are a 64th of a
    // tile apart - about 37 m across a z14 tile in the Alps) rather than one DEM texel: halving it
    // turned the aspect wheel to confetti (measured at Cortina, contours3d.js's own comment). In
    // EXTENT units (this shader's pos_extent, 0..8192) that step is EXTENT/128 = 64.
    const float d = 8192.0 / 128.0;
    const float eL = get_elevation(in.pos_extent + float2(-d, 0.0), demTexture, demSampler, tileProps.dem_coords,
                                   tileProps.dem_unpack, tileProps.dem_dim, tileProps.dem_exaggeration, tileProps.dem_enabled);
    const float eR = get_elevation(in.pos_extent + float2( d, 0.0), demTexture, demSampler, tileProps.dem_coords,
                                   tileProps.dem_unpack, tileProps.dem_dim, tileProps.dem_exaggeration, tileProps.dem_enabled);
    const float eN = get_elevation(in.pos_extent + float2(0.0, -d), demTexture, demSampler, tileProps.dem_coords,
                                   tileProps.dem_unpack, tileProps.dem_dim, tileProps.dem_exaggeration, tileProps.dem_enabled);
    const float eS = get_elevation(in.pos_extent + float2(0.0,  d), demTexture, demSampler, tileProps.dem_coords,
                                   tileProps.dem_unpack, tileProps.dem_dim, tileProps.dem_exaggeration, tileProps.dem_enabled);

    // get_elevation() applies dem_exaggeration (the terrain relief multiplier); a slope angle
    // must not, per the brief ("a 40 degree slope must read as 40 degrees whatever the map is
    // doing to the terrain exaggeration") - divide it back out before differencing, rather than
    // asking get_elevation for an unexaggerated read it has no parameter for.
    const float unexaggerate = tileProps.dem_exaggeration > 0.0 ? 1.0 / tileProps.dem_exaggeration : 1.0;

    // Tile v (pos_extent.y) runs southward, so the north neighbour is the -y one - matching
    // contours3d.js's own comment on the identical convention.
    const float span = 2.0 * d * tileProps.m_per_extent;
    const float dzdE = (eR - eL) * unexaggerate / span;
    const float dzdN = (eN - eS) * unexaggerate / span;
    const float g = sqrt(dzdE * dzdE + dzdN * dzdN);
    // MSL has no degrees()/radians() (GLSL-only builtins) - converted by hand.
    const float RAD_TO_DEG = 57.29577951308232;
    const float slopeDeg = atan(g) * RAD_TO_DEG;
    // Compass bearing of the DOWNHILL direction, 0 at north, clockwise - contours3d.js's own
    // aspectDeg.
    float aspectDeg = g > 1e-6 ? atan2(-dzdE, -dzdN) * RAD_TO_DEG : 0.0;
    if (aspectDeg < 0.0) {
        aspectDeg += 360.0;
    }

    const float4 c = lutTexture.sample(lutSampler, float2(clamp(slopeDeg / 90.0, 0.0, 1.0), aspectDeg / 360.0));
    if (c.a < 0.5) {
        discard_fragment();
        out.color = half4(0.0);
        return out;
    }

    const float a = props.opacity;
    out.color = half4(half3(c.rgb) * half(a), half(a));
    return out;
}
)";
};

} // namespace shaders
} // namespace mln
