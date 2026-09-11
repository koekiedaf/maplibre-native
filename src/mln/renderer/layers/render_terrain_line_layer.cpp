#include <mln/renderer/layers/render_terrain_line_layer.hpp>

#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/renderer/buckets/terrain_line_bucket.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/layers/terrain_line_layer_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/shaders/terrain_line_layer_ubo.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>
#include <mln/tile/tile.hpp>
#include <mln/util/containers.hpp>

namespace mln {

using namespace style;

namespace {

inline const style::TerrainLineLayer::Impl& impl_cast(const Immutable<style::Layer::Impl>& impl) {
    assert(impl->getTypeInfo() == TerrainLineLayer::Impl::staticTypeInfo());
    return static_cast<const style::TerrainLineLayer::Impl&>(*impl);
}

constexpr auto TerrainLineShaderGroupName = "TerrainLineShader";

} // namespace

using namespace shaders;

RenderTerrainLineLayer::RenderTerrainLineLayer(Immutable<style::TerrainLineLayer::Impl> _impl)
    : RenderLayer(makeMutable<TerrainLineLayerProperties>(std::move(_impl))),
      unevaluated(impl_cast(baseImpl).paint.untransitioned()) {
    styleDependencies = unevaluated.getDependencies();
}

void RenderTerrainLineLayer::transition(const TransitionParameters& parameters) {
    unevaluated = impl_cast(baseImpl).paint.transitioned(parameters, std::move(unevaluated));
    styleDependencies = unevaluated.getDependencies();
}

void RenderTerrainLineLayer::evaluate(const PropertyEvaluationParameters& parameters) {
    const auto previousProperties = staticImmutableCast<TerrainLineLayerProperties>(evaluatedProperties);
    auto properties = makeMutable<TerrainLineLayerProperties>(
        staticImmutableCast<TerrainLineLayer::Impl>(baseImpl),
        unevaluated.evaluate(parameters, previousProperties->evaluated));
    const auto& evaluated = properties->evaluated;

    passes = (evaluated.get<style::TerrainLineOpacity>() > 0 && evaluated.get<style::TerrainLineColor>().a > 0)
                 ? RenderPass::Translucent
                 : RenderPass::None;
    properties->renderPasses = mln::underlying_type(passes);
    evaluatedProperties = std::move(properties);

    if (layerTweaker) {
        layerTweaker->updateProperties(evaluatedProperties);
    }
}

bool RenderTerrainLineLayer::hasTransition() const {
    return unevaluated.hasTransition();
}

bool RenderTerrainLineLayer::hasCrossfade() const {
    return false;
}

bool RenderTerrainLineLayer::queryIntersectsFeature(const GeometryCoordinates&,
                                                    const GeometryTileFeature&,
                                                    float,
                                                    const TransformState&,
                                                    float,
                                                    const mat4&,
                                                    const FeatureState&) const {
    // Not implemented in this first cut - see the class comment.
    return false;
}

void RenderTerrainLineLayer::update(gfx::ShaderRegistry& shaders,
                                    gfx::Context& context,
                                    const TransformState&,
                                    const std::shared_ptr<UpdateParameters>&,
                                    const PaintParameters&,
                                    const RenderTree&,
                                    UniqueChangeRequestVec& changes) {
    if (!renderTiles || renderTiles->empty()) {
        removeAllDrawables();
        return;
    }

    // Elevated (renderToTerrain=false), like symbol/circle/fill-extrusion - real 3D-world-space
    // geometry over the terrain, not a drape. See docs/plans/2026-09-11-engine-layer-plumbing.md
    // A4/A5.
    if (!layerGroup) {
        if (auto layerGroup_ = context.createTileLayerGroup(layerIndex, /*initialCapacity=*/64, getID(), false)) {
            setLayerGroup(std::move(layerGroup_), changes);
        } else {
            return;
        }
    }
    auto* tileLayerGroup = static_cast<TileLayerGroup*>(layerGroup.get());
    if (!layerTweaker) {
        layerTweaker = std::make_shared<TerrainLineLayerTweaker>(getID(), evaluatedProperties);
        layerGroup->addLayerTweaker(layerTweaker);
    }

    if (!terrainLineShader) {
        terrainLineShader = context.getGenericShader(shaders, TerrainLineShaderGroupName);
    }
    if (!terrainLineShader) {
        removeAllDrawables();
        return;
    }

    constexpr auto renderPass = RenderPass::Translucent;
    if (!(mln::underlying_type(renderPass) & evaluatedProperties->renderPasses)) {
        removeAllDrawables();
        return;
    }

    stats.drawablesRemoved += tileLayerGroup->removeDrawablesIf(
        [&](gfx::Drawable& drawable) { return drawable.getTileID() && !hasRenderTile(*drawable.getTileID()); });

    for (const RenderTile& tile : *renderTiles) {
        const auto& tileID = tile.getOverscaledTileID();

        const LayerRenderData* renderData = getRenderDataForPass(tile, renderPass);
        if (!renderData || !renderData->bucket || !renderData->bucket->hasData()) {
            removeTile(renderPass, tileID);
            continue;
        }

        auto& bucket = static_cast<TerrainLineBucket&>(*renderData->bucket);
        const auto vertexCount = bucket.vertices.elements();

        const auto prevBucketID = getRenderTileBucketID(tileID);
        if (prevBucketID != util::SimpleIdentity::Empty && prevBucketID != bucket.getID()) {
            removeTile(renderPass, tileID);
        }
        setRenderTileBucketID(tileID, bucket.getID());

        auto updateExisting = [&](gfx::Drawable& drawable) {
            return drawable.getLayerTweaker() == layerTweaker;
        };
        if (updateTile(renderPass, tileID, std::move(updateExisting))) {
            continue;
        }

        auto vertexAttrs = context.createVertexAttributeArray();
        if (const auto& attr = vertexAttrs->set(idTerrainLinePosVertexAttribute)) {
            attr->setSharedRawData(bucket.sharedVertices,
                                   offsetof(TerrainLineLayoutVertex, a1),
                                   /*vertexOffset=*/0,
                                   sizeof(TerrainLineLayoutVertex),
                                   gfx::AttributeDataType::Short2);
        }
        if (const auto& attr = vertexAttrs->set(idTerrainLineOtherVertexAttribute)) {
            attr->setSharedRawData(bucket.sharedVertices,
                                   offsetof(TerrainLineLayoutVertex, a2),
                                   /*vertexOffset=*/0,
                                   sizeof(TerrainLineLayoutVertex),
                                   gfx::AttributeDataType::Short2);
        }
        if (const auto& attr = vertexAttrs->set(idTerrainLineFlagVertexAttribute)) {
            attr->setSharedRawData(bucket.sharedVertices,
                                   offsetof(TerrainLineLayoutVertex, a3),
                                   /*vertexOffset=*/0,
                                   sizeof(TerrainLineLayoutVertex),
                                   gfx::AttributeDataType::Short2);
        }
        if (const auto& attr = vertexAttrs->set(idTerrainLineDistVertexAttribute)) {
            attr->setSharedRawData(bucket.sharedVertices,
                                   offsetof(TerrainLineLayoutVertex, a4),
                                   /*vertexOffset=*/0,
                                   sizeof(TerrainLineLayoutVertex),
                                   gfx::AttributeDataType::Float);
        }

        auto builder = context.createDrawableBuilder("terrainLine");
        builder->setShader(std::static_pointer_cast<gfx::ShaderProgramBase>(terrainLineShader));
        // Depth *test* is decided per-frame by the tweaker (drawable.setEnableDepth), exactly
        // like circle - see TerrainLineLayerTweaker::execute's comment for why. Depth *write*
        // stays off (ReadOnly): a translucent ribbon must not punch a hole in the depth buffer
        // that other translucent geometry behind it would otherwise test against.
        builder->setDepthType(gfx::DepthMaskType::ReadOnly);
        builder->setColorMode(gfx::ColorMode::alphaBlended());
        builder->setCullFaceMode(gfx::CullFaceMode::disabled());
        builder->setRenderPass(renderPass);
        builder->setVertexAttributes(std::move(vertexAttrs));
        builder->setRawVertices({}, vertexCount, gfx::AttributeDataType::Short2);
        builder->setSegments(gfx::Triangles(), bucket.sharedTriangles, bucket.segments.data(), bucket.segments.size());

        builder->flush(context);

        for (auto& drawable : builder->clearDrawables()) {
            drawable->setTileID(tileID);
            drawable->setLayerTweaker(layerTweaker);
            drawable->setRenderTile(renderTilesOwner, &tile);

            tileLayerGroup->addDrawable(renderPass, tileID, std::move(drawable));
            ++stats.drawablesAdded;
        }
    }
}

} // namespace mln
