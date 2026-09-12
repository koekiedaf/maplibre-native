#include <mln/renderer/layers/render_terrain_contour_layer.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/layers/terrain_contour_layer_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/shaders/terrain_contour_layer_ubo.hpp>
#include <mln/style/layers/terrain_contour_layer_impl.hpp>

#include <algorithm>
#include <cstring>

namespace mln {

using namespace style;

namespace {

inline const style::TerrainContourLayer::Impl& impl_cast(const Immutable<style::Layer::Impl>& impl) {
    assert(impl->getTypeInfo() == TerrainContourLayer::Impl::staticTypeInfo());
    return static_cast<const style::TerrainContourLayer::Impl&>(*impl);
}

constexpr auto TerrainContourShaderGroupName = "TerrainContourShader";

} // namespace

using namespace shaders;

RenderTerrainContourLayer::RenderTerrainContourLayer(Immutable<style::TerrainContourLayer::Impl> _impl)
    : RenderLayer(makeMutable<TerrainContourLayerProperties>(std::move(_impl))),
      unevaluated(impl_cast(baseImpl).paint.untransitioned()) {
    styleDependencies = unevaluated.getDependencies();
}

void RenderTerrainContourLayer::transition(const TransitionParameters& parameters) {
    unevaluated = impl_cast(baseImpl).paint.transitioned(parameters, std::move(unevaluated));
    styleDependencies = unevaluated.getDependencies();
}

void RenderTerrainContourLayer::evaluate(const PropertyEvaluationParameters& parameters) {
    const auto previousProperties = staticImmutableCast<TerrainContourLayerProperties>(evaluatedProperties);
    auto properties = makeMutable<TerrainContourLayerProperties>(
        staticImmutableCast<TerrainContourLayer::Impl>(baseImpl),
        unevaluated.evaluate(parameters, previousProperties->evaluated));
    const auto& evaluated = properties->evaluated;

    // Translucent whenever either family could draw anything - an interval of 0 means "don't
    // draw this family" (see the paint spec docs), matching contours3d.js's own u_minor_i >
    // 0.0 / u_index_i > 0.0 guards.
    const bool minorVisible = evaluated.get<style::TerrainContourMinorInterval>() > 0.0f &&
                              evaluated.get<style::TerrainContourMinorOpacity>() > 0.0f;
    const bool indexVisible = evaluated.get<style::TerrainContourIndexInterval>() > 0.0f &&
                              evaluated.get<style::TerrainContourIndexOpacity>() > 0.0f;
    passes = (minorVisible || indexVisible) ? RenderPass::Translucent : RenderPass::None;
    properties->renderPasses = mln::underlying_type(passes);
    evaluatedProperties = std::move(properties);

    if (layerTweaker) {
        layerTweaker->updateProperties(evaluatedProperties);
    }
}

bool RenderTerrainContourLayer::hasTransition() const {
    return unevaluated.hasTransition();
}

bool RenderTerrainContourLayer::hasCrossfade() const {
    return false;
}

void RenderTerrainContourLayer::prepare(const LayerPrepareParameters&) {
    // Deliberately does not call RenderLayer::prepare(): its default body unconditionally
    // dereferences LayerPrepareParameters::source, which is null for this source-less layer
    // type - see the header comment. update() gets its own tile list from
    // RenderTerrain::getTilesWithDrawables() instead of renderTiles.
}

void RenderTerrainContourLayer::update(gfx::ShaderRegistry& shaders,
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
        layerTweaker = std::make_shared<TerrainContourLayerTweaker>(getID(), evaluatedProperties);
        layerGroup->addLayerTweaker(layerTweaker);
    }

    if (!terrainContourShader) {
        terrainContourShader = context.getGenericShader(shaders, TerrainContourShaderGroupName);
    }
    if (!terrainContourShader) {
        removeAllDrawables();
        return;
    }

    constexpr auto renderPass = RenderPass::Translucent;
    if (!(mln::underlying_type(renderPass) & evaluatedProperties->renderPasses)) {
        removeAllDrawables();
        return;
    }

    // Drop drawables for tiles RenderTerrain no longer has a terrain drawable for - keeps this
    // layer's own cover exactly in step with the terrain's, every frame, so nothing is leaked
    // for tiles the terrain has since dropped.
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
        // Already have a drawable for this tile - the tweaker updates its DEM binding and
        // evaluated properties every frame, nothing to rebuild here.
        if (tileLayerGroup->getDrawableCount(renderPass, tileID) > 0) {
            continue;
        }

        if (!builder) {
            builder = context.createDrawableBuilder("terrain-contour");
            builder->setShader(std::static_pointer_cast<gfx::ShaderProgramBase>(terrainContourShader));
            // Depth test on, LEQUAL (the engine's standard 3D depth mode - see
            // PaintParameters::depthModeForSublayer/depthModeFor3D, both LessEqual), write off:
            // this mesh sits exactly on RenderTerrain's own terrain surface and must not punch
            // holes in the depth buffer for translucent geometry drawn behind it.
            builder->setDepthType(gfx::DepthMaskType::ReadOnly);
            // ONE / ONE_MINUS_SRC_ALPHA, premultiplied - matches contours3d.js's
            // gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA) exactly.
            builder->setColorMode(gfx::ColorMode::alphaBlended());
            builder->setEnableDepth(true);
            builder->setRenderPass(renderPass);
            builder->setVertexAttrId(idTerrainContourPosVertexAttribute);
        }

        // Reuses RenderTerrain::getMesh()'s shared vertex/index buffers: the SAME mesh data the
        // terrain's own drawable for this tile was built from, copied into this drawable's own
        // raw buffers exactly as RenderTerrain::createDrawableForTile does for its own drawable
        // (see render_terrain.cpp) - not a novel sharing mechanism, the existing one.
        std::vector<uint8_t> vertexData(terrainMesh.vertices.size() * sizeof(int16_t));
        std::memcpy(vertexData.data(), terrainMesh.vertices.data(), vertexData.size());
        builder->setRawVertices(std::move(vertexData), terrainMesh.vertexCount, gfx::AttributeDataType::Short4);

        SegmentVector segments;
        segments.emplace_back(0, 0, terrainMesh.vertexCount, terrainMesh.indexCount);
        std::vector<uint16_t> indexData = terrainMesh.indices;
        builder->setSegments(gfx::Triangles(), std::move(indexData), segments.data(), segments.size());

        // Task Q3: order this layer group's drawables by TILE, not by the moment each tile
        // happened to load. This layer is alpha-blended and neighbouring terrain tiles share
        // their seam pixels, and src-over is not commutative, so a load-order draw sequence is
        // a run-to-run difference nothing upstream can remove - see
        // gfx::tileDrawOrderPriority's own comment for the measurement that found it.
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
