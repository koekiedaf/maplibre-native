// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#pragma once

#include <mln/style/types.hpp>
#include <mln/style/layer_properties.hpp>
#include <mln/style/layers/terrain_contour_layer.hpp>
#include <mln/style/layout_property.hpp>
#include <mln/style/paint_property.hpp>
#include <mln/style/properties.hpp>
#include <mln/shaders/attributes.hpp>
#include <mln/shaders/uniforms.hpp>

namespace mln {
namespace style {

struct TerrainContourFadeHi : PaintProperty<float> {
    static float defaultValue() { return 2.2f; }
};

struct TerrainContourFadeLo : PaintProperty<float> {
    static float defaultValue() { return 0.6f; }
};

struct TerrainContourIndexColor : PaintProperty<Color> {
    static Color defaultValue() { return { 0.792156862745098, 0.6549019607843137, 0.5058823529411764, 1 }; }
};

struct TerrainContourIndexInterval : PaintProperty<float> {
    static float defaultValue() { return 100.f; }
};

struct TerrainContourIndexOpacity : PaintProperty<float> {
    static float defaultValue() { return 1.f; }
};

struct TerrainContourIndexWidth : PaintProperty<float> {
    static float defaultValue() { return 2.1f; }
};

struct TerrainContourMinorColor : PaintProperty<Color> {
    static Color defaultValue() { return { 0.7803921568627451, 0.6352941176470588, 0.4196078431372549, 1 }; }
};

struct TerrainContourMinorInterval : PaintProperty<float> {
    static float defaultValue() { return 20.f; }
};

struct TerrainContourMinorOpacity : PaintProperty<float> {
    static float defaultValue() { return 1.f; }
};

struct TerrainContourMinorWidth : PaintProperty<float> {
    static float defaultValue() { return 1.3f; }
};

class TerrainContourPaintProperties : public Properties<
    TerrainContourFadeHi,
    TerrainContourFadeLo,
    TerrainContourIndexColor,
    TerrainContourIndexInterval,
    TerrainContourIndexOpacity,
    TerrainContourIndexWidth,
    TerrainContourMinorColor,
    TerrainContourMinorInterval,
    TerrainContourMinorOpacity,
    TerrainContourMinorWidth
> {};

class TerrainContourLayerProperties final : public LayerProperties {
public:
    explicit TerrainContourLayerProperties(Immutable<TerrainContourLayer::Impl>);
    TerrainContourLayerProperties(
        Immutable<TerrainContourLayer::Impl>,
        TerrainContourPaintProperties::PossiblyEvaluated);
    ~TerrainContourLayerProperties() override;

    unsigned long constantsMask() const override;

    expression::Dependency getDependencies() const noexcept override;

    const TerrainContourLayer::Impl& layerImpl() const noexcept;
    // Data members.
    TerrainContourPaintProperties::PossiblyEvaluated evaluated;
};

} // namespace style
} // namespace mln

// clang-format on
