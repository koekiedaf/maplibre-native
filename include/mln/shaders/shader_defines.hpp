#pragma once

#include <mln/shaders/layer_ubo.hpp>

#include <algorithm>

namespace mln {
namespace shaders {

// layer SSBOs

static constexpr uint32_t layerSSBOStartId = globalUBOCount;

enum {
    idDrawableReservedVertexOnlyUBO = layerSSBOStartId,
    idDrawableReservedFragmentOnlyUBO,
    drawableReservedUBOCount
};

enum {
    idBackgroundDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    backgroundLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idCircleDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    circleLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idColorReliefDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    idColorReliefTilePropsUBO = drawableReservedUBOCount,       // SSBO
    colorReliefLayerSSBOCount
};

enum {
    idFillDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    idFillTilePropsUBO = drawableReservedUBOCount,       // SSBO
    fillLayerSSBOCount
};

enum {
    idFillExtrusionDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    idFillExtrusionTilePropsUBO = drawableReservedUBOCount,       // both SSBO and UBO?
    fillExtrusionLayerSSBOCount
};

enum {
    idHeatmapDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    heatmapLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idHillshadeDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idHillshadeTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    hillshadeLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idLineDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idLineTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    lineLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idRasterDrawableUBO = idDrawableReservedVertexOnlyUBO, // SSBO
    rasterLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idSymbolDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idSymbolTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    symbolLayerSSBOCount = drawableReservedUBOCount
};

enum {
    idTerrainDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idTerrainTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    terrainLayerSSBOCount = drawableReservedUBOCount
};

// terrain-line (DuckMaps fork): elevated ribbon layer. Vertex-only drawable UBO (matrix + dem_*
// + reference_w) plus a fragment-only tile-props UBO (dash_period + dash_on, task 2.2c) - the
// same split terrain-contour uses (terrain_contour_layer_ubo.hpp) and for the identical reason:
// mtl::UniformBufferArray::bindMtl (src/mln/mtl/uniform_buffer.cpp:39-51) binds a buffer at
// idDrawableReservedVertexOnlyUBO to the VERTEX stage only, so a fragment shader reading
// idTerrainLineDrawableUBO directly (as this shader used to for dash_period/dash_on) saw an
// unbound Metal argument-table slot and read zeros - dash_period reading 0 means the dash test
// (`dash_period > 0.0 && ...`) never fires, i.e. every dasharray renders solid. See
// TerrainLineDrawableUBO/TerrainLineTilePropsUBO's own comments in terrain_line_layer_ubo.hpp.
enum {
    idTerrainLineDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idTerrainLineTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    terrainLineLayerSSBOCount = drawableReservedUBOCount
};

// terrain-contour (DuckMaps fork): contour lines computed per-fragment from the terrain DEM,
// drawn over RenderTerrain's own mesh. Vertex-only drawable UBO (matrix + dem_*, task 2.4a) plus
// a fragment-only tile-props UBO (dem_* duplicated + reference_w, task 2.4b) - see
// docs/plans/2026-09-11-engine-layer-plumbing.md and TerrainContourDrawableUBO's own comment in
// terrain_contour_layer_ubo.hpp for why the fragment stage needs its own copy rather than
// reading idTerrainContourDrawableUBO (that id is idDrawableReservedVertexOnlyUBO, which
// mtl::UniformBufferArray::bindMtl binds to the vertex stage only).
enum {
    idTerrainContourDrawableUBO = idDrawableReservedVertexOnlyUBO,    // SSBO
    idTerrainContourTilePropsUBO = idDrawableReservedFragmentOnlyUBO, // SSBO
    terrainContourLayerSSBOCount = drawableReservedUBOCount
};

// layer UBOs
static constexpr uint32_t layerUBOStartId = std::max({static_cast<uint32_t>(drawableReservedUBOCount),
                                                      static_cast<uint32_t>(backgroundLayerSSBOCount),
                                                      static_cast<uint32_t>(circleLayerSSBOCount),
                                                      static_cast<uint32_t>(fillLayerSSBOCount),
                                                      static_cast<uint32_t>(fillExtrusionLayerSSBOCount),
                                                      static_cast<uint32_t>(heatmapLayerSSBOCount),
                                                      static_cast<uint32_t>(hillshadeLayerSSBOCount),
                                                      static_cast<uint32_t>(colorReliefLayerSSBOCount),
                                                      static_cast<uint32_t>(lineLayerSSBOCount),
                                                      static_cast<uint32_t>(rasterLayerSSBOCount),
                                                      static_cast<uint32_t>(symbolLayerSSBOCount),
                                                      static_cast<uint32_t>(terrainLayerSSBOCount),
                                                      static_cast<uint32_t>(terrainLineLayerSSBOCount),
                                                      static_cast<uint32_t>(terrainContourLayerSSBOCount)});

#if MLN_RENDER_BACKEND_VULKAN
#define getEnumValue(packed, unpacked) unpacked
#else
#define getEnumValue(packed, unpacked) packed
#endif

enum {
    idBackgroundPropsUBO = getEnumValue(backgroundLayerSSBOCount, layerUBOStartId),
    backgroundLayerUBOCount
};

enum {
    idCircleEvaluatedPropsUBO = getEnumValue(circleLayerSSBOCount, layerUBOStartId),
    circleLayerUBOCount
};

enum {
    idColorReliefEvaluatedPropsUBO = getEnumValue(colorReliefLayerSSBOCount, layerUBOStartId),
    colorReliefLayerUBOCount
};

enum {
    idFillEvaluatedPropsUBO = getEnumValue(fillLayerSSBOCount, layerUBOStartId),
    fillLayerUBOCount
};

enum {
    idFillExtrusionPropsUBO = getEnumValue(fillExtrusionLayerSSBOCount, layerUBOStartId),
    fillExtrusionLayerUBOCount
};

enum {
    idHeatmapEvaluatedPropsUBO = getEnumValue(heatmapLayerSSBOCount, layerUBOStartId),
    heatmapLayerUBOCount
};

enum {
    idHeatmapTexturePropsUBO = getEnumValue(drawableReservedUBOCount, layerUBOStartId),
    heatmapTextureUBOCount
};

enum {
    idHillshadeEvaluatedPropsUBO = getEnumValue(hillshadeLayerSSBOCount, layerUBOStartId),
    hillshadeLayerUBOCount
};

enum {
    idLineEvaluatedPropsUBO = getEnumValue(lineLayerSSBOCount, layerUBOStartId),
    idLineExpressionUBO,
    lineLayerUBOCount
};

enum {
    idRasterEvaluatedPropsUBO = getEnumValue(rasterLayerSSBOCount, layerUBOStartId),
    rasterLayerUBOCount
};

enum {
    idSymbolEvaluatedPropsUBO = getEnumValue(symbolLayerSSBOCount, layerUBOStartId),
    symbolLayerUBOCount
};

enum {
    idTerrainEvaluatedPropsUBO = getEnumValue(terrainLayerSSBOCount, layerUBOStartId),
    terrainLayerUBOCount
};

enum {
    idTerrainLineEvaluatedPropsUBO = getEnumValue(terrainLineLayerSSBOCount, layerUBOStartId),
    terrainLineLayerUBOCount
};

enum {
    idTerrainContourEvaluatedPropsUBO = getEnumValue(terrainContourLayerSSBOCount, layerUBOStartId),
    terrainContourLayerUBOCount
};

// drawable SSBOs

static constexpr uint32_t drawableSSBOStartId = std::max({static_cast<uint32_t>(backgroundLayerUBOCount),
                                                          static_cast<uint32_t>(circleLayerUBOCount),
                                                          static_cast<uint32_t>(colorReliefLayerUBOCount),
                                                          static_cast<uint32_t>(fillLayerUBOCount),
                                                          static_cast<uint32_t>(fillExtrusionLayerUBOCount),
                                                          static_cast<uint32_t>(heatmapLayerUBOCount),
                                                          static_cast<uint32_t>(hillshadeLayerUBOCount),
                                                          static_cast<uint32_t>(lineLayerUBOCount),
                                                          static_cast<uint32_t>(rasterLayerUBOCount),
                                                          static_cast<uint32_t>(symbolLayerUBOCount),
                                                          static_cast<uint32_t>(terrainLayerUBOCount),
                                                          static_cast<uint32_t>(terrainLineLayerUBOCount),
                                                          static_cast<uint32_t>(terrainContourLayerUBOCount)});

enum {
#if MLN_USE_FILL_EXTRUSION_INSTANCING
    idFillExtrusionInstancedDrawableUBO = getEnumValue(fillExtrusionLayerUBOCount, drawableSSBOStartId),
#endif
    fillExtrusionDrawableSSBOCount
};

enum {
#if MLN_USE_SYMBOL_INSTANCING
    idSymbolInstancedDrawableUBO = getEnumValue(symbolLayerUBOCount, drawableSSBOStartId),
    idSymbolDynamicInstancedDrawableUBO,
    idSymbolOpacityInstancedDrawableUBO,
    idSymbolDataInstancedDrawableUBO,
#endif
    symbolDrawableSSBOCount
};

// drawable UBOs

static constexpr uint32_t drawableUBOStartId = std::max({static_cast<uint32_t>(drawableSSBOStartId),
                                                         static_cast<uint32_t>(fillExtrusionDrawableSSBOCount),
                                                         static_cast<uint32_t>(symbolDrawableSSBOCount)});

enum {
    backgroundUBOCount = getEnumValue(backgroundLayerUBOCount, drawableUBOStartId)
};

enum {
    circleUBOCount = getEnumValue(circleLayerUBOCount, drawableUBOStartId)
};

enum {
    idCollisionDrawableUBO = getEnumValue(idDrawableReservedVertexOnlyUBO, drawableUBOStartId), // UBO
    idCollisionTilePropsUBO = getEnumValue(drawableReservedUBOCount, idCollisionDrawableUBO + 1),
    collisionUBOCount
};

enum {
    idClippingMaskUBO = getEnumValue(idDrawableReservedVertexOnlyUBO, drawableUBOStartId),
    clippingMaskUBOCount = getEnumValue(drawableReservedUBOCount, idClippingMaskUBO + 1)
};

// DuckMaps fork only, task T3: the style spec's `sky` root property. SkyShader is, like
// ClippingMaskProgram just above, a raw once-per-frame draw outside the tile drawable/tweaker
// system - see mtl/sky.hpp's own header comment - so it needs only this one fragment-only UBO
// slot, matching the identically-named enum in that header's MSL prelude.
enum {
    idSkyUBO = getEnumValue(idDrawableReservedFragmentOnlyUBO, drawableUBOStartId),
    skyUBOCount = getEnumValue(drawableReservedUBOCount, idSkyUBO + 1)
};

enum {
    colorReliefUBOCount = getEnumValue(colorReliefLayerUBOCount, drawableUBOStartId)
};

enum {
    idCustomGeometryDrawableUBO = getEnumValue(drawableReservedUBOCount, drawableUBOStartId), // UBO
    customGeometryUBOCount
};

enum {
    idCustomSymbolDrawableUBO = getEnumValue(idDrawableReservedVertexOnlyUBO, drawableUBOStartId), // UBO
    customSymbolUBOCount = getEnumValue(drawableReservedUBOCount, idCustomSymbolDrawableUBO + 1)
};

enum {
    idDebugUBO = getEnumValue(drawableReservedUBOCount, drawableUBOStartId), // UBO
    debugUBOCount
};

enum {
    fillUBOCount = getEnumValue(fillLayerUBOCount, drawableUBOStartId)
};

enum {
    fillExtrusionUBOCount = getEnumValue(fillExtrusionLayerUBOCount, drawableUBOStartId)
};

enum {
    heatmapUBOCount = getEnumValue(heatmapLayerUBOCount, drawableUBOStartId)
};

enum {
    idHillshadePrepareDrawableUBO = getEnumValue(idDrawableReservedVertexOnlyUBO, drawableUBOStartId),          // UBO
    idHillshadePrepareTilePropsUBO = getEnumValue(drawableReservedUBOCount, idHillshadePrepareDrawableUBO + 1), // UBO
    hillshadePrepareUBOCount
};

enum {
    hillshadeUBOCount = getEnumValue(hillshadeLayerUBOCount, drawableUBOStartId)
};

enum {
    lineUBOCount = getEnumValue(lineLayerUBOCount, drawableUBOStartId)
};

enum {
    idLocationIndicatorDrawableUBO = getEnumValue(drawableReservedUBOCount, drawableUBOStartId), // UBO
    locationIndicatorUBOCount
};

enum {
    rasterUBOCount = getEnumValue(rasterLayerUBOCount, drawableUBOStartId)
};

enum {
    symbolUBOCount = getEnumValue(symbolLayerUBOCount, drawableUBOStartId)
};

enum {
    terrainUBOCount = getEnumValue(terrainLayerUBOCount, drawableUBOStartId)
};

enum {
    terrainLineUBOCount = getEnumValue(terrainLineLayerUBOCount, drawableUBOStartId)
};

enum {
    terrainContourUBOCount = getEnumValue(terrainContourLayerUBOCount, drawableUBOStartId)
};

enum {
    idWideVectorUniformsUBO = getEnumValue(idDrawableReservedVertexOnlyUBO, drawableUBOStartId),         // UBO
    idWideVectorUniformWideVecUBO = getEnumValue(drawableReservedUBOCount, idWideVectorUniformsUBO + 1), // UBO
    wideVectorUBOCount
};

#undef getEnumValue

static constexpr uint32_t maxUBOCountPerShader = std::max({static_cast<uint32_t>(backgroundUBOCount),
                                                           static_cast<uint32_t>(circleUBOCount),
                                                           static_cast<uint32_t>(clippingMaskUBOCount),
                                                           static_cast<uint32_t>(skyUBOCount),
                                                           static_cast<uint32_t>(collisionUBOCount),
                                                           static_cast<uint32_t>(colorReliefUBOCount),
                                                           static_cast<uint32_t>(customGeometryUBOCount),
                                                           static_cast<uint32_t>(debugUBOCount),
                                                           static_cast<uint32_t>(fillUBOCount),
                                                           static_cast<uint32_t>(fillExtrusionUBOCount),
                                                           static_cast<uint32_t>(heatmapTextureUBOCount),
                                                           static_cast<uint32_t>(heatmapUBOCount),
                                                           static_cast<uint32_t>(hillshadePrepareUBOCount),
                                                           static_cast<uint32_t>(hillshadeUBOCount),
                                                           static_cast<uint32_t>(lineUBOCount),
                                                           static_cast<uint32_t>(locationIndicatorUBOCount),
                                                           static_cast<uint32_t>(rasterUBOCount),
                                                           static_cast<uint32_t>(symbolUBOCount),
                                                           static_cast<uint32_t>(terrainUBOCount),
                                                           static_cast<uint32_t>(terrainLineUBOCount),
                                                           static_cast<uint32_t>(wideVectorUBOCount)});

static constexpr uint32_t maxSSBOCountPerLayer = layerUBOStartId - layerSSBOStartId;
static constexpr uint32_t maxUBOCountPerLayer = drawableSSBOStartId - layerUBOStartId;

static constexpr uint32_t maxSSBOCountPerDrawable = drawableUBOStartId - drawableSSBOStartId;
static constexpr uint32_t maxUBOCountPerDrawable = maxUBOCountPerShader - drawableUBOStartId;

// Texture defines
enum {
    idBackgroundImageTexture,
    backgroundTextureCount
};

enum {
    idCircleDEMTexture,
    circleTextureCount
};

enum {
    clippingMaskTextureCount
};

enum {
    collisionTextureCount
};

enum {
    idCustomGeometryTexture,
    customGeometryTextureCount
};

enum {
    idCustomSymbolImageTexture,
    customSymbolTextureCount
};

enum {
    idDebugOverlayTexture,
    debugTextureCount
};

enum {
    idFillImageTexture,
    fillTextureCount
};

enum {
    idFillExtrusionImageTexture,
    idFillExtrusionDEMTexture,
    fillExtrusionTextureCount
};

enum {
    idHeatmapImageTexture,
    idHeatmapColorRampTexture,
    heatmapTextureCount
};

enum {
    idHillshadeImageTexture,
    hillshadeTextureCount
};

enum {
    idColorReliefImageTexture,
    idColorReliefElevationStopsTexture,
    idColorReliefColorStopsTexture,
    colorReliefTextureCount
};

enum {
    idLocationIndicatorTexture,
    locationIndicatorTextureCount
};

enum {
    idLineImageTexture,
    lineTextureCount
};

enum {
    idRasterImage0Texture,
    idRasterImage1Texture,
    rasterTextureCount
};

enum {
    idSymbolImageTexture,
    idSymbolImageIconTexture,
    idSymbolDEMTexture,
    idSymbolDepthTexture,
    symbolTextureCount
};

enum {
    idTerrainDEMTexture,
    idTerrainMapTexture,
    idTerrainDEMArrayTexture, // sampler2DArray of packed DEM tiles for the instanced GL depth pass
    terrainTextureCount
};

enum {
    idTerrainLineDEMTexture,
    idTerrainLineDepthTexture,
    terrainLineTextureCount
};

enum {
    idTerrainContourDEMTexture,
    terrainContourTextureCount
};

static constexpr uint32_t maxTextureCountPerShader = std::max({static_cast<uint32_t>(backgroundTextureCount),
                                                               static_cast<uint32_t>(circleTextureCount),
                                                               static_cast<uint32_t>(clippingMaskTextureCount),
                                                               static_cast<uint32_t>(collisionTextureCount),
                                                               static_cast<uint32_t>(customGeometryTextureCount),
                                                               static_cast<uint32_t>(customSymbolTextureCount),
                                                               static_cast<uint32_t>(debugTextureCount),
                                                               static_cast<uint32_t>(fillTextureCount),
                                                               static_cast<uint32_t>(fillExtrusionTextureCount),
                                                               static_cast<uint32_t>(heatmapTextureCount),
                                                               static_cast<uint32_t>(hillshadeTextureCount),
                                                               static_cast<uint32_t>(colorReliefTextureCount),
                                                               static_cast<uint32_t>(lineTextureCount),
                                                               static_cast<uint32_t>(locationIndicatorTextureCount),
                                                               static_cast<uint32_t>(rasterTextureCount),
                                                               static_cast<uint32_t>(symbolTextureCount),
                                                               static_cast<uint32_t>(terrainTextureCount),
                                                               static_cast<uint32_t>(terrainLineTextureCount),
                                                               static_cast<uint32_t>(terrainContourTextureCount)});

// Vertex attribute defines
enum {
    idBackgroundPosVertexAttribute,
    backgroundVertexAttributeCount
};

enum {
    idCirclePosVertexAttribute,

    // Data driven
    idCircleColorVertexAttribute,
    idCircleRadiusVertexAttribute,
    idCircleBlurVertexAttribute,
    idCircleOpacityVertexAttribute,
    idCircleStrokeColorVertexAttribute,
    idCircleStrokeWidthVertexAttribute,
    idCircleStrokeOpacityVertexAttribute,

    circleVertexAttributeCount
};

enum {
    idClippingMaskPosVertexAttribute,
    clippingMaskVertexAttributeCount
};

enum {
    idCollisionPosVertexAttribute,
    idCollisionAnchorPosVertexAttribute,
    idCollisionExtrudeVertexAttribute,
    idCollisionPlacedVertexAttribute,
    idCollisionShiftVertexAttribute,
    collisionVertexAttributeCount
};

enum {
    idCustomGeometryPosVertexAttribute,
    idCustomGeometryTexVertexAttribute,
    customGeometryVertexAttributeCount
};

enum {
    idCustomSymbolPosVertexAttribute,
    idCustomSymbolTexVertexAttribute,
    customSymbolVertexAttributeCount
};

enum {
    idDebugPosVertexAttribute,
    debugVertexAttributeCount
};

enum {
    idFillPosVertexAttribute,

    // Data driven
    idFillColorVertexAttribute,
    idFillOpacityVertexAttribute,
    idFillOutlineColorVertexAttribute,
    idFillPatternFromVertexAttribute,
    idFillPatternToVertexAttribute,

    fillVertexAttributeCount
};

enum {
    idFillExtrusionPosVertexAttribute,
    idFillExtrusionDecimalsEdAttribute,

#if MLN_USE_FILL_EXTRUSION_INSTANCING
    idFillExtrusionOutlinePosAttribute,
#else
    idFillExtrusionNormal2DVertexAttribute,
#endif
    // Both paths: polygon centroid for terrain elevation
    idFillExtrusionCentroidVertexAttribute,

    // Data driven
    idFillExtrusionBaseVertexAttribute,
    idFillExtrusionColorVertexAttribute,
    idFillExtrusionHeightVertexAttribute,
    idFillExtrusionPatternFromVertexAttribute,
    idFillExtrusionPatternToVertexAttribute,

    fillExtrusionVertexAttributeCount
};

enum {
    idHeatmapPosVertexAttribute,

    // Data driven
    idHeatmapWeightVertexAttribute,
    idHeatmapRadiusVertexAttribute,

    heatmapVertexAttributeCount
};

enum {
    idHillshadePosVertexAttribute,
    idHillshadeTexturePosVertexAttribute,
    hillshadeVertexAttributeCount
};

enum {
    idColorReliefPosVertexAttribute,
    idColorReliefTexturePosVertexAttribute,
    colorReliefVertexAttributeCount
};

enum {
    idLinePosNormalVertexAttribute,
    idLineDataVertexAttribute,

    // Data driven
    idLineColorVertexAttribute,
    idLineBlurVertexAttribute,
    idLineOpacityVertexAttribute,
    idLineGapWidthVertexAttribute,
    idLineOffsetVertexAttribute,
    idLineWidthVertexAttribute,
    idLineFloorWidthVertexAttribute,
    idLinePatternFromVertexAttribute,
    idLinePatternToVertexAttribute,

    lineVertexAttributeCount
};

enum {
    idLocationIndicatorPosVertexAttribute,
    idLocationIndicatorTexVertexAttribute,
    locationIndicatorVertexAttributeCount
};

enum {
    idRasterPosVertexAttribute,
    idRasterTexturePosVertexAttribute,
    rasterVertexAttributeCount
};

enum {
#if MLN_USE_SYMBOL_INSTANCING
    idSymbolPosAttribute,

    idSymbolSortedInstanceAttribute,

    idSymbolPosScaleAttribute,
    idSymbolOffsetTlTrAttribute,
    idSymbolOffsetBlBrAttribute,
    idSymbolTextureRectAttribute,
    idSymbolPixelOffsetAttribute,
    idSymbolSizeSdfAttribute,
#else
    idSymbolPosOffsetAttribute,
    idSymbolDataAttribute,
    idSymbolPixelOffsetAttribute,
#endif

    idSymbolProjectedPosAttribute,
    idSymbolFadeOpacityAttribute,

    // Data driven
    idSymbolOpacityAttribute,
    idSymbolColorAttribute,
    idSymbolHaloColorAttribute,
    idSymbolHaloWidthAttribute,
    idSymbolHaloBlurAttribute,

    symbolAttributeCount
};

enum {
    idTerrainPosVertexAttribute,
    idTerrainTexturePosVertexAttribute,
    idTerrainInstanceVertexAttribute, // per-instance index for the instanced GL depth pass
    terrainVertexAttributeCount
};

enum {
    idTerrainLinePosVertexAttribute,
    idTerrainLineOtherVertexAttribute,
    idTerrainLineFlagVertexAttribute,
    idTerrainLineDistVertexAttribute,
    terrainLineVertexAttributeCount
};

enum {
    idTerrainContourPosVertexAttribute,
    terrainContourVertexAttributeCount
};

// DuckMaps fork only, task T3: the style spec's `sky` root property. SkyShader is a raw,
// once-per-frame full-screen draw (Context::renderSky, mirroring ClippingMaskProgram's own
// Context::renderTileClippingMasks - see mtl/sky.hpp's header comment), not a per-tile drawable,
// so it needs only one vertex attribute id and no per-drawable UBO array.
enum {
    idSkyPosVertexAttribute,
    skyVertexAttributeCount
};

enum {
    idWideVectorScreenPos,
    idWideVectorColor,
    idWideVectorIndex,

    wideVectorAttributeCount
};

enum {
    idWideVectorInstanceCenter,
    idWideVectorInstanceColor,
    idWideVectorInstancePrevious,
    idWideVectorInstanceNext,

    wideVectorInstanceAttributeCount
};

static constexpr uint32_t maxAttributeCountPerShader = std::max({
    static_cast<uint32_t>(backgroundVertexAttributeCount),
    static_cast<uint32_t>(circleVertexAttributeCount),
    static_cast<uint32_t>(clippingMaskVertexAttributeCount),
    static_cast<uint32_t>(collisionVertexAttributeCount),
    static_cast<uint32_t>(customGeometryVertexAttributeCount),
    static_cast<uint32_t>(customSymbolVertexAttributeCount),
    static_cast<uint32_t>(debugVertexAttributeCount),
    static_cast<uint32_t>(fillVertexAttributeCount),
    static_cast<uint32_t>(fillExtrusionVertexAttributeCount),
    static_cast<uint32_t>(heatmapVertexAttributeCount),
    static_cast<uint32_t>(hillshadeVertexAttributeCount),
    static_cast<uint32_t>(colorReliefVertexAttributeCount),
    static_cast<uint32_t>(lineVertexAttributeCount),
    static_cast<uint32_t>(locationIndicatorVertexAttributeCount),
    static_cast<uint32_t>(rasterVertexAttributeCount),
    static_cast<uint32_t>(symbolAttributeCount),
    static_cast<uint32_t>(terrainVertexAttributeCount),
    static_cast<uint32_t>(terrainLineVertexAttributeCount),
    static_cast<uint32_t>(terrainContourVertexAttributeCount),
    static_cast<uint32_t>(wideVectorAttributeCount),
    static_cast<uint32_t>(wideVectorInstanceAttributeCount),
    static_cast<uint32_t>(skyVertexAttributeCount),
});

} // namespace shaders
} // namespace mln
