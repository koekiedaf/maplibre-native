// Generated code, do not modify this file!
#pragma once
#include <mln/shaders/shader_source.hpp>

namespace mln {
namespace shaders {

template <>
struct ShaderSource<BuiltIn::TerrainContourShader, gfx::Backend::Type::OpenGL> {
    static constexpr const char* name = "TerrainContourShader";
    static constexpr const char* vertex = R"(// DuckMaps fork only, task 2.4a. Ported from the web engine's contours3d.js VS
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
)";
    static constexpr const char* fragment = R"(// DuckMaps fork only, task 2.4a. Ported line for line from the web engine's contours3d.js FS
// (contours3d.js:200-303) - see include/mln/shaders/mtl/terrain_contour.hpp for the full port
// commentary (the DEM padding convention, widthScale/gl_FragCoord.w, the coverage()/density()
// early-outs).
//
// UNTESTED: this fork's GL/drawable renderer is not built for the iOS Simulator target this
// task builds against, so this file (and its vertex counterpart) could not be compiled or
// exercised here - see the task's own final report.
layout (std140) uniform TerrainContourDrawableUBO {
    highp mat4 u_matrix;
    highp vec4 u_dem_coords;
    highp vec4 u_dem_unpack;
    highp float u_dem_dim;
    highp float u_dem_exaggeration;
    lowp float u_dem_enabled;
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

in vec2 v_pos_extent;
in float v_skirt;

// contours3d.js distToLine()/coverage()/density(), ported line for line.
float terrain_contour_dist_to_line(float v, float i) {
    float m = v / i;
    float f = fract(m);
    return min(f, 1.0 - f);
}

// grad's `< 1e-4` early-out matters: without it, perfectly flat ground (a lake, or the ocean at
// elevation 0, itself an exact multiple of every interval) reports a fake zero-width crossing
// and washes solid colour over the whole flat area instead of drawing nothing.
float terrain_contour_coverage(float v, float i, float halfPx, float grad) {
    if (grad < 1e-4) return 0.0;
    float aa = grad * halfPx;
    return 1.0 - smoothstep(0.0, aa, terrain_contour_dist_to_line(v, i));
}

float terrain_contour_density(float grad, float fadeLo, float fadeHi) {
    return smoothstep(fadeLo, fadeHi, 1.0 / max(grad, 1e-6));
}

void main() {
    // Slope-scaled depth bias (contours3d.js SLOPE_BIAS = 8.0), written unconditionally before
    // any discard - see contours3d.js's own comment on this exact requirement.
    gl_FragDepth = gl_FragCoord.z - u_slope_bias * fwidth(gl_FragCoord.z);

    // Skirt curtains hang below the surface to hide cracks between neighbouring terrain tiles;
    // a contour line has no business being drawn on a near-vertical hidden curtain (not in the
    // web source, which has no skirt geometry at all - a judgment call made porting onto this
    // engine's own terrain mesh, see include/mln/shaders/mtl/terrain_contour.hpp's comment).
    if (v_skirt > 0.5) discard;

    float e = get_elevation(v_pos_extent, u_dem, u_dem_coords, u_dem_unpack, u_dem_dim, u_dem_exaggeration, u_dem_enabled);

    // gl_FragCoord.w is 1/w_clip, the same quantity Metal's fragment [[position]].w carries.
    float widthScale = max(u_reference_w * gl_FragCoord.w, 0.0);

    float gradIndex = u_index_interval > 0.0 ? fwidth(e / u_index_interval) : 0.0;
    float gradMinor = u_minor_interval > 0.0 ? fwidth(e / u_minor_interval) : 0.0;

    float indexCov = u_index_interval > 0.0
        ? terrain_contour_coverage(e, u_index_interval, u_index_width * widthScale, gradIndex) *
          terrain_contour_density(gradIndex, u_fade_lo, u_fade_hi)
        : 0.0;
    float minorCov = u_minor_interval > 0.0
        ? terrain_contour_coverage(e, u_minor_interval, u_minor_width * widthScale, gradMinor) *
          terrain_contour_density(gradMinor, u_fade_lo, u_fade_hi)
        : 0.0;

    // Index wins over minor, exactly as contours3d.js's render() tests index before minor.
    if (indexCov > 0.01) {
        fragColor = u_index_color * indexCov;
    } else if (minorCov > 0.01) {
        fragColor = u_minor_color * minorCov;
    } else {
        discard;
    }

#ifdef OVERDRAW_INSPECTOR
    fragColor = vec4(1.0);
#endif
}
)";
};

} // namespace shaders
} // namespace mln
