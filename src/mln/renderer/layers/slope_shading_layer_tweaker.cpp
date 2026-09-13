#include <mln/renderer/layers/slope_shading_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_terrain.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/slope_shading_layer_ubo.hpp>
#include <mln/style/layers/slope_shading_layer_impl.hpp>
#include <mln/style/layers/slope_shading_layer_properties.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/image.hpp>
#include <mln/util/math.hpp>
#include <mln/util/projection.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

// ---------------------------------------------------------------------------------------------
// The lookup table, ported line for line from the web engine's contours3d.js SLOPE_PRESETS /
// zoneAt() / buildLut() (see container/server/app/map/assets/contours3d.js ~355-425). David chose
// these three configurations and no editor, 9 September 2026 - the band boundaries and colours
// belong to the preset itself, not to a tunable dial (see scripts/style-spec.mjs's own comment on
// "slope-shading-preset" for why this lives here, ported, rather than in style.py).

struct SlopeZone {
    float minSlope = -1.f;   // -1 = unset (no lower bound)
    float maxSlope = -1.f;   // -1 = unset (no upper bound); inclusive of whole degrees, see below
    float minAspect = -1.f;  // -1 = no aspect gate
    float maxAspect = -1.f;
    std::array<uint8_t, 3> color{};
};

constexpr int LUT_W = 256;
constexpr int LUT_H = 256;

// A zone's slope pair is an INCLUSIVE band of whole degrees: {26, 29} means every slope from 26.0
// up to but not including 30.0 - see scripts/style-spec.mjs and contours3d.js's own comment on
// why this keeps the avalanche bands contiguous with no uncoloured gap at each whole-degree
// boundary.
const SlopeZone* zoneAt(const std::vector<SlopeZone>& zones, float slope, float aspect) {
    for (const auto& z : zones) {
        if (z.minSlope >= 0.f && slope < z.minSlope) continue;
        if (z.maxSlope >= 0.f && slope >= z.maxSlope + 1.f) continue;
        if (z.minAspect >= 0.f) {
            if (!(aspect >= z.minAspect && aspect < z.maxAspect)) continue;
        }
        return &z;
    }
    return nullptr;
}

std::vector<SlopeZone> avalancheZones() {
    return {
        {26, 29, -1, -1, {241, 204, 56}},
        {30, 31, -1, -1, {255, 146, 46}},
        {32, 34, -1, -1, {255, 95, 43}},
        {35, 45, -1, -1, {215, 7, 15}},
        {46, 50, -1, -1, {77, 39, 157}},
        {51, 59, -1, -1, {0, 49, 155}},
        {59, -1, -1, -1, {0, 0, 0}},
    };
}

std::vector<SlopeZone> aspectZones() {
    static constexpr std::array<std::array<uint8_t, 3>, 16> wheel{{
        {255, 48, 0}, {255, 143, 0}, {255, 239, 0}, {175, 255, 0}, {80, 255, 0}, {0, 255, 16},
        {0, 255, 112}, {0, 255, 207}, {0, 207, 255}, {0, 112, 255}, {0, 16, 255}, {80, 0, 255},
        {175, 0, 255}, {255, 0, 239}, {255, 0, 143}, {255, 0, 48},
    }};
    std::vector<SlopeZone> zones;
    zones.reserve(16);
    for (int i = 0; i < 16; ++i) {
        zones.push_back({2.f, -1.f, i * 22.5f, (i + 1) * 22.5f, wheel[static_cast<size_t>(i)]});
    }
    return zones;
}

std::vector<SlopeZone> flatZones() {
    return {{0, 1, -1, -1, {0, 255, 68}}};
}

// 0 = avalanche, 1 = aspect, 2 = flat - see scripts/style-spec.mjs's own comment on
// "slope-shading-preset" for why the paint property carries this small integer rather than a
// first-class style-spec string enum. Anything outside [0, 2] (a hand-written style, or a value
// this build predates) falls back to avalanche rather than drawing nothing.
std::vector<SlopeZone> zonesForPreset(int presetId) {
    switch (presetId) {
        case 1: return aspectZones();
        case 2: return flatZones();
        default: return avalancheZones();
    }
}

std::vector<uint8_t> buildLut(const std::vector<SlopeZone>& zones) {
    std::vector<uint8_t> data(static_cast<size_t>(LUT_W) * LUT_H * 4, 0);
    for (int ai = 0; ai < LUT_H; ++ai) {
        const float aspect = (static_cast<float>(ai) + 0.5f) / static_cast<float>(LUT_H) * 360.f;
        for (int si = 0; si < LUT_W; ++si) {
            const float slope = (static_cast<float>(si) + 0.5f) / static_cast<float>(LUT_W) * 90.f;
            const SlopeZone* z = zoneAt(zones, slope, aspect);
            const size_t o = (static_cast<size_t>(ai) * LUT_W + static_cast<size_t>(si)) * 4;
            if (z) {
                data[o + 0] = z->color[0];
                data[o + 1] = z->color[1];
                data[o + 2] = z->color[2];
                data[o + 3] = 255;
            }
        }
    }
    return data;
}

// contours3d.js's tileMetres(): how much ground a whole tile spans east-west, in metres, at its
// own latitude - the only thing that turns a difference in metres of elevation into a slope
// angle. Reached through this engine's own Projection rather than reimplemented by hand: a
// tile's own centre latitude via Projection::unproject at that tile's world-pixel position, then
// Projection::getMetersPerPixelAtLatitude (the same call Camera::getWorldToCamera uses) times the
// tile's own pixel width.
float metersPerTile(const UnwrappedTileID& tileID) {
    const auto& c = tileID.canonical;
    if (c.z > 30) {
        return 0.f; // guards the exp2 below; no real tile is ever this deep
    }
    const double scale = std::exp2(static_cast<double>(c.z));
    const Point<double> centerPx{(static_cast<double>(c.x) + 0.5) * util::tileSize_D,
                                 (static_cast<double>(c.y) + 0.5) * util::tileSize_D};
    const LatLng centerLatLng = Projection::unproject(centerPx, scale);
    const double metersPerPixel = Projection::getMetersPerPixelAtLatitude(centerLatLng.latitude(),
                                                                          static_cast<double>(c.z));
    return static_cast<float>(util::tileSize_D * metersPerPixel);
}

} // namespace

void SlopeShadingLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    auto& context = parameters.context;
    const auto& properties = static_cast<const SlopeShadingLayerProperties&>(*evaluatedProperties);
    const auto& evaluated = properties.evaluated;

#ifndef NDEBUG
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    // "preset" never reaches the shader as a number - it only selects which lookup texture is
    // bound (see slope_shading_layer_ubo.hpp's own comment). Rounded and clamped defensively: a
    // hand-written style or an interpolated value (this property is transition:false, but a
    // malformed style.json is still possible) must not read out of zonesForPreset's switch.
    const float presetRaw = evaluated.get<style::SlopeShadingPreset>();
    const int presetId = std::clamp(static_cast<int>(std::lround(presetRaw)), 0, 2);
    if (!lutTexture || presetId != cachedPresetId) {
        const std::vector<uint8_t> pixels = buildLut(zonesForPreset(presetId));
        if (!lutTexture) {
            lutTexture = context.createTexture2D();
        }
        lutTexture->setFormat(gfx::TexturePixelType::RGBA, gfx::TextureChannelDataType::UnsignedByte);
        lutTexture->setSamplerConfiguration({.filter = gfx::TextureFilterType::Nearest,
                                             .wrapU = gfx::TextureWrapType::Clamp,
                                             .wrapV = gfx::TextureWrapType::Clamp});
        lutTexture->upload(pixels.data(), Size{static_cast<uint32_t>(LUT_W), static_cast<uint32_t>(LUT_H)});
        cachedPresetId = presetId;
    }

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const SlopeShadingEvaluatedPropsUBO evaluatedPropsUBO = {
            .opacity = evaluated.get<style::SlopeShadingOpacity>(),
            // Re-derived for Metal's [0,1] clip convention, same reasoning and same halving as
            // TerrainContourEvaluatedPropsUBO's own depth_bias (see that struct's comment for the
            // full derivation against contours3d.js's DEPTH_BIAS of 0.0003).
            .depth_bias = 0.00015f,
            .slope_bias = 8.0f,
            .pad0 = 0.0f,
        };
        context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &evaluatedPropsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idSlopeShadingEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    std::vector<SlopeShadingDrawableUBO> drawableUBOVector(layerGroup.getDrawableCount());
    std::vector<SlopeShadingTilePropsUBO> tilePropsUBOVector(layerGroup.getDrawableCount());
#endif

    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }
        const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();

        // Same Metal clip-space z remap terrain-contour's own tweaker applies, for the identical
        // reason: this mesh is drawn directly in world space over RenderTerrain's own surface
        // (never draped), reaching into the near half of the GL clip volume at ordinary pitch.
        // Without this remap the geometry is silently clipped away in its entirety on Metal - see
        // terrain_contour_layer_tweaker.cpp's own comment on this exact line for the measured
        // symptom (zero pixels at every pitch, including pitch 0).
        mat4 matrix = parameters.matrixForTile(tileID);
#if !MLN_RENDER_BACKEND_OPENGL
        matrix[2] = 0.5 * (matrix[2] + matrix[3]);
        matrix[6] = 0.5 * (matrix[6] + matrix[7]);
        matrix[10] = 0.5 * (matrix[10] + matrix[11]);
        matrix[14] = 0.5 * (matrix[14] + matrix[15]);
#endif

        std::optional<RenderTerrain::TerrainData> terrainData;
        if (parameters.terrain) {
            terrainData = parameters.terrain->getTerrainData(tileID);
            drawable.setTexture(
                terrainData ? terrainData->demTexture : parameters.terrain->getPlaceholderDEMTexture(context),
                idSlopeShadingDEMTexture);
        } else {
            drawable.setTexture(context.getPlaceholderTexture2D(), idSlopeShadingDEMTexture);
        }
        drawable.setTexture(lutTexture, idSlopeShadingLutTexture);

        const auto demCoords = terrainData ? terrainData->demCoords : std::array<float, 4>{{0, 0, 0, 0}};
        const auto demUnpack = parameters.terrain ? parameters.terrain->getDEMUnpackVector()
                                                  : std::array<float, 4>{{0, 0, 0, 0}};
        const float demDim = terrainData ? terrainData->demDim : 0.0f;
        const float demExaggeration = parameters.terrain ? parameters.terrain->getExaggeration() : 0.0f;
        const float demEnabled = terrainData ? 1.0f : 0.0f;
        const float mPerExtent = metersPerTile(tileID) / static_cast<float>(util::EXTENT);

#if MLN_UBO_CONSOLIDATION
        drawableUBOVector[i] = {
#else
        const SlopeShadingDrawableUBO drawableUBO = {
#endif
            .matrix = util::cast<float>(matrix),
            .dem_coords = demCoords,
            .dem_unpack = demUnpack,
            .dem_dim = demDim,
            .dem_exaggeration = demExaggeration,
            .dem_enabled = demEnabled,
            .pad0 = 0.0f,
        };
#if MLN_UBO_CONSOLIDATION
        tilePropsUBOVector[i] = {
#else
        const SlopeShadingTilePropsUBO tilePropsUBO = {
#endif
            .dem_coords = demCoords,
            .dem_unpack = demUnpack,
            .dem_dim = demDim,
            .dem_exaggeration = demExaggeration,
            .dem_enabled = demEnabled,
            .m_per_extent = mPerExtent,
        };

#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(i++);
#else
        auto& drawableUniforms = drawable.mutableUniformBuffers();
        drawableUniforms.createOrUpdate(idSlopeShadingDrawableUBO, &drawableUBO, context);
        drawableUniforms.createOrUpdate(idSlopeShadingTilePropsUBO, &tilePropsUBO, context);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    const size_t drawableUBOVectorSize = sizeof(SlopeShadingDrawableUBO) * drawableUBOVector.size();
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < drawableUBOVectorSize) {
        drawableUniformBuffer = context.createUniformBuffer(
            drawableUBOVector.data(), drawableUBOVectorSize, false, true);
    } else {
        drawableUniformBuffer->update(drawableUBOVector.data(), drawableUBOVectorSize);
    }
    layerUniforms.set(idSlopeShadingDrawableUBO, drawableUniformBuffer);

    const size_t tilePropsUBOVectorSize = sizeof(SlopeShadingTilePropsUBO) * tilePropsUBOVector.size();
    if (!tilePropsUniformBuffer || tilePropsUniformBuffer->getSize() < tilePropsUBOVectorSize) {
        tilePropsUniformBuffer = context.createUniformBuffer(
            tilePropsUBOVector.data(), tilePropsUBOVectorSize, false, true);
    } else {
        tilePropsUniformBuffer->update(tilePropsUBOVector.data(), tilePropsUBOVectorSize);
    }
    layerUniforms.set(idSlopeShadingTilePropsUBO, tilePropsUniformBuffer);
#endif
}

} // namespace mln
