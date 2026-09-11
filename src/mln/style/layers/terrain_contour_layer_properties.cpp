// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/terrain_contour_layer_properties.hpp>

#include <mln/style/layers/terrain_contour_layer_impl.hpp>

namespace mln {
namespace style {

TerrainContourLayerProperties::TerrainContourLayerProperties(
    Immutable<TerrainContourLayer::Impl> impl_)
    : LayerProperties(std::move(impl_)) {}

TerrainContourLayerProperties::TerrainContourLayerProperties(
    Immutable<TerrainContourLayer::Impl> impl_,
    TerrainContourPaintProperties::PossiblyEvaluated evaluated_)
  : LayerProperties(std::move(impl_)),
    evaluated(std::move(evaluated_)) {}

TerrainContourLayerProperties::~TerrainContourLayerProperties() = default;

unsigned long TerrainContourLayerProperties::constantsMask() const {
    return evaluated.constantsMask();
}

const TerrainContourLayer::Impl& TerrainContourLayerProperties::layerImpl() const noexcept {
    return static_cast<const TerrainContourLayer::Impl&>(*baseImpl);
}

expression::Dependency TerrainContourLayerProperties::getDependencies() const noexcept {
    return layerImpl().paint.getDependencies();
}

} // namespace style
} // namespace mln

// clang-format on
