#pragma once

#include <mln/layermanager/layer_factory.hpp>

namespace mln {

// DuckMaps fork only, task 2.4a. No source, no bucket/layout - modelled on
// BackgroundLayerFactory (include/mln/layermanager/background_layer_factory.hpp): only
// getTypeInfo/createLayer/createRenderLayer, no createLayout override.
class TerrainContourLayerFactory : public LayerFactory {
protected:
    const style::LayerTypeInfo* getTypeInfo() const noexcept final;
    std::unique_ptr<style::Layer> createLayer(const std::string& id,
                                              const style::conversion::Convertible& value) noexcept final;
    std::unique_ptr<RenderLayer> createRenderLayer(Immutable<style::Layer::Impl>) noexcept final;
};

} // namespace mln
