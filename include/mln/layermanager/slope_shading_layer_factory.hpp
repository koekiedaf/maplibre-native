#pragma once

#include <mln/layermanager/layer_factory.hpp>

namespace mln {

// DuckMaps fork only, task 2.6. No source, no bucket/layout - same shape as
// TerrainContourLayerFactory (include/mln/layermanager/terrain_contour_layer_factory.hpp), which
// is itself modelled on BackgroundLayerFactory: only getTypeInfo/createLayer/createRenderLayer,
// no createLayout override.
class SlopeShadingLayerFactory : public LayerFactory {
protected:
    const style::LayerTypeInfo* getTypeInfo() const noexcept final;
    std::unique_ptr<style::Layer> createLayer(const std::string& id,
                                              const style::conversion::Convertible& value) noexcept final;
    std::unique_ptr<RenderLayer> createRenderLayer(Immutable<style::Layer::Impl>) noexcept final;
};

} // namespace mln
