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
    // Fragment-only per-tile data (dem_* + reference_w), bound at idDrawableReservedFragmentOnlyUBO
    // - see TerrainContourDrawableUBO's comment in terrain_contour_layer_ubo.hpp for why this is
    // a separate buffer from drawableUniformBuffer rather than the fragment stage reading that one.
    gfx::UniformBufferPtr tilePropsUniformBuffer;
#endif
};

} // namespace mln
