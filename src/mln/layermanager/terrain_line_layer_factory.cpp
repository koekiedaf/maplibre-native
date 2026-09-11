#include <mln/layermanager/terrain_line_layer_factory.hpp>

#include <mln/layout/terrain_line_layout.hpp>
#include <mln/renderer/layers/render_terrain_line_layer.hpp>
#include <mln/style/layers/terrain_line_layer.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>

namespace mln {

const style::LayerTypeInfo* TerrainLineLayerFactory::getTypeInfo() const noexcept {
    return style::TerrainLineLayer::Impl::staticTypeInfo();
}

std::unique_ptr<style::Layer> TerrainLineLayerFactory::createLayer(
    const std::string& id, const style::conversion::Convertible& value) noexcept {
    const auto source = getSource(value);
    return std::unique_ptr<style::Layer>(source ? new (std::nothrow) style::TerrainLineLayer(id, *source) : nullptr);
}

std::unique_ptr<Layout> TerrainLineLayerFactory::createLayout(
    const LayoutParameters& parameters,
    std::unique_ptr<GeometryTileLayer> layer,
    const std::vector<Immutable<style::LayerProperties>>& group) {
    return std::unique_ptr<Layout>(new (std::nothrow)
                                       TerrainLineLayout(parameters.bucketParameters, group, std::move(layer)));
}

std::unique_ptr<RenderLayer> TerrainLineLayerFactory::createRenderLayer(Immutable<style::Layer::Impl> impl) noexcept {
    assert(impl->getTypeInfo() == getTypeInfo());
    return std::unique_ptr<RenderLayer>(
        new (std::nothrow) RenderTerrainLineLayer(staticImmutableCast<style::TerrainLineLayer::Impl>(impl)));
}

} // namespace mln
