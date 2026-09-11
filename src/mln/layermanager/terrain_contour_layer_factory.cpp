#include <mln/layermanager/terrain_contour_layer_factory.hpp>

#include <mln/renderer/layers/render_terrain_contour_layer.hpp>
#include <mln/style/layers/terrain_contour_layer.hpp>
#include <mln/style/layers/terrain_contour_layer_impl.hpp>

namespace mln {

const style::LayerTypeInfo* TerrainContourLayerFactory::getTypeInfo() const noexcept {
    return style::TerrainContourLayer::Impl::staticTypeInfo();
}

std::unique_ptr<style::Layer> TerrainContourLayerFactory::createLayer(
    const std::string& id, const style::conversion::Convertible&) noexcept {
    return std::unique_ptr<style::Layer>(new (std::nothrow) style::TerrainContourLayer(id));
}

std::unique_ptr<RenderLayer> TerrainContourLayerFactory::createRenderLayer(
    Immutable<style::Layer::Impl> impl) noexcept {
    assert(impl->getTypeInfo() == getTypeInfo());
    return std::unique_ptr<RenderLayer>(
        new (std::nothrow) RenderTerrainContourLayer(staticImmutableCast<style::TerrainContourLayer::Impl>(impl)));
}

} // namespace mln
