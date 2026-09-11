#pragma once

#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>
#include <mln/style/layers/terrain_line_layer_properties.hpp>

namespace mln {

class TerrainLineLayerTweaker;
using TerrainLineLayerTweakerPtr = std::shared_ptr<TerrainLineLayerTweaker>;

// DuckMaps fork only: an elevated ribbon layer (trail/route/track) drawn in real 3D world space
// on the terrain, never draped - see docs/plans/2026-09-11-engine-layer-plumbing.md. Modelled
// directly on RenderCircleLayer's shape: one drawable per tile, one shader (no data-driven paint
// properties, so no shader GROUP/variant selection the way RenderCircleLayer needs).
class RenderTerrainLineLayer final : public RenderLayer {
public:
    explicit RenderTerrainLineLayer(Immutable<style::TerrainLineLayer::Impl>);
    ~RenderTerrainLineLayer() final = default;

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

    // Not implemented in this first cut: query/hit-testing against the ribbon geometry.
    bool queryIntersectsFeature(const GeometryCoordinates&,
                                const GeometryTileFeature&,
                                float,
                                const TransformState&,
                                float,
                                const mat4&,
                                const FeatureState&) const override;

private:
    // Paint properties
    style::TerrainLinePaintProperties::Unevaluated unevaluated;

    gfx::ShaderProgramBasePtr terrainLineShader;
};

} // namespace mln
