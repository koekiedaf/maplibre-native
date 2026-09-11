// DuckMaps fork only, task 2.4a. Ported from the web engine's contours3d.js VS
// (contours3d.js:179-199) - see include/mln/shaders/mtl/terrain_contour.hpp for the full port
// commentary; this GL copy carries the same logic, expressed as std140 UBO blocks per this
// engine's own GL shader convention (terrain_line.vertex.glsl is the closest template: another
// elevated, non-draped layer type sampling the terrain DEM in its own vertex shader).
//
// UNTESTED: this fork's GL/drawable renderer is not built for the iOS Simulator target this
// task builds against, so this file (and its fragment counterpart) could not be compiled or
// exercised here - see the task's own final report.
layout (location = 0) in vec4 a_pos;

layout (std140) uniform TerrainContourDrawableUBO {
    highp mat4 u_matrix;
    highp vec4 u_dem_coords;
    highp vec4 u_dem_unpack;
    highp float u_dem_dim;
    highp float u_dem_exaggeration;
    lowp float u_dem_enabled;
    // Clip-space w at the ground under the map centre - see
    // TerrainContourLayerTweaker::computeReferenceClipW.
    highp float u_reference_w;
};

uniform sampler2D u_dem;

layout (std140) uniform TerrainContourEvaluatedPropsUBO {
    highp vec4 u_minor_color;
    highp vec4 u_index_color;
    highp float u_minor_interval;
    highp float u_index_interval;
    mediump float u_minor_width;
    mediump float u_index_width;
    mediump float u_fade_lo;
    mediump float u_fade_hi;
    highp float u_depth_bias;
    highp float u_slope_bias;
};

out vec2 v_pos_extent;
out float v_skirt;

void main() {
    vec2 pos = a_pos.xy;
    float elevation = get_elevation(pos, u_dem, u_dem_coords, u_dem_unpack, u_dem_dim, u_dem_exaggeration, u_dem_enabled);

    vec4 p = u_matrix * vec4(pos, elevation, 1.0);
    // This mesh sits exactly on RenderTerrain's own terrain mesh (the SAME shared vertex/index
    // buffers), so a small constant pull toward the camera in NDC z, scaled by w, wins the
    // depth test against it - matches contours3d.js's own DEPTH_BIAS (0.0003).
    p.z -= u_depth_bias * p.w;

    gl_Position = p;
    v_pos_extent = pos;
    v_skirt = a_pos.z;
}
