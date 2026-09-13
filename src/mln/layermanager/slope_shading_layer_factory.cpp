#include <mln/layermanager/slope_shading_layer_factory.hpp>

#include <mln/renderer/layers/render_slope_shading_layer.hpp>
#include <mln/style/layers/slope_shading_layer.hpp>
#include <mln/style/layers/slope_shading_layer_impl.hpp>

namespace mln {

const style::LayerTypeInfo* SlopeShadingLayerFactory::getTypeInfo() const noexcept {
    return style::SlopeShadingLayer::Impl::staticTypeInfo();
}

std::unique_ptr<style::Layer> SlopeShadingLayerFactory::createLayer(
    const std::string& id, const style::conversion::Convertible&) noexcept {
    return std::unique_ptr<style::Layer>(new (std::nothrow) style::SlopeShadingLayer(id));
}

std::unique_ptr<RenderLayer> SlopeShadingLayerFactory::createRenderLayer(
    Immutable<style::Layer::Impl> impl) noexcept {
    assert(impl->getTypeInfo() == getTypeInfo());
    return std::unique_ptr<RenderLayer>(
        new (std::nothrow) RenderSlopeShadingLayer(staticImmutableCast<style::SlopeShadingLayer::Impl>(impl)));
}

} // namespace mln
