// Generated code, do not modify this file!
#pragma once
#include <mln/shaders/shader_source.hpp>

namespace mln {
namespace shaders {

template <>
struct ShaderSource<BuiltIn::SkyShader, gfx::Backend::Type::OpenGL> {
    static constexpr const char* name = "SkyShader";
    static constexpr const char* vertex = R"(// DuckMaps fork only, task T3: the style spec's `sky` root property
// (https://maplibre.org/maplibre-style-spec/sky/). Ported verbatim from maplibre-gl-js's own
// sky vertex shader (vendored at
// container/server/app/map/vendor/maplibre-gl-6.mjs in the server checkout) - see
// include/mln/shaders/mtl/sky.hpp for the full port commentary and the GL-to-Metal fragment
// coordinate conversion this task made (this vertex shader needs no such conversion: it is
// already backend-agnostic clip space).
//
// UNTESTED: this fork's GL/drawable renderer is not built for the iOS Simulator target this
// task builds against (see the task's own final report) - like terrain_contour.vertex.glsl
// before it, this file exists only so the tree's own shader-source convention (a GL header
// alongside the Metal one) stays complete; SkyShader is registered only with the Metal backend
// (src/mln/mtl/renderer_backend.cpp), never GL.
layout (location = 0) in vec2 a_pos;

void main() {
    gl_Position = vec4(a_pos, 1.0, 1.0);
}
)";
    static constexpr const char* fragment = R"(// DuckMaps fork only, task T3: the style spec's `sky` root property
// (https://maplibre.org/maplibre-style-spec/sky/). Ported verbatim from maplibre-gl-js's own
// sky fragment shader (vendored at
// container/server/app/map/vendor/maplibre-gl-6.mjs in the server checkout) - see
// include/mln/shaders/mtl/sky.hpp for the full port commentary.
//
// This GL copy is the TRUE verbatim port: gl_FragCoord's origin is bottom-left with y growing
// upward, exactly the convention this formula was written for, so unlike the Metal port
// (include/mln/shaders/mtl/sky.hpp) no axis conversion is needed here.
//
// UNTESTED: this fork's GL/drawable renderer is not built for the iOS Simulator target this
// task builds against (see the task's own final report) - like terrain_contour.fragment.glsl
// before it, this file exists only so the tree's own shader-source convention stays complete;
// SkyShader is registered only with the Metal backend (src/mln/mtl/renderer_backend.cpp).
uniform vec4 u_sky_color;
uniform vec4 u_horizon_color;
uniform vec2 u_horizon;
uniform vec2 u_horizon_normal;
uniform float u_sky_horizon_blend;
uniform float u_sky_blend;

void main() {
    float x = gl_FragCoord.x;
    float y = gl_FragCoord.y;
    float blend = (y - u_horizon.y) * u_horizon_normal.y + (x - u_horizon.x) * u_horizon_normal.x;
    if (blend > 0.0) {
        if (blend < u_sky_horizon_blend) {
            fragColor = mix(u_sky_color, u_horizon_color, pow(1.0 - blend / u_sky_horizon_blend, 2.0));
        } else {
            fragColor = u_sky_color;
        }
    }
    fragColor = mix(fragColor, vec4(vec3(0.0), 0.0), u_sky_blend);
}
)";
};

} // namespace shaders
} // namespace mln
