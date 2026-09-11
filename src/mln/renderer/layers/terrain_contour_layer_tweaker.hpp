#pragma once

#include <mln/renderer/layer_tweaker.hpp>

#include <string>

namespace mln {

// DuckMaps fork only, task 2.4a.
class TerrainContourLayerTweaker : public LayerTweaker {
public:
    TerrainContourLayerTweaker(std::string id_, Immutable<style::LayerProperties> properties)
        : LayerTweaker(std::move(id_), properties) {}
    ~TerrainContourLayerTweaker() override = default;

    void execute(LayerGroupBase&, const PaintParameters&) override;

protected:
    gfx::UniformBufferPtr evaluatedPropsUniformBuffer;

#if MLN_UBO_CONSOLIDATION
    gfx::UniformBufferPtr drawableUniformBuffer;
#endif
};

} // namespace mln
