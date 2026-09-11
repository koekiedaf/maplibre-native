// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/terrain_line_layer_properties.hpp>

#include <mln/style/layers/terrain_line_layer_impl.hpp>

namespace mln {
namespace style {

TerrainLineLayerProperties::TerrainLineLayerProperties(
    Immutable<TerrainLineLayer::Impl> impl_)
    : LayerProperties(std::move(impl_)) {}

TerrainLineLayerProperties::TerrainLineLayerProperties(
    Immutable<TerrainLineLayer::Impl> impl_,
    TerrainLinePaintProperties::PossiblyEvaluated evaluated_)
  : LayerProperties(std::move(impl_)),
    evaluated(std::move(evaluated_)) {}

TerrainLineLayerProperties::~TerrainLineLayerProperties() = default;

unsigned long TerrainLineLayerProperties::constantsMask() const {
    return evaluated.constantsMask();
}

const TerrainLineLayer::Impl& TerrainLineLayerProperties::layerImpl() const noexcept {
    return static_cast<const TerrainLineLayer::Impl&>(*baseImpl);
}

expression::Dependency TerrainLineLayerProperties::getDependencies() const noexcept {
    return layerImpl().paint.getDependencies();
}

} // namespace style
} // namespace mln

// clang-format on
