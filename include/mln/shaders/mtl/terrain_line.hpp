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
// drawable instance (no per-feature data-driven attributes - all twelve paint properties are
// PropertyValue<T>, evaluated once per layer per frame into TerrainLineEvaluatedPropsUBO), and
// no raw u_viewport uniform (see the vertex shader comment below for why).
//
// Task 2.2: the terrain depth-texture occlusion test is now ported (u_depth/u_depth_texel/
// u_occlusion_eps/u_occlusion_far/u_ghost in the web shader - routes3d.js:425-480 for the shader,
// :111-175 for the argument for a metres-based margin, :1365-1395 for occlusionFar()). Still left
// out: every debug early-out.
//
// Task 2.2b: the distance fade is now ported too (u_fade_ref/u_fade_k/u_fade_amount in the web
// shader - routes3d.js:381-392 for the vertex math, :455 for where it multiplies alpha, :930-943
// for the reference point and scale, :1035-1038 for the per-ribbon uniform set). fade_ref/fade_k
// are PER-TILE, like dash_period/dash_on, so they live in TerrainLineDrawableUBO below, not
// TerrainLineEvaluatedPropsUBO - the fade curve itself is computed in the VERTEX shader (v_fade
// in the web), so they must be readable by the vertex stage, which is exactly what
// TerrainLineDrawableUBO is bound as (idDrawableReservedVertexOnlyUBO - vertex stage only on
// Metal, see this struct's own comment below). fade/fade_distance (the amount and the metres
// boundary) stay in TerrainLineEvaluatedPropsUBO where they already were: that buffer is bound to
// BOTH stages (it is passed into vertexMain below already, for half_px/edge_px/rail_offset/
// depth_bias), so the vertex shader reads props.fade straight out of it rather than duplicating
// it into TerrainLineDrawableUBO.
//
// The web anchors its fade to its own near-far anchor (state.nearFarAnchor, held still through
// small pans, routes3d.js:930-934) - a hand-over point between this engine's 3D ribbon and a 2D
// flat drawing beyond it. This engine has no such hand-over (there is no 2D fallback drawing to
// hand over to), so fade_ref anchors to the live map centre (state.getLatLng()) instead.
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

    // Task 2.2b: this tile's own fade reference point and scale - see the file's top-of-file
    // comment for why these two live here (vertex-only) rather than in
    // TerrainLineEvaluatedPropsUBO below, and TerrainLineLayerTweaker::execute for how each is
    // computed. fade_ref is the map centre expressed in THIS TILE's own EXTENT-unit local
    // coordinates (matching a_pos/a_other's own units); fade_k is (metres per EXTENT unit at this
    // tile's own zoom) divided by terrain-line-fade-distance, so the vertex shader's
    // `length(pos - fade_ref) * fade_k` is one multiply with no divide, exactly as the web
    // shader's own comment (routes3d.js:381-386) describes for its mercator-unit equivalent.
    /* 112 */ float2 fade_ref;
    /* 120 */ float fade_k;
    /* 124 */ float pad2;
    /* 128 */
};
static_assert(sizeof(TerrainLineDrawableUBO) == 8 * 16, "wrong size");

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
    // Task 2.2b: fade is the distance-fade amount (terrain-line-fade), read by the VERTEX shader
    // below (v_fade's own equivalent) even though this buffer's fields are otherwise fragment-
    // facing - see the file's top-of-file comment for why it stays here rather than moving into
    // TerrainLineDrawableUBO. fade_distance (terrain-line-fade-distance) is not read directly by
    // either shader stage; it is folded into fade_k (TerrainLineDrawableUBO above) once per tile
    // by TerrainLineLayerTweaker::execute instead, so the shader needs no divide.
    /* 40 */ float fade;
    /* 44 */ float fade_distance; // folded into TerrainLineDrawableUBO::fade_k, not read directly
    /* 48 */ float pad1;
    /* 52 */ float pad2;
    // The halo - see terrain_line_layer_ubo.hpp's C++ twin of this struct for the full comment.
    // Two of the four trailing pad floats become halo_half_px/halo_edge_px; halo_color is a new
    // trailing float4. halo_half_px == 0 (the default) means no halo.
    /* 56 */ float halo_half_px;
    /* 60 */ float halo_edge_px;
    /* 64 */ float4 halo_color;
    /* 80 */
};
static_assert(sizeof(TerrainLineEvaluatedPropsUBO) == 5 * 16, "wrong size");

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
    float fade;
    // The halo: the vertex stage already scaled both the body and halo half-widths by
    // widthScale (reference_w / this vertex's own w) and picked whichever extends the quad
    // further, so the fragment stage needs the quad's own true extent (ext_px) to turn `side`
    // (a -1..1 fraction of the quad's own half-width) back into a screen-pixel distance from the
    // centreline, and the halo's own scaled half-width (halo_half_px) to test against it.
    // halo_half_px == 0 means no halo, exactly as today.
    float ext_px;
    float halo_half_px;
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
    // The halo pass shares this same quad rather than drawing a second one (see this file's
    // top-of-file comment and terrain_line_layer_ubo.hpp's TerrainLineEvaluatedPropsUBO comment
    // for the one-pass compositing argument): the quad must extend far enough to cover whichever
    // of the body or the halo reaches further, so `ext` (the extrusion distance) and `capPx` (the
    // square-cap extension) both take the max of the body's and the halo's own half-width plus
    // feather. With halo_half_px == 0 (the default), max(halfPx + edge_px, 0 + halo_edge_px) and
    // max(props.half_px, 0) both collapse back to exactly today's halfPx + props.edge_px and
    // props.half_px, so an unset halo changes nothing here.
    const float haloHalfPx = (props.halo_half_px > 0.0) ? props.halo_half_px * widthScale : 0.0;
    // The halo's own feather only widens the quad when there IS a halo. Without this guard an
    // unset halo would still push `ext` up to halo_edge_px (0.5 by default) for any ribbon
    // narrower than that, which is a change to a style that never asked for a halo.
    const float ext = (haloHalfPx > 0.0) ? max(halfPx + props.edge_px, haloHalfPx + props.halo_edge_px)
                                         : halfPx + props.edge_px;
    // The square cap stays the BODY's own half width, NOT the halo's. The cap offset is applied
    // in screen space while `dist` (the along-line distance the dash is computed from) is a
    // per-vertex attribute that is not adjusted with it, so lengthening the cap stretches the
    // dash. Measured at the Gavarnie wall with the cap taken from the halo instead: the alpine
    // trail's own blue fell from 9 752 to 6 859 pixels, a 30 percent loss of body ink, while the
    // halo colour rose 11 percent. The halo's own caps are therefore the body's length rather
    // than its own width, which clips the halo only at the very end of a line - the ribbons are
    // densified every 12 metres (terrain_line_layout.hpp), so every interior segment end is
    // covered by its neighbour.
    const float capPx = props.half_px * widthScale;
    // Square caps extend the quad forward and back by the same half width (u_cap_px equals
    // u_half_px in the web engine, routes3d.js:1120-1121), so it is not a separate property;
    // capPx (the wider of the body's and the halo's own half width) is reused directly here
    // rather than adding one.
    const float2 offsetPx = n * (float(vertx.flag.y) * ext + props.rail_offset * widthScale) -
                            d * (float(vertx.flag.x) * capPx);

    p0.xy += (offsetPx / paintParams.units_to_pixels) * w0;
    // Pull the ribbon toward the camera by a small constant fraction of its own depth, so it
    // does not z-fight with the terrain surface it sits directly on top of.
    p0.z -= props.depth_bias * w0;

    // Task 2.2b: the distance fade, ported from the web vertex shader's v_fade (routes3d.js:
    // 388-391). Computed from `pos`, this vertex's own tile-local EXTENT position, before the
    // pixel-space offset above is applied - matching the web's use of a_pos.xy, its own
    // pre-offset mercator position. drawable.fade_k is 0 whenever terrain-line-fade-distance is
    // not positive (TerrainLineLayerTweaker::execute), which forces ft to 0 and fade to 1 - no
    // fade - regardless of props.fade, the same "far > 0" gate the web applies on the CPU side.
    const float ft = clamp(length(pos - drawable.fade_ref) * drawable.fade_k, 0.0, 1.0);
    const float fade = 1.0 - props.fade * ft * ft * (3.0 - 2.0 * ft);

    return {
        .position     = p0,
        .center       = center,
        .dist         = vertx.dist,
        .side         = float(vertx.flag.y),
        .half_px      = halfPx,
        .fade         = fade,
        .ext_px       = ext,
        .halo_half_px = haloHalfPx,
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

    // dash_period == 0 (an empty/undefined dasharray) means "draw solid". With no halo
    // (halo_half_px <= 0, the default) a gap still discards outright, byte-identical to before
    // this task. With a halo, the flat style draws a SOLID halo under a DASHED body, so a gap
    // must keep drawing the halo pass instead of discarding - dashGap only zeroes the body's own
    // contribution below (dashFactor), and the fragment is discarded later only if the resulting
    // composited alpha is actually zero.
    const bool dashGap = tileProps.dash_period > 0.0 && fract(in.dist / tileProps.dash_period) > tileProps.dash_on;
    if (dashGap && in.halo_half_px <= 0.0) {
        discard_fragment();
    }
    const float dashFactor = dashGap ? 0.0 : 1.0;

    // Task 2.2b: the distance fade multiplies alpha here, BEFORE the occlusion test below, matching
    // the web fragment shader's own ordering and its comment for why (routes3d.js:449-454): the
    // fade is applied to every pass this program draws so the whole ribbon recedes together, and a
    // ghosted (occluded-but-still-drawn) fragment must fade the same way a fully visible one does,
    // or a faded line would stop reading as the same line where it crosses behind a ridge.
    float alpha = props.opacity * in.fade;

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

    // `d` (the fragment's own distance from the centreline, in screen pixels) is measured against
    // in.ext_px, the quad's own true half-extent computed in the vertex stage above (the wider of
    // the body's and the halo's own half-width plus feather) - not in.half_px + props.edge_px,
    // which would clip a halo wider than the body's own coverage ramp. `a`, the body's own
    // coverage, is unchanged.
    const float d = abs(in.side) * in.ext_px;
    const float a = clamp((in.half_px - d) / props.edge_px + 0.5, 0.0, 1.0);

    // The halo's own coverage ramp, same shape as the body's, against its own half-width and its
    // own feather (props.halo_edge_px). ha stays 0 whenever there is no halo (in.halo_half_px <=
    // 0, the default), so it contributes nothing below.
    float ha = 0.0;
    if (in.halo_half_px > 0.0) {
        ha = clamp((in.halo_half_px - d) / max(props.halo_edge_px, 1e-4) + 0.5, 0.0, 1.0);
    }

    // One pass, not two: the body is composited over the halo here, standard over-compositing of
    // premultiplied values, rather than drawing a halo pass then a body pass - see this file's
    // top-of-file comment and terrain_line_layer_ubo.hpp's TerrainLineEvaluatedPropsUBO comment
    // for why that is exactly equivalent when the body colour is opaque, and avoids any
    // draw-order question. dashFactor zeroes the body's own contribution in a dash gap (so the
    // halo alone shows through, solid, exactly as the flat style's own dashed-body/solid-halo
    // look requires) without touching the halo's. With halo_half_px == 0, haloA is always 0 and
    // this reduces to exactly today's `props.color * (a * alpha)`.
    const float bodyA = a * alpha * dashFactor;
    const float haloA = ha * alpha;
    const float4 body = props.color * bodyA;
    const float4 halo = props.halo_color * haloA;
    const float4 result = body + halo * (1.0 - body.a);

    // A fully transparent fragment writes nothing rather than a premultiplied no-op blend. Gated
    // on the halo being active: without a halo this shader has always let a zero-coverage
    // fragment through to the blend as a no-op, and discarding it instead is a change (a discard
    // also skips the depth write) to a style that never asked for a halo.
    if (in.halo_half_px > 0.0 && result.a <= 0.0) {
        discard_fragment();
    }

    // Premultiplied alpha, matching the web shader's `fragColor = v_color * (a * alpha)` for the
    // no-halo case.
    return half4(result);
}
)";
};

} // namespace shaders
} // namespace mln
