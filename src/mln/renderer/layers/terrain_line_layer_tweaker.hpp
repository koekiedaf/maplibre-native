#pragma once

#include <mln/renderer/layer_tweaker.hpp>

#include <string>
#include <vector>

namespace mln {

struct DebugDrawableUBOEntry;

// DuckMaps fork only. Which pass a terrain-line drawable belongs to - stored via
// gfx::Drawable::setType/getType, the same generic per-drawable "which variant" slot
// fill/line/location-indicator layers already use for their own multi-drawable-per-feature
// passes (see e.g. render_fill_layer.cpp's FillVariant). RenderTerrainLineLayer::update() sets
// this when it builds a tile's drawables; TerrainLineLayerTweaker::execute() reads it back to
// fill each drawable's own halo_pass UBO flag (terrain_line_layer_ubo.hpp) - see that file's
// comment for why the flag has to be duplicated into two UBOs rather than read from one.
enum class TerrainLinePassType : uint8_t {
    Body = 0,
    Halo = 1,
};

// DuckMaps fork only.
class TerrainLineLayerTweaker : public LayerTweaker {
public:
    TerrainLineLayerTweaker(std::string id_, Immutable<style::LayerProperties> properties)
        : LayerTweaker(std::move(id_), properties) {}
    ~TerrainLineLayerTweaker() override = default;

    void execute(LayerGroupBase&, const PaintParameters&) override;

    // DuckMaps fork only, task C7: terrain-line's own half of the per-drawable UBO/texture
    // trace - see TerrainContourLayerTweaker::debugDrainContourDrawableUBOEntries's comment for
    // the full contract (same DUCKMAPS_ELEVATION_TRACE guard, same shape, drained once per frame
    // by Renderer::Impl::render). Every entry's "pass" is getDrawPriority(), which already
    // separates a tile's halo drawable from its body drawable (see render_terrain_line_layer.cpp:
    // TerrainLineHaloDrawPriority/TerrainLineBodyDrawPriority differ by exactly one
    // kTileDrawOrderPassStride), so the same tile id legitimately appears twice - that is not a
    // duplicate, it is the two passes.
    static std::vector<DebugDrawableUBOEntry> debugDrainLineDrawableUBOEntries();

protected:
    gfx::UniformBufferPtr evaluatedPropsUniformBuffer;

#if MLN_UBO_CONSOLIDATION
    gfx::UniformBufferPtr drawableUniformBuffer;
    // Fragment-only per-tile data (dash_period/dash_on), bound at
    // idDrawableReservedFragmentOnlyUBO - see TerrainLineDrawableUBO's comment in
    // terrain_line_layer_ubo.hpp for why this is a separate buffer from drawableUniformBuffer
    // rather than the fragment stage reading that one.
    gfx::UniformBufferPtr tilePropsUniformBuffer;
#endif
};

} // namespace mln
