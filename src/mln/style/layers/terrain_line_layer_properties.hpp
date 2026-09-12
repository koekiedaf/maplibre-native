// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#pragma once

#include <mln/style/types.hpp>
#include <mln/style/layer_properties.hpp>
#include <mln/style/layers/terrain_line_layer.hpp>
#include <mln/style/layout_property.hpp>
#include <mln/style/paint_property.hpp>
#include <mln/style/properties.hpp>
#include <mln/shaders/attributes.hpp>
#include <mln/shaders/uniforms.hpp>

namespace mln {
namespace style {

struct TerrainLineBlur : PaintProperty<float> {
    // DuckMaps fork only, task: feather-vs-geometry settlement (12 Sept 2026). Used to default to
    // 0.5 CSS px, a fixed compromise chosen because a style-spec default cannot read the device's
    // own pixelRatio (see the now-superseded comment this replaced in scripts/style-spec.mjs) - at
    // dpr 3 (an iPhone) that fixed CSS-point default rendered as 1.5 DEVICE pixels of feather,
    // three times the ordinary flat `line` layer's own antialiasing edge (line.vertex.glsl's
    // ANTIALIASING = 1.0 / DEVICE_PIXEL_RATIO / 2.0, pinned to half a device pixel at ANY
    // pixelRatio). The default is now 0, matching LineBlur's own default exactly: the always-on,
    // pixelRatio-correct antialiasing minimum is added at RUNTIME instead, in
    // TerrainLineLayerTweaker::execute (terrain_line_layer_tweaker.cpp), which - unlike this
    // style-spec value - DOES know the live frame's pixelRatio. This property now means exactly
    // what terrain-line-width means for its own concern: a deliberate, additional amount of blur
    // a style author asks for on top of that always-on minimum, never a way to control the
    // minimum itself.
    static float defaultValue() { return 0.0f; }
};

struct TerrainLineColor : PaintProperty<Color> {
    static Color defaultValue() { return Color::black(); }
};

struct TerrainLineDasharray : PaintProperty<std::vector<float>> {
    static std::vector<float> defaultValue() { return {0.f, 0.f}; }
};

struct TerrainLineFade : PaintProperty<float> {
    static float defaultValue() { return 0.f; }
};

struct TerrainLineFadeDistance : PaintProperty<float> {
    static float defaultValue() { return 8000.f; }
};

struct TerrainLineGhostOpacity : PaintProperty<float> {
    static float defaultValue() { return 0.f; }
};

struct TerrainLineHaloBlur : PaintProperty<float> {
    // DuckMaps fork only, task: feather-vs-geometry settlement (12 Sept 2026) - same fix, same
    // reasoning as TerrainLineBlur::defaultValue() above: the always-on, pixelRatio-correct
    // antialiasing minimum moved to the tweaker; this default is now 0, a pure additional-blur
    // knob.
    static float defaultValue() { return 0.0f; }
};

struct TerrainLineHaloColor : PaintProperty<Color> {
    static Color defaultValue() { return Color::black(); }
};

struct TerrainLineHaloWidth : PaintProperty<float> {
    static float defaultValue() { return 0.f; }
};

struct TerrainLineOffset : PaintProperty<float> {
    static float defaultValue() { return 0.f; }
};

struct TerrainLineOpacity : PaintProperty<float> {
    static float defaultValue() { return 1.f; }
};

struct TerrainLineWidth : PaintProperty<float> {
    static float defaultValue() { return 1.f; }
};

class TerrainLinePaintProperties : public Properties<
    TerrainLineBlur,
    TerrainLineColor,
    TerrainLineDasharray,
    TerrainLineFade,
    TerrainLineFadeDistance,
    TerrainLineGhostOpacity,
    TerrainLineHaloBlur,
    TerrainLineHaloColor,
    TerrainLineHaloWidth,
    TerrainLineOffset,
    TerrainLineOpacity,
    TerrainLineWidth
> {};

class TerrainLineLayerProperties final : public LayerProperties {
public:
    explicit TerrainLineLayerProperties(Immutable<TerrainLineLayer::Impl>);
    TerrainLineLayerProperties(
        Immutable<TerrainLineLayer::Impl>,
        TerrainLinePaintProperties::PossiblyEvaluated);
    ~TerrainLineLayerProperties() override;

    unsigned long constantsMask() const override;

    expression::Dependency getDependencies() const noexcept override;

    const TerrainLineLayer::Impl& layerImpl() const noexcept;
    // Data members.
    TerrainLinePaintProperties::PossiblyEvaluated evaluated;
};

} // namespace style
} // namespace mln

// clang-format on
