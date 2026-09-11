// DuckMaps fork only. Ported from the web engine's routes3d.js FS (routes3d.js:392-422, quoted
// in full at docs/plans/grounding/ground-web-engine.md section 1), first-cut subset: elevation,
// width/caps via reference w, the AA edge feather, colour, opacity, the dash discard, the rail
// offset and the depth bias. Left out, deferred to 2.2b: the terrain depth-texture occlusion
// test, ghosting, distance fade, every debug early-out. No depth texture is bound here.
layout (std140) uniform TerrainLineDrawableUBO {
    highp mat4 u_matrix;
    highp vec4 u_dem_coords;
    highp vec4 u_dem_unpack;
    highp float u_dem_dim;
    highp float u_dem_exaggeration;
    lowp float u_dem_enabled;
    highp float u_reference_w;
    highp float u_dash_period;
    highp float u_dash_on;
    lowp float drawable_pad1;
    lowp float drawable_pad2;
};

layout (std140) uniform TerrainLineEvaluatedPropsUBO {
    highp vec4 u_color;
    lowp float u_opacity;
    mediump float u_half_px;
    mediump float u_edge_px;
    mediump float u_rail_offset;
    highp float u_depth_bias;
    lowp float u_ghost_opacity;
    lowp float u_fade;
    highp float u_fade_distance;
    lowp float props_pad1;
    lowp float props_pad2;
    lowp float props_pad3;
    lowp float props_pad4;
};

in vec4 v_center;
in float v_dist;
in float v_side;
in float v_half_px;

void main() {
    // dash_period == 0 (an empty/undefined dasharray) means "draw solid".
    if (u_dash_period > 0.0 && fract(v_dist / u_dash_period) > u_dash_on) discard;

    // 2.2b adds the terrain depth-texture occlusion test and the distance fade here, both keyed
    // off v_center and u_ghost_opacity/u_fade/u_fade_distance - neither is read in this cut.
    float alpha = u_opacity;

    float d = abs(v_side) * (v_half_px + u_edge_px);
    float a = clamp((v_half_px - d) / u_edge_px + 0.5, 0.0, 1.0);

    // Premultiplied alpha, matching the web shader's `fragColor = v_color * (a * alpha)`.
    fragColor = u_color * (a * alpha);

#ifdef OVERDRAW_INSPECTOR
    fragColor = vec4(1.0);
#endif
}
