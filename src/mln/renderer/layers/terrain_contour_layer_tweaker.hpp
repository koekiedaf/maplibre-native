#pragma once

#include <mln/renderer/layer_tweaker.hpp>

#include <string>
#include <vector>

namespace mln {

struct DebugDrawableUBOEntry;

// DuckMaps fork only, task 2.4a.
class TerrainContourLayerTweaker : public LayerTweaker {
public:
    TerrainContourLayerTweaker(std::string id_, Immutable<style::LayerProperties> properties)
        : LayerTweaker(std::move(id_), properties) {}
    ~TerrainContourLayerTweaker() override = default;

    void execute(LayerGroupBase&, const PaintParameters&) override;

    // DuckMaps fork only, task C6: debug-only, off-by-default trace of this tweaker's own
    // per-frame reference-w computation (DUCKMAPS_ELEVATION_TRACE), drained by
    // Renderer::Impl::render exactly like DEMElevationProvider::debugDrainElevationQueries.
    // Returns "null" (not queried, no allocation) when the trace is off; costs one static
    // bool check per execute() call otherwise, and nothing when disabled.
    static std::string debugDrainContourReferenceTraceJSON();

    // DuckMaps fork only, task C7: one DebugDrawableUBOEntry per contour drawable this frame -
    // the raw bytes of every uniform buffer bound to it (hashed) and every bound texture's
    // stable identity, filled in the same execute() loop that builds the real UBOs (see the
    // .cpp for why hashing the CPU-side struct here rather than reading back the GPU buffer is
    // correct even under MLN_UBO_CONSOLIDATION). Empty (no allocation) when the trace is off.
    // Drained once per frame by Renderer::Impl::render, same contract as the drain above.
    static std::vector<DebugDrawableUBOEntry> debugDrainContourDrawableUBOEntries();

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
