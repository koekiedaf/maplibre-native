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

    // The halo now draws as a SECOND drawable per tile, sharing this tile's own vertex/index
    // buffers with the body (the same buffer-sharing this function already does for one drawable -
    // see the vertexAttrs/setRawVertices/setSegments calls below), rather than compositing under
    // the body inside one fragment - see shaders/mtl/terrain_line.hpp's top-of-file comment for
    // why. A style that never sets terrain-line-halo-width evaluates it to its default of 0 here
    // and gets no halo drawable at all: the drawable count and the rendered frame for such a style
    // are therefore exactly what they were before this task.
    const bool hasHalo = staticImmutableCast<TerrainLineLayerProperties>(evaluatedProperties)
                             ->evaluated.get<style::TerrainLineHaloWidth>() > 0.0f;

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

        // If terrain-line-halo-width turned on or off since this tile's drawables were last
        // built (with the bucket itself unchanged - the check above only catches a bucket
        // rebuild), the existing drawable count no longer matches what this frame needs (one
        // body-only, or one body plus one halo). updateTile's own "any existing drawable found ->
        // leave it alone" shortcut below would otherwise silently leave the tile with a missing or
        // stale halo drawable, so force a full rebuild in that case.
        const std::size_t expectedDrawableCount = hasHalo ? 2 : 1;
        const std::size_t existingDrawableCount = tileLayerGroup->getDrawableCount(renderPass, tileID);
        if (existingDrawableCount != 0 && existingDrawableCount != expectedDrawableCount) {
            removeTile(renderPass, tileID);
        }

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

        // Body drawn on top of the halo: TileLayerGroup's own drawable set orders by draw
        // priority ascending (gfx::DrawableLessByPriority, drawable.hpp), and mtl::TileLayerGroup
        // ::render walks that same order to issue draw calls (mtl/tile_layer_group.cpp), so the
        // lower-numbered halo drawable is guaranteed to draw, and blend, before the body one -
        // see shaders/mtl/terrain_line.hpp's top-of-file comment.
        // Task Q3: the pass number is the HIGH part of the priority and the tile is the low
        // part, so all halos still draw before all bodies AND, within a pass, the tiles draw in
        // a fixed order instead of the order they happened to load. Before this, every halo
        // drawable shared priority 0 and every body drawable priority 1, and
        // DrawableLessByPriority broke that tie on the drawable's creation id - which is tile
        // load order. The ribbons are alpha-blended and dashed, so that showed up as a
        // run-to-run difference along the ribbon itself (measured at the Gavarnie wall: the
        // difference mask between two otherwise identical runs traces the alpine trail and a
        // stream and nothing else). See gfx::tileDrawOrderPriority.
        const gfx::DrawPriority tileOrder = gfx::tileDrawOrderPriority(tileID.toUnwrapped());
        const gfx::DrawPriority TerrainLineHaloDrawPriority = 0 * gfx::kTileDrawOrderPassStride + tileOrder;
        const gfx::DrawPriority TerrainLineBodyDrawPriority = 1 * gfx::kTileDrawOrderPassStride + tileOrder;

        auto makeBuilder = [&](const char* name, gfx::DrawPriority priority) {
            auto b = context.createDrawableBuilder(name);
            b->setShader(std::static_pointer_cast<gfx::ShaderProgramBase>(terrainLineShader));
            // Depth *test* is decided per-frame by the tweaker (drawable.setEnableDepth), exactly
            // like circle - see TerrainLineLayerTweaker::execute's comment for why. Depth *write*
            // stays off (ReadOnly): a translucent ribbon must not punch a hole in the depth buffer
            // that other translucent geometry behind it would otherwise test against.
            b->setDepthType(gfx::DepthMaskType::ReadOnly);
            b->setColorMode(gfx::ColorMode::alphaBlended());
            b->setCullFaceMode(gfx::CullFaceMode::disabled());
            b->setRenderPass(renderPass);
            b->setDrawPriority(priority);
            b->setRawVertices({}, vertexCount, gfx::AttributeDataType::Short2);
            b->setSegments(gfx::Triangles(), bucket.sharedTriangles, bucket.segments.data(), bucket.segments.size());
            return b;
        };

        // Both drawables share this tile's own vertex buffer (the same buffer-sharing this
        // function already did for its one drawable before this task) - only the draw priority
        // and the per-drawable halo_pass flag (TerrainLineLayerTweaker::execute) differ between
        // them. The halo builder gets a COPY of vertexAttrs (an lvalue set); the body builder,
        // built and used last, takes ownership via move - the same pattern
        // render_fill_extrusion_layer.cpp's own depth/color builder pair uses for its shared
        // vertexAttrs.
        auto bodyBuilder = makeBuilder("terrainLineBody", TerrainLineBodyDrawPriority);
        gfx::UniqueDrawableBuilder haloBuilder;
        if (hasHalo) {
            haloBuilder = makeBuilder("terrainLineHalo", TerrainLineHaloDrawPriority);
            haloBuilder->setVertexAttributes(vertexAttrs);
        }
        bodyBuilder->setVertexAttributes(std::move(vertexAttrs));

        const auto finish = [&](gfx::DrawableBuilder& builder, TerrainLinePassType passType) {
            builder.flush(context);
            for (auto& drawable : builder.clearDrawables()) {
                drawable->setTileID(tileID);
                drawable->setType(static_cast<std::size_t>(passType));
                drawable->setLayerTweaker(layerTweaker);
                drawable->setRenderTile(renderTilesOwner, &tile);

                tileLayerGroup->addDrawable(renderPass, tileID, std::move(drawable));
                ++stats.drawablesAdded;
            }
        };
        // Halo first: not required for correctness (draw priority alone decides render order),
        // but it means the halo drawable's own ID is lower too, which keeps the sort stable and
        // matches the render order for anyone reading gfx::DrawableLessByPriority's tie-break.
        if (haloBuilder) {
            finish(*haloBuilder, TerrainLinePassType::Halo);
        }
        finish(*bodyBuilder, TerrainLinePassType::Body);
    }
}

} // namespace mln
