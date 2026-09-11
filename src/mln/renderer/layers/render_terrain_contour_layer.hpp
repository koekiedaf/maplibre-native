#pragma once

#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/terrain_contour_layer_impl.hpp>
#include <mln/style/layers/terrain_contour_layer_properties.hpp>

namespace mln {

class TerrainContourLayerTweaker;
using TerrainContourLayerTweakerPtr = std::shared_ptr<TerrainContourLayerTweaker>;

// DuckMaps fork only, task 2.4a. Contour lines computed per-fragment from the terrain DEM,
// drawn over RenderTerrain's own mesh (renderToTerrain=false, real 3D world space, never
// draped) - see docs/plans/2026-09-11-engine-layer-plumbing.md and
// container/server/app/map/assets/contours3d.js (the web reference this ports).
//
// Unlike every other RenderLayer in this fork, this one has NO source and NO bucket: it tracks
// no renderTiles of its own (modelled on RenderBackgroundLayer, the existing source-less
// precedent) and instead follows RenderTerrain::getTilesWithDrawables() every frame, reusing
// RenderTerrain::getMesh()'s shared vertex/index buffers for its own drawables' geometry - the
// terrain's own drawables and this layer's are two independent drawables over the identical
// mesh data, one per tile, kept in step by update() below.
class RenderTerrainContourLayer final : public RenderLayer {
public:
    explicit RenderTerrainContourLayer(Immutable<style::TerrainContourLayer::Impl>);
    ~RenderTerrainContourLayer() final = default;

    /// Generate any changes needed by the layer
    void update(gfx::ShaderRegistry&,
                gfx::Context&,
                const TransformState&,
                const std::shared_ptr<UpdateParameters>&,
                const PaintParameters&,
                const RenderTree&,
                UniqueChangeRequestVec&) override;

private:
    void transition(const TransitionParameters&) override;
    void evaluate(const PropertyEvaluationParameters&) override;
    bool hasTransition() const override;
    bool hasCrossfade() const override;

private:
    // Paint properties
    style::TerrainContourPaintProperties::Unevaluated unevaluated;

    gfx::ShaderProgramBasePtr terrainContourShader;
};

} // namespace mln
