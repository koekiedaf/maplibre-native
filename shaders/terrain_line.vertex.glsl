// DuckMaps fork only. Ported from the web engine's routes3d.js VS (routes3d.js:375-391, quoted
// in full at docs/plans/grounding/ground-web-engine.md section 1). See
// include/mln/shaders/mtl/terrain_line.hpp for the full port commentary - this GL copy carries
// the same logic, expressed as std140 UBO blocks per this engine's own GL shader convention
// (color_relief.vertex.glsl / circle.vertex.glsl are the templates this follows).
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_other;
layout (location = 2) in vec2 a_flag;
layout (location = 3) in float a_dist;

layout (std140) uniform GlobalPaintParamsUBO {
    highp vec2 u_pattern_atlas_texsize;
    highp vec2 u_units_to_pixels;
    highp vec2 u_world_size;
    highp float u_camera_to_center_distance;
    highp float u_symbol_fade_change;
    highp float u_aspect_ratio;
    highp float u_pixel_ratio;
    highp float u_map_zoom;
    lowp float global_pad1;
    highp vec4 u_drape_tile;
};

// Vertex-only. Task 2.2c: dash_period/dash_on moved out of this block into
// TerrainLineTilePropsUBO (declared in the fragment shader only) - see
// include/mln/shaders/mtl/terrain_line.hpp's TerrainLineDrawableUBO comment for why the Metal
// fragment stage could not read them from here, which is the actual reason for the split. GL
// does not have that stage-binding restriction, but the tweaker that fills these buffers is
// shared across backends, so both backends' declarations follow the same struct layout.
layout (std140) uniform TerrainLineDrawableUBO {
    highp mat4 u_matrix;
    // 3D terrain elevation; see RenderTerrain::getTerrainData.
    highp vec4 u_dem_coords;
    highp vec4 u_dem_unpack;
    highp float u_dem_dim;
    highp float u_dem_exaggeration;
    lowp float u_dem_enabled;
    // Clip-space w at the ground under the map centre - see
    // TerrainLineLayerTweaker::computeReferenceClipW.
    highp float u_reference_w;
};

uniform sampler2D u_dem;

layout (std140) uniform TerrainLineEvaluatedPropsUBO {
    highp vec4 u_color;
    lowp float u_opacity;
    mediump float u_half_px;
    mediump float u_edge_px;
    mediump float u_rail_offset;
    highp float u_depth_bias;
    // 2.2b fields, unused by this shader so far.
    lowp float u_ghost_opacity;
    lowp float u_fade;
    highp float u_fade_distance;
    lowp float props_pad1;
    lowp float props_pad2;
    lowp float props_pad3;
    lowp float props_pad4;
};

out vec4 v_center;
out float v_dist;
out float v_side;
out float v_half_px;

void main() {
    // Both ends are sampled independently and elevated onto the terrain here - the whole point
    // of an elevated (renderToTerrain=false) layer type rather than a draped one.
    float eleA = get_elevation(a_pos, u_dem, u_dem_coords, u_dem_unpack, u_dem_dim, u_dem_exaggeration, u_dem_enabled);
    float eleB = get_elevation(a_other, u_dem, u_dem_coords, u_dem_unpack, u_dem_dim, u_dem_exaggeration, u_dem_enabled);

    vec4 p0 = u_matrix * vec4(a_pos, eleA, 1.0);
    vec4 p1 = u_matrix * vec4(a_other, eleB, 1.0);
    v_center = p0;

    float w0 = max(p0.w, 1e-4);
    float w1 = max(p1.w, 1e-4);

    // Screen-space (device pixel) direction/normal for this sub-segment. The web shader carries
    // a raw u_viewport uniform, multiplying by half the viewport size to convert into that pixel
    // space, then, after extruding, dividing back out by the same half-viewport factor to return
    // to clip space. u_units_to_pixels (GlobalPaintParamsUBO, bound to every shader already)
    // is exactly that same "0.5 * viewport" factor: it is 1 / PaintParameters::pixelsToGLUnits,
    // and pixelsToGLUnits is 2 / framebuffer size (paint_parameters.cpp). Dividing by it here and
    // multiplying by it again below reproduces the web's round trip with no separate uniform.
    vec2 s0 = (p0.xy / w0) * u_units_to_pixels;
    vec2 s1 = (p1.xy / w1) * u_units_to_pixels;
    vec2 d = s1 - s0;
    float len = length(d);
    d = len > 1e-3 ? d / len : vec2(1.0, 0.0);
    d *= a_flag.x;
    vec2 n = vec2(-d.y, d.x);

    // Task: width-shortfall investigation (12 Sept 2026, gavarnie pitch-0 body/halo
    // measurement). widthScale keeps the ribbon's on-screen width constant at the map centre
    // (where reference_w == w0) while it scales the width DOWN for anything farther from the
    // camera than the centre and UP for anything closer - a real physical-ribbon depth effect,
    // ported faithfully from the web's routes3d.js for its own interactively-drawn ROUTE
    // overlay (a real 3D object the user watches from the side, tilted, where that effect reads
    // as depth). It was never meant for, and the web never applies it to, the BASE trail/
    // waterway NETWORK - routes3d.js draws those with ordinary flat `line` layers, constant
    // width on screen regardless of position, same as every other cartographic line on this
    // map (roads, contours' own labels, etc).
    //
    // Every terrain-line layer this engine currently draws IS that base network (there is no
    // native drawn-route ribbon yet), so at the map centre's own elevation reference_w/w0 == 1
    // as intended, but a single frame with real relief (Gavarnie's cirque: ~1477-2608 m visible
    // at zoom 16 in the control run this comment was written against) puts most of the frame's
    // own trails at a meaningfully different elevation than the centre. At this zoom's ordinary
    // camera height (~1150 m above the centre's own terrain, unrelated to any camera bug -
    // confirmed unchanged under a 3000 m collision margin), that swings widthScale from about
    // 0.84x at the visible minimum elevation to about 4.8x at the visible maximum - a six-fold
    // range dwarfing the style curve's own ~50% growth from zoom 14 to 17. That is the entire
    // "nearly flat with zoom" symptom: the aggregate width any measurement reports is dominated
    // by which elevations happen to be on screen, not by the zoom-interpolated curve at all,
    // which a direct per-frame trace (widthPxStyle) confirmed is evaluated correctly, at the
    // live map zoom, every time.
    //
    // Fix: pin widthScale to 1.0 for the base network, matching the web's own flat rendering of
    // the identical layers and this engine's own non-terrain (engine=0) line layers exactly -
    // both already confirmed (this task's control measurement) to track the style curve
    // correctly. reference_w/computeReferenceClipW stay exactly as FAULT 2 fixed them: this is
    // not a math bug in that formula, it is that formula's real, correct output applied to a
    // family it was never meant to scale. The moment a genuine drawn-route ribbon layer exists,
    // THAT layer is the one to re-enable this on - not the base network - and it should get its
    // own opt-in rather than reviving this unconditional line.
    const float widthScale = 1.0;
    float halfPx = u_half_px * widthScale;
    float ext = halfPx + u_edge_px;
    // Square caps extend the quad forward/back by the same half width (u_cap_px == u_half_px in
    // the web engine, routes3d.js:1120-1121) - not a separate property, u_half_px reused here.
    vec2 offsetPx = n * (a_flag.y * ext + u_rail_offset * widthScale) - d * (a_flag.x * u_half_px * widthScale);

    p0.xy += (offsetPx / u_units_to_pixels) * w0;
    // Pull the ribbon toward the camera by a small constant fraction of its own depth, so it
    // doesn't z-fight with the terrain surface it sits directly on top of.
    p0.z -= u_depth_bias * w0;

    gl_Position = p0;
    v_dist = a_dist;
    v_side = a_flag.y;
    v_half_px = halfPx;
}
