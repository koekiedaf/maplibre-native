#pragma once

#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/slope_shading_layer_impl.hpp>
#include <mln/style/layers/slope_shading_layer_properties.hpp>

namespace mln {

class SlopeShadingLayerTweaker;
using SlopeShadingLayerTweakerPtr = std::shared_ptr<SlopeShadingLayerTweaker>;

// DuckMaps fork only, task 2.6. Slope/aspect area fill computed per-fragment from the terrain
// DEM, drawn over RenderTerrain's own mesh (renderToTerrain=false, real 3D world space, never
// draped), BEFORE RenderTerrainContourLayer so the contour lines sit on top of the fill - see
// container/server/app/map/assets/contours3d.js (the web reference this ports) and
// render_terrain_contour_layer.hpp, whose shape (and whose own header comment) this mirrors
// exactly: no source, no bucket, follows RenderTerrain::getTilesWithDrawables() every frame,
// reusing RenderTerrain::getMesh()'s shared vertex/index buffers for its own drawables.
class RenderSlopeShadingLayer final : public RenderLayer {
public:
    explicit RenderSlopeShadingLayer(Immutable<style::SlopeShadingLayer::Impl>);
    ~RenderSlopeShadingLayer() final = default;

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

    // See RenderTerrainContourLayer::prepare's own comment: a source-less layer type must
    // override this no-op, or RenderLayer's default dereferences a null source on the very
    // first frame.
    void prepare(const LayerPrepareParameters&) override;

private:
    // Paint properties
    style::SlopeShadingPaintProperties::Unevaluated unevaluated;

    gfx::ShaderProgramBasePtr slopeShadingShader;
};

} // namespace mln
