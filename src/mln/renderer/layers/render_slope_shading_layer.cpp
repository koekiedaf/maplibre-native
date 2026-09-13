#include <mln/renderer/layers/render_slope_shading_layer.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/layers/slope_shading_layer_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/shaders/slope_shading_layer_ubo.hpp>
#include <mln/style/layers/slope_shading_layer_impl.hpp>

#include <algorithm>
#include <cstring>

namespace mln {

using namespace style;

namespace {

inline const style::SlopeShadingLayer::Impl& impl_cast(const Immutable<style::Layer::Impl>& impl) {
    assert(impl->getTypeInfo() == SlopeShadingLayer::Impl::staticTypeInfo());
    return static_cast<const style::SlopeShadingLayer::Impl&>(*impl);
}

constexpr auto SlopeShadingShaderGroupName = "SlopeShadingShader";

} // namespace

using namespace shaders;

RenderSlopeShadingLayer::RenderSlopeShadingLayer(Immutable<style::SlopeShadingLayer::Impl> _impl)
    : RenderLayer(makeMutable<SlopeShadingLayerProperties>(std::move(_impl))),
      unevaluated(impl_cast(baseImpl).paint.untransitioned()) {
    styleDependencies = unevaluated.getDependencies();
}

void RenderSlopeShadingLayer::transition(const TransitionParameters& parameters) {
    unevaluated = impl_cast(baseImpl).paint.transitioned(parameters, std::move(unevaluated));
    styleDependencies = unevaluated.getDependencies();
}

void RenderSlopeShadingLayer::evaluate(const PropertyEvaluationParameters& parameters) {
    const auto previousProperties = staticImmutableCast<SlopeShadingLayerProperties>(evaluatedProperties);
    auto properties = makeMutable<SlopeShadingLayerProperties>(
        staticImmutableCast<SlopeShadingLayer::Impl>(baseImpl),
        unevaluated.evaluate(parameters, previousProperties->evaluated));
    const auto& evaluated = properties->evaluated;

    // Translucent whenever the fill could draw anything - an opacity of 0 means "don't draw",
    // matching contours3d.js's own slope.opacity gate and terrain-contour's identical reasoning
    // for its own minor/index intervals.
    const bool visible = evaluated.get<style::SlopeShadingOpacity>() > 0.0f;
    passes = visible ? RenderPass::Translucent : RenderPass::None;
    properties->renderPasses = mln::underlying_type(passes);
    evaluatedProperties = std::move(properties);

    if (layerTweaker) {
        layerTweaker->updateProperties(evaluatedProperties);
    }
}

bool RenderSlopeShadingLayer::hasTransition() const {
    return unevaluated.hasTransition();
}

bool RenderSlopeShadingLayer::hasCrossfade() const {
    return false;
}

void RenderSlopeShadingLayer::prepare(const LayerPrepareParameters&) {
    // Deliberately does not call RenderLayer::prepare() - see the header comment.
}

void RenderSlopeShadingLayer::update(gfx::ShaderRegistry& shaders,
                                     gfx::Context& context,
                                     const TransformState&,
                                     const std::shared_ptr<UpdateParameters>&,
                                     const PaintParameters& parameters,
                                     const RenderTree&,
                                     UniqueChangeRequestVec& changes) {
    // No source, no renderTiles (see the class comment) - this layer draws over
    // RenderTerrain's own mesh, one drawable per tile RenderTerrain currently has one for.
    if (!parameters.terrain || !parameters.terrain->isEnabled()) {
        removeAllDrawables();
        return;
    }

    const auto tileIDs = parameters.terrain->getTilesWithDrawables();
    if (tileIDs.empty()) {
        removeAllDrawables();
        return;
    }

    if (!layerGroup) {
        if (auto layerGroup_ = context.createTileLayerGroup(layerIndex, /*initialCapacity=*/64, getID(), false)) {
            setLayerGroup(std::move(layerGroup_), changes);
        } else {
            return;
        }
    }
    auto* tileLayerGroup = static_cast<TileLayerGroup*>(layerGroup.get());
    if (!layerTweaker) {
        layerTweaker = std::make_shared<SlopeShadingLayerTweaker>(getID(), evaluatedProperties);
        layerGroup->addLayerTweaker(layerTweaker);
    }

    if (!slopeShadingShader) {
        slopeShadingShader = context.getGenericShader(shaders, SlopeShadingShaderGroupName);
    }
    if (!slopeShadingShader) {
        removeAllDrawables();
        return;
    }

    constexpr auto renderPass = RenderPass::Translucent;
    if (!(mln::underlying_type(renderPass) & evaluatedProperties->renderPasses)) {
        removeAllDrawables();
        return;
    }

    // Drop drawables for tiles RenderTerrain no longer has a terrain drawable for - keeps this
    // layer's own cover exactly in step with the terrain's, every frame (same reasoning as
    // RenderTerrainContourLayer's own removeDrawablesIf).
    stats.drawablesRemoved += tileLayerGroup->removeDrawablesIf([&](gfx::Drawable& drawable) {
        return drawable.getTileID() &&
               std::ranges::find(tileIDs, *drawable.getTileID()) == tileIDs.end();
    });

    const auto& terrainMesh = parameters.terrain->getMesh(context);
    if (terrainMesh.vertices.empty() || terrainMesh.indices.empty()) {
        return;
    }

    std::unique_ptr<gfx::DrawableBuilder> builder;

    for (const auto& tileID : tileIDs) {
        // Already have a drawable for this tile - the tweaker updates its DEM/LUT bindings and
        // evaluated properties every frame, nothing to rebuild here.
        if (tileLayerGroup->getDrawableCount(renderPass, tileID) > 0) {
            continue;
        }

        if (!builder) {
            builder = context.createDrawableBuilder("slope-shading");
            builder->setShader(std::static_pointer_cast<gfx::ShaderProgramBase>(slopeShadingShader));
            // Depth test on, LEQUAL, write off - same reasoning as terrain-contour's own
            // builder: this mesh sits exactly on RenderTerrain's own terrain surface and must not
            // punch holes in the depth buffer for translucent geometry drawn behind it.
            builder->setDepthType(gfx::DepthMaskType::ReadOnly);
            // ONE / ONE_MINUS_SRC_ALPHA, premultiplied - matches contours3d.js's
            // gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA) exactly, same as terrain-contour.
            builder->setColorMode(gfx::ColorMode::alphaBlended());
            builder->setEnableDepth(true);
            builder->setRenderPass(renderPass);
            builder->setVertexAttrId(idSlopeShadingPosVertexAttribute);
        }

        // Reuses RenderTerrain::getMesh()'s shared vertex/index buffers - the SAME mesh data the
        // terrain's own drawable for this tile was built from (see RenderTerrainContourLayer's
        // identical copy, and render_terrain.cpp's generateMesh() for the layout).
        std::vector<uint8_t> vertexData(terrainMesh.vertices.size() * sizeof(int16_t));
        std::memcpy(vertexData.data(), terrainMesh.vertices.data(), vertexData.size());
        builder->setRawVertices(std::move(vertexData), terrainMesh.vertexCount, gfx::AttributeDataType::Short4);

        SegmentVector segments;
        segments.emplace_back(0, 0, terrainMesh.vertexCount, terrainMesh.indexCount);
        std::vector<uint16_t> indexData = terrainMesh.indices;
        builder->setSegments(gfx::Triangles(), std::move(indexData), segments.data(), segments.size());

        // Deterministic per-tile draw order, not load order - this layer alpha-blends over the
        // same neighbouring-tile seams terrain-contour does, and src-over is not commutative; see
        // terrain_contour_layer_tweaker.cpp's own comment on gfx::tileDrawOrderPriority for the
        // measurement (up to 228,680 of 3,162,132 pixels differing run to run) that made this a
        // fixed rule for every alpha-blended tile-keyed layer group in this fork, not just
        // terrain-contour's own.
        builder->setDrawPriority(gfx::tileDrawOrderPriority(tileID.toUnwrapped()));

        builder->flush(context);

        for (auto& drawable : builder->clearDrawables()) {
            drawable->setTileID(tileID);
            drawable->setLayerTweaker(layerTweaker);

            tileLayerGroup->addDrawable(renderPass, tileID, std::move(drawable));
            ++stats.drawablesAdded;
        }
    }
}

} // namespace mln
