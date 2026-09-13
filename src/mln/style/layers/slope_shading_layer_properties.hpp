// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#pragma once

#include <mln/style/types.hpp>
#include <mln/style/layer_properties.hpp>
#include <mln/style/layers/slope_shading_layer.hpp>
#include <mln/style/layout_property.hpp>
#include <mln/style/paint_property.hpp>
#include <mln/style/properties.hpp>
#include <mln/shaders/attributes.hpp>
#include <mln/shaders/uniforms.hpp>

namespace mln {
namespace style {

struct SlopeShadingOpacity : PaintProperty<float> {
    static float defaultValue() { return 0.55f; }
};

struct SlopeShadingPreset : PaintProperty<float> {
    static float defaultValue() { return 0.f; }
};

class SlopeShadingPaintProperties : public Properties<
    SlopeShadingOpacity,
    SlopeShadingPreset
> {};

class SlopeShadingLayerProperties final : public LayerProperties {
public:
    explicit SlopeShadingLayerProperties(Immutable<SlopeShadingLayer::Impl>);
    SlopeShadingLayerProperties(
        Immutable<SlopeShadingLayer::Impl>,
        SlopeShadingPaintProperties::PossiblyEvaluated);
    ~SlopeShadingLayerProperties() override;

    unsigned long constantsMask() const override;

    expression::Dependency getDependencies() const noexcept override;

    const SlopeShadingLayer::Impl& layerImpl() const noexcept;
    // Data members.
    SlopeShadingPaintProperties::PossiblyEvaluated evaluated;
};

} // namespace style
} // namespace mln

// clang-format on
