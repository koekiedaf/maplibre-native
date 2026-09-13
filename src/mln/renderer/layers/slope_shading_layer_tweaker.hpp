#pragma once

#include <mln/gfx/context.hpp>
#include <mln/renderer/layer_tweaker.hpp>

#include <cstdint>
#include <string>

namespace mln {

// DuckMaps fork only, task 2.6.
class SlopeShadingLayerTweaker : public LayerTweaker {
public:
    SlopeShadingLayerTweaker(std::string id_, Immutable<style::LayerProperties> properties)
        : LayerTweaker(std::move(id_), properties) {}
    ~SlopeShadingLayerTweaker() override = default;

    void execute(LayerGroupBase&, const PaintParameters&) override;

protected:
    gfx::UniformBufferPtr evaluatedPropsUniformBuffer;

#if MLN_UBO_CONSOLIDATION
    gfx::UniformBufferPtr drawableUniformBuffer;
    gfx::UniformBufferPtr tilePropsUniformBuffer;
#endif

    // The 256x256 slope/aspect lookup texture (see slope_shading_layer_ubo.hpp's own comment and
    // contours3d.js's buildLut()/zoneAt(), which this ports), rebuilt only when the selected
    // preset actually changes - cheap either way (one CPU fill of 256*256*4 bytes and one texture
    // upload), but there is no reason to redo it every frame.
    gfx::Texture2DPtr lutTexture;
    int cachedPresetId = -1;
};

} // namespace mln
