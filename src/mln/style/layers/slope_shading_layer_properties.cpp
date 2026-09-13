// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/slope_shading_layer_properties.hpp>

#include <mln/style/layers/slope_shading_layer_impl.hpp>

namespace mln {
namespace style {

SlopeShadingLayerProperties::SlopeShadingLayerProperties(
    Immutable<SlopeShadingLayer::Impl> impl_)
    : LayerProperties(std::move(impl_)) {}

SlopeShadingLayerProperties::SlopeShadingLayerProperties(
    Immutable<SlopeShadingLayer::Impl> impl_,
    SlopeShadingPaintProperties::PossiblyEvaluated evaluated_)
  : LayerProperties(std::move(impl_)),
    evaluated(std::move(evaluated_)) {}

SlopeShadingLayerProperties::~SlopeShadingLayerProperties() = default;

unsigned long SlopeShadingLayerProperties::constantsMask() const {
    return evaluated.constantsMask();
}

const SlopeShadingLayer::Impl& SlopeShadingLayerProperties::layerImpl() const noexcept {
    return static_cast<const SlopeShadingLayer::Impl&>(*baseImpl);
}

expression::Dependency SlopeShadingLayerProperties::getDependencies() const noexcept {
    return layerImpl().paint.getDependencies();
}

} // namespace style
} // namespace mln

// clang-format on
