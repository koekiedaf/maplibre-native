#pragma once
#include <mln/gfx/drawable.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/util/monotonic_timer.hpp>

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mln {

class PatternAtlas;
class PaintParameters;

namespace gfx {
class UploadPass;
class Context;
} // namespace gfx

class LayerGroupBase;
using LayerGroupBasePtr = std::shared_ptr<LayerGroupBase>;

class RenderItem {
public:
    enum class DebugType {
        TextOutline,
        Text,
        Border
    };
    using DebugLayerGroupMap = std::map<DebugType, LayerGroupBasePtr>;

    virtual ~RenderItem() = default;
    virtual void upload(gfx::UploadPass&) const = 0;
    virtual void render(PaintParameters&) const = 0;
    virtual bool hasRenderPass(RenderPass) const = 0;
    virtual const std::string& getName() const = 0;
    virtual void updateDebugDrawables(DebugLayerGroupMap&, PaintParameters&) const = 0;
};

class RenderSource;
class LayerRenderItem final : public RenderItem {
public:
    LayerRenderItem(RenderLayer& layer_, RenderSource* source_, uint32_t index_);
    bool operator<(const LayerRenderItem& other) const { return index < other.index; }

    std::reference_wrapper<RenderLayer> layer;
    RenderSource* source;
    const uint32_t index;

private:
    bool hasRenderPass(RenderPass pass) const override;
    void upload(gfx::UploadPass& pass) const override;
    void render(PaintParameters& parameters) const override;
    const std::string& getName() const override;
    void updateDebugDrawables(DebugLayerGroupMap&, PaintParameters&) const override;
};

using RenderItems = std::vector<std::reference_wrapper<const RenderItem>>;

class RenderTreeParameters {
public:
    explicit RenderTreeParameters(const TransformState& state_,
                                  MapMode mapMode_,
                                  MapDebugOptions debugOptions_,
                                  TimePoint timePoint_,
                                  EvaluatedLight light_)
        : transformParams(state_),
          mapMode(mapMode_),
          debugOptions(debugOptions_),
          timePoint(timePoint_),
          light(std::move(light_)) {}
    TransformParameters transformParams;
    MapMode mapMode;
    MapDebugOptions debugOptions;
    TimePoint timePoint;
    EvaluatedLight light;
    bool has3D = false;
    uint32_t opaquePassCutOff = 0;
    Color backgroundColor;
    float symbolFadeChange = 0.0f;
    bool needsRepaint = false;
    bool loaded = false;
    bool placementChanged = false;

    // Task "break the frame down by section": three phases of RenderOrchestrator::createRenderTree
    // that happen before the RenderTree even reaches Renderer::Impl::render, so they cannot be
    // timed there - measured with the same mln::util::MonotonicTimer the tree's own startTime/
    // getElapsedTime() already use, and carried on RenderTreeParameters (built once per frame,
    // always at hand where these phases run) rather than adding a second timing mechanism.
    // Renderer::Impl::render copies these into gfx::RenderingStats right after fetching
    // renderTree.getParameters(), the same object the platform layer already reads its own
    // encodingTime/renderingTime off. Seconds, matching RenderingStats' own unit.
    double tileCoverTime = 0.0;    ///< per-source tile cover (RenderSource::update, incl. util::tileCover)
    double layerPrepareTime = 0.0; ///< per-layer per-tile prepare (RenderLayer::prepare loop)
    double placementTime = 0.0;    ///< symbol placement and collision (Placement::placeLayers)
};

class RenderTree {
public:
    virtual ~RenderTree() = default;
    virtual void prepare() {}
    // Render items
    virtual const std::set<LayerRenderItem>& getLayerRenderItemMap() const noexcept = 0;
    virtual RenderItems getLayerRenderItems() const = 0;
    virtual RenderItems getSourceRenderItems() const = 0;
    // Resources
    virtual LineAtlas& getLineAtlas() const = 0;
    virtual PatternAtlas& getPatternAtlas() const = 0;
    // Parameters
    const RenderTreeParameters& getParameters() const { return *parameters; }

    double getElapsedTime() const { return util::MonotonicTimer::now().count() - startTime; }

protected:
    RenderTree(std::unique_ptr<RenderTreeParameters> parameters_, double startTime_)
        : parameters(std::move(parameters_)),
          startTime(startTime_) {
        assert(parameters);
    }
    std::unique_ptr<RenderTreeParameters> parameters;

    double startTime;
};

} // namespace mln
