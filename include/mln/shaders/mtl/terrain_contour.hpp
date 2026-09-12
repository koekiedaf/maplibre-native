#pragma once

#include <mln/shaders/terrain_contour_layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

namespace mln {
namespace shaders {

// DuckMaps fork only, task 2.4a. Contour lines computed per-fragment from the terrain DEM,
// ported line for line from the web engine's contours3d.js VS/FS (see
// container/server/app/map/assets/contours3d.js:179-302, quoted and explained at length in that
// file's own header comment, and docs/plans/2026-09-11-engine-layer-plumbing.md for the
// plumbing this shader plugs into).
//
// Differences from the web shader, all forced by porting into this engine rather than being a
// judgment call:
//  - Elevation comes from this engine's own get_elevation() prelude helper (mtl/common.hpp),
//    not a hand-rolled decodeDem()/eleTex() - the web reimplements MapLibre GL JS's own bilinear
//    sampler by hand because it cannot reach the engine's; here we ARE the engine. The prelude's
//    padding convention is +2.0 texels (a 2-pixel backfilled DEM border), the web's is +1.0 -
//    this is not a discrepancy to reconcile, it is two different DEM texture packings (native's
//    RenderTerrain uploads a 2px border, gl-js's the web reads uploads a 1px one), and the
//    prelude is authoritative here because the texture this shader samples is the engine's own.
//  - gl_FragCoord.w (used for widthScale) is exactly Metal's fragment [[position]].w - both are
//    1/w_clip - so no conversion is needed at the call site.
//  - u_reference_w is computed once per frame on the CPU (TerrainContourLayerTweaker,
//    mirroring contours3d.js's referenceClipW()) rather than derived from a per-drawable matrix
//    in the shader.
//
// NOTE: like terrain_line.hpp, this file is intentionally never run through clang-format - see
// that file's comment for why. Hand-indent any edits instead.
constexpr auto terrainContourShaderPrelude = R"(

enum {
    idTerrainContourDrawableUBO = idDrawableReservedVertexOnlyUBO,
    // Fragment-only per-tile data. NOT the same buffer as idTerrainContourDrawableUBO above:
    // mtl::UniformBufferArray::bindMtl (src/mln/mtl/uniform_buffer.cpp:39-51) binds a buffer at
    // idDrawableReservedVertexOnlyUBO to the vertex stage only, so a fragment shader reading it
    // sees an unbound argument-table slot - every field, including dem_enabled, reads back as
    // 0.0 regardless of which tile's drawable is being drawn (see TerrainContourDrawableUBO's
    // own comment in terrain_contour_layer_ubo.hpp for the full account of the bug this caused:
    // zero contour pixels at every pitch). The fields the fragment stage needs are duplicated
    // into TerrainContourTilePropsUBO, filled in lockstep with the drawable UBO by
    // TerrainContourLayerTweaker::execute, and bound at this reserved fragment-only id instead -
    // the same split hillshade/symbol/line/terrain all use for their own vertex- vs
    // fragment-only per-drawable data.
    idTerrainContourTilePropsUBO = idDrawableReservedFragmentOnlyUBO,
    idTerrainContourEvaluatedPropsUBO = drawableReservedUBOCount,
    terrainContourUBOCount
};

struct alignas(16) TerrainContourDrawableUBO {
    /*   0 */ float4x4 matrix;

    /*  64 */ float4 dem_coords;
    /*  80 */ float4 dem_unpack;
    /*  96 */ float dem_dim;
    /* 100 */ float dem_exaggeration;
    /* 104 */ float dem_enabled;
    /* 108 */ float pad0;
    /* 112 */
};
static_assert(sizeof(TerrainContourDrawableUBO) == 7 * 16, "wrong size");

struct alignas(16) TerrainContourTilePropsUBO {
    /*  0 */ float4 dem_coords;
    /* 16 */ float4 dem_unpack;
    /* 32 */ float dem_dim;
    /* 36 */ float dem_exaggeration;
    /* 40 */ float dem_enabled;
    /* 44 */ float reference_w;
    // Task: terrain depth-texture occlusion test, see terrain_contour_layer_ubo.hpp's comment
    // on the C++ twin of this struct for the full reasoning.
    /* 48 */ float occlusion_eps;
    /* 52 */ float occlusion_far;
    /* 56 */ float2 depth_texel;
    /* 64 */ float depth_enabled;
    /* 68 */ float pad1;
    /* 72 */ float pad2;
    /* 76 */ float pad3;
    /* 80 */
};
static_assert(sizeof(TerrainContourTilePropsUBO) == 5 * 16, "wrong size");

struct alignas(16) TerrainContourEvaluatedPropsUBO {
    /*  0 */ float4 minor_color;
    /* 16 */ float4 index_color;
    /* 32 */ float minor_interval;
    /* 36 */ float index_interval;
    /* 40 */ float minor_width;
    /* 44 */ float index_width;
    /* 48 */ float fade_lo;
    /* 52 */ float fade_hi;
    /* 56 */ float depth_bias;
    /* 60 */ float slope_bias;
    /* 64 */
};
static_assert(sizeof(TerrainContourEvaluatedPropsUBO) == 4 * 16, "wrong size");

)";

template <>
struct ShaderSource<BuiltIn::TerrainContourShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "TerrainContourShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 2> textures;

    static constexpr auto prelude = terrainContourShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    // xy = tile-local position (0..EXTENT), z = skirt flag (1 = skirt curtain vertex), matching
    // RenderTerrain's own mesh vertex layout exactly - this shader draws on that SAME mesh
    // (RenderTerrainContourLayer reuses RenderTerrain::getMesh(), see render_terrain.cpp's
    // generateMesh() for the layout this decodes).
    short4 pos [[attribute(0)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float2 pos_extent;
    float skirt;
    // Task: the terrain depth-texture occlusion test - the raw clip-space position (BEFORE the
    // depth_bias pull below and before the rasterizer's own perspective divide), matching
    // terrain_line.hpp's v_center exactly. `in.position` cannot be reused for this: on Metal a
    // fragment [[position]] is already in pixel/window coordinates with z remapped to [0,1] by
    // the depth_bias-adjusted write below, not the NDC xyz the occlusion test needs to compare
    // against the depth texture's own UV space.
    float4 center;
};

struct FragmentOut {
    half4 color [[color(0)]];
    float depth [[depth(any)]];
};

// contours3d.js distToLine()/coverage()/density(), ported line for line.
inline float terrain_contour_dist_to_line(float v, float i) {
    float m = v / i;
    float f = fract(m);
    return min(f, 1.0 - f);
}

// grad's `< 1e-4` early-out matters: without it, perfectly flat ground (a lake, or the ocean at
// elevation 0, itself an exact multiple of every interval) reports a fake zero-width crossing
// and washes solid colour over the whole flat area instead of drawing nothing - see
// contours3d.js's own comment on this exact guard.
inline float terrain_contour_coverage(float v, float i, float halfPx, float grad) {
    if (grad < 1e-4) {
        return 0.0;
    }
    float aa = grad * halfPx;
    return 1.0 - smoothstep(0.0, aa, terrain_contour_dist_to_line(v, i));
}

inline float terrain_contour_density(float grad, float fadeLo, float fadeHi) {
    return smoothstep(fadeLo, fadeHi, 1.0 / max(grad, 1e-6));
}

FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                device const TerrainContourDrawableUBO* drawableVector [[buffer(idTerrainContourDrawableUBO)]],
                                device const TerrainContourEvaluatedPropsUBO& props [[buffer(idTerrainContourEvaluatedPropsUBO)]],
                                texture2d<float, access::sample> demTexture [[texture(0)]],
                                sampler demSampler [[sampler(0)]]) {

    device const TerrainContourDrawableUBO& drawable = drawableVector[uboIndex];

    const float2 pos = float2(vertx.pos.xy);
    const float elevation = get_elevation(pos, demTexture, demSampler, drawable.dem_coords, drawable.dem_unpack,
                                          drawable.dem_dim, drawable.dem_exaggeration, drawable.dem_enabled);

    float4 p = drawable.matrix * float4(pos, elevation, 1.0);
    // Captured BEFORE the depth_bias pull below, same as terrain_line.hpp's `center = p0` -
    // the occlusion test in fragmentMain needs this tile's true, unbiased position to look up
    // the terrain depth texture, not the self-z-fight-avoidance value written to gl_Position.
    const float4 center = p;
    // A small constant pull toward the camera in NDC z, scaled by w - this mesh sits exactly on
    // RenderTerrain's own terrain mesh (the SAME shared vertex/index buffers), so the two depth
    // values would otherwise differ only by floating-point noise between two independently
    // computed positions. Matches contours3d.js's own DEPTH_BIAS (0.0003) and its VS comment.
    p.z -= props.depth_bias * p.w;

    return {
        .position   = p,
        .pos_extent = pos,
        .skirt      = float(vertx.pos.z),
        .center     = center,
    };
}

FragmentOut fragment fragmentMain(FragmentStage in [[stage_in]],
                                  device const uint32_t& uboIndex [[buffer(idGlobalUBOIndex)]],
                                  device const TerrainContourTilePropsUBO* tilePropsVector [[buffer(idTerrainContourTilePropsUBO)]],
                                  device const TerrainContourEvaluatedPropsUBO& props [[buffer(idTerrainContourEvaluatedPropsUBO)]],
                                  texture2d<float, access::sample> demTexture [[texture(0)]],
                                  sampler demSampler [[sampler(0)]],
                                  texture2d<float, access::sample> depthTexture [[texture(1)]],
                                  sampler depthSampler [[sampler(1)]]) {
    FragmentOut out;

    // Slope-scaled depth bias (contours3d.js SLOPE_BIAS = 8.0): pulls a fragment toward the
    // camera by extra amount proportional to how fast depth is already changing across it on
    // screen, so a near edge-on triangle (a cirque wall viewed near along its own strike) still
    // wins the depth test against RenderTerrain's own mesh - written unconditionally, before any
    // discard, exactly as contours3d.js's gl_FragDepth assignment is (matching a GLSL
    // requirement that every path assign it; MSL has no such requirement, but the source order
    // is kept identical to the ported original for the same reason: nothing here is now, or
    // will later be, moved after a discard by accident).
    out.depth = in.position.z - props.slope_bias * fwidth(in.position.z);

#if defined(OVERDRAW_INSPECTOR)
    out.color = half4(1.0);
    return out;
#endif

    // idTerrainContourTilePropsUBO, NOT idTerrainContourDrawableUBO - the drawable UBO is bound
    // at idDrawableReservedVertexOnlyUBO, which mtl::UniformBufferArray::bindMtl only binds to
    // the vertex stage (see the long comment on TerrainContourDrawableUBO in
    // terrain_contour_layer_ubo.hpp for the bug that reading it here caused). This tile-props
    // UBO carries the same dem_* fields, duplicated for the fragment stage.
    device const TerrainContourTilePropsUBO& tileProps = tilePropsVector[uboIndex];

    // Skirt curtains hang below the surface to hide cracks between neighbouring terrain tiles;
    // a contour line has no business being drawn on a near-vertical hidden curtain, so those
    // fragments are dropped. Not in the web source (which has no skirt geometry at all - this
    // engine's terrain mesh does) - a judgment call made porting onto this engine's own mesh.
    if (in.skirt > 0.5) {
        out.color = half4(0.0);
        return out;
    }

    // Task: the terrain depth-texture occlusion test, ported onto terrain-contour from
    // terrain-line's own fragmentMain (task 2.2) - see terrain_contour_layer_ubo.hpp's comment on
    // TerrainContourTilePropsUBO for why contour needs this IN ADDITION to (not instead of) its
    // own hardware depth test/SLOPE_BIAS above: that hardware test can be won, at a grazing
    // angle, by a fragment on ground the camera cannot actually see. This test reads the SAME
    // pre-rendered terrain depth pass ribbons use, which carries no slope bias, so it catches
    // exactly the case the biased hardware test can miss.
    if (tileProps.depth_enabled > 0.5) {
        const float3 c = in.center.xyz / in.center.w;
        // common.hpp's unpack_depth() hands back GL NDC z in [-1, 1] - see that function's own
        // comment: the depth pass packs Metal window depth in [0, 1] (rendered with a
        // [0,1]-remapped matrix, exactly like this shader's own vertex stage below), then
        // unpack_depth() converts BACK to GL [-1, 1] for callers whose own clip z is in that
        // convention - which terrain-line's c.z is (its matrix, built via
        // LayerTweaker::getTileMatrix, carries no such remap). This shader's `in.center` comes
        // from the SAME remapped matrix TerrainContourDrawableUBO::matrix carries (this file's
        // vertexMain, matching RenderTerrain's own matrixForTile remap) - Metal [0, 1] - so c.z
        // must be converted to the same GL [-1, 1] convention before comparing against
        // unpack_depth()'s result, or every fragment compares a [0,1] value against a [-1,1] one
        // and (given typical depths land past the texture's own [-1,1] range's midpoint) fails
        // almost everywhere: this was measured directly - the fix below took the occlusion test
        // from discarding every contour fragment in the frame back to only the ones actually
        // hidden behind a ridge.
        const float cz = c.z * 2.0 - 1.0;
        // common.hpp's depth_opacity() samples the depth texture at (uv.x, 1.0 - uv.y) - our own
        // y convention is flipped relative to clip space. Match it here for the same reason.
        const float2 uv = float2(c.x * 0.5 + 0.5, 1.0 - (c.y * 0.5 + 0.5));
        // The texture is at CSS-pixel size, nearest-sampled: on a steep face the texel under this
        // fragment can belong to a neighbouring pixel whose terrain is metres nearer. The
        // farthest of the 3x3 texels around it decides, exactly like terrain-line's own test.
        float terrain = 0.0;
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                const float4 rgba = depthTexture.sample(depthSampler,
                                                        uv + float2(float(i), float(j)) * tileProps.depth_texel);
                terrain = max(terrain, unpack_depth(rgba));
            }
        }
        const float eps = min(tileProps.occlusion_eps,
                              tileProps.occlusion_far / max(in.center.w * in.center.w, 1e-6));
        if (terrain + eps < cz) {
            out.color = half4(0.0);
            return out;
        }
    }

    const float e = get_elevation(in.pos_extent, demTexture, demSampler, tileProps.dem_coords, tileProps.dem_unpack,
                                  tileProps.dem_dim, tileProps.dem_exaggeration, tileProps.dem_enabled);

    // position.w from a Metal fragment [[position]] is already 1/w_clip, the same quantity
    // GLSL's gl_FragCoord.w carries - no conversion needed at this call site.
    const float widthScale = max(tileProps.reference_w * in.position.w, 0.0);

    const float gradIndex = props.index_interval > 0.0 ? fwidth(e / props.index_interval) : 0.0;
    const float gradMinor = props.minor_interval > 0.0 ? fwidth(e / props.minor_interval) : 0.0;

    const float indexCov = props.index_interval > 0.0
                               ? terrain_contour_coverage(e, props.index_interval, props.index_width * widthScale, gradIndex) *
                                     terrain_contour_density(gradIndex, props.fade_lo, props.fade_hi)
                               : 0.0;
    const float minorCov = props.minor_interval > 0.0
                               ? terrain_contour_coverage(e, props.minor_interval, props.minor_width * widthScale, gradMinor) *
                                     terrain_contour_density(gradMinor, props.fade_lo, props.fade_hi)
                               : 0.0;

    // Index wins over minor, exactly as contours3d.js's render() tests index before minor.
    if (indexCov > 0.01) {
        out.color = half4(props.index_color * indexCov);
        return out;
    }
    if (minorCov > 0.01) {
        out.color = half4(props.minor_color * minorCov);
        return out;
    }

    discard_fragment();
    out.color = half4(0.0);
    return out;
}
)";
};

} // namespace shaders
} // namespace mln
