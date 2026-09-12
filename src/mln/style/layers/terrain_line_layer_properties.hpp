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
    static float defaultValue() { return 0.5f; }
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
    static float defaultValue() { return 0.5f; }
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
