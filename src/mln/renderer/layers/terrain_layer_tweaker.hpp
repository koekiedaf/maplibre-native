#pragma once

#include <mln/util/noncopyable.hpp>
#include <mln/shaders/shader_defines.hpp>

#include <memory>
#include <string>

namespace mln {

class RenderTerrain;
class RenderOrchestrator; // DuckMaps fork only: for the sky, see execute()'s own comment
class LayerGroupBase;
class PaintParameters;

namespace gfx {
class UniformBuffer;
using UniformBufferPtr = std::shared_ptr<UniformBuffer>;
} // namespace gfx

/**
 * Terrain layer specific tweaker - updates UBOs for terrain rendering
 * Note: This is NOT a LayerTweaker because terrain is not a regular layer
 */
class TerrainLayerTweaker : util::noncopyable {
public:
    // DuckMaps fork only: `orchestrator_` reaches this tweaker the same way `terrain_` already
    // does - a raw pointer stored at construction and dereferenced fresh on every `execute()`
    // call, since RenderOrchestrator (a value member of Renderer::Impl) outlives every frame
    // just as the RenderTerrain this tweaker belongs to does. It is how this tweaker reaches the
    // style's `sky` root property for the terrain ground fog (RenderOrchestrator::getSky()).
    explicit TerrainLayerTweaker(const RenderTerrain* terrain_, const RenderOrchestrator* orchestrator_)
        : terrain(terrain_),
          orchestrator(orchestrator_) {}

    ~TerrainLayerTweaker() = default;

    void execute(LayerGroupBase&, const PaintParameters&);

protected:
#if MLN_UBO_CONSOLIDATION
    gfx::UniformBufferPtr drawableUniformBuffer;
#endif

    const RenderTerrain* terrain = nullptr;
    const RenderOrchestrator* orchestrator = nullptr;
};

} // namespace mln
