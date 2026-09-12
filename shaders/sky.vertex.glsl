// DuckMaps fork only, task T3: the style spec's `sky` root property
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
