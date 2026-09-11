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
    // Dash period/on-fraction for this drawable's tile, already converted to this tile's
    // EXTENT-unit distance space - see TerrainLineLayerTweaker's dash conversion comment.
    highp float u_dash_period;
    highp float u_dash_on;
    lowp float drawable_pad1;
    lowp float drawable_pad2;
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

    // widthScale keeps the ribbon's on-screen width constant at the map centre (where
    // reference_w == w0) while it scales naturally with depth away from it.
    float widthScale = max(u_reference_w / w0, 0.0);
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
