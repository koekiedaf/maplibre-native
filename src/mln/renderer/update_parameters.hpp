#pragma once

#include <mln/map/mode.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/style/light.hpp>
#include <mln/style/terrain.hpp>
#include <mln/style/sky.hpp>
#include <mln/style/image.hpp>
#include <mln/style/source.hpp>
#include <mln/style/layer.hpp>
#include <mln/text/glyph.hpp>
#include <mln/util/chrono.hpp>
#include <mln/util/immutable.hpp>

#include <numbers>
#include <vector>

#include <mapbox/std/weak.hpp>

namespace mln {

class AnnotationManager;
class FileSource;

class UpdateParameters {
public:
    const bool styleLoaded;
    const MapMode mode;
    const float pixelRatio;
    const MapDebugOptions debugOptions;
    const TimePoint timePoint;
    const TransformState transformState;

    const std::string glyphURL;
    std::shared_ptr<FontFaces> fontFaces;
    const bool spriteLoaded;
    const style::TransitionOptions transitionOptions;
    const Immutable<style::Light::Impl> light;
    const std::optional<Immutable<style::Terrain::Impl>> terrain;
    // DuckMaps fork only, task T3: the style spec's `sky` root property.
    const std::optional<Immutable<style::Sky::Impl>> sky;
    const Immutable<std::vector<Immutable<style::Image::Impl>>> images;
    const Immutable<std::vector<Immutable<style::Source::Impl>>> sources;
    const Immutable<std::vector<Immutable<style::Layer::Impl>>> layers;

    mapbox::base::WeakPtr<AnnotationManager> annotationManager;
    std::shared_ptr<FileSource> fileSource;

    const uint8_t prefetchZoomDelta;

    // For still image requests, render requested
    const bool stillImageRequest;

    const bool crossSourceCollisions;

    const bool fastPFOREnabled = false;

    double tileLodMinRadius = 3;
    double tileLodScale = 1;
    double tileLodPitchThreshold = (60.0 / 180.0) * std::numbers::pi;
    double tileLodZoomShift = 0;
    TileLodMode tileLodMode = TileLodMode::Default;
    TerrainLoadMode terrainLoadMode = TerrainLoadMode::Quality;
    TerrainSkirtLength terrainSkirtLength = TerrainSkirtLength::Auto;
    /// Performance round, Phase 2 dial 1 (drape texture size by distance). A drape target is
    /// sized to its tile's screen footprint: `drapeTexelsPerPixel` texels for every screen
    /// pixel the tile spans (2, MapLibre GL JS's qualityFactor, keeps the full 1024 target for
    /// a tile 512 pixels wide), rounded up to a power of two, never above 1024 and never
    /// below 1024 * `drapeFarSizeFactor`. 1.0 for the factor switches the dial off.
    double drapeTexelsPerPixel = 2.0;
    double drapeFarSizeFactor = 0.25;
    // Debug: when set, RenderTerrain logs the camera eye's clearance over the terrain
    // (ABOVE-GROUND ...). Off by default; the per-frame elevation sampling is skipped entirely
    // when off, so it has no cost unless explicitly enabled (Map::setDebugAboveGroundLog).
    bool debugAboveGroundLog = false;
};

} // namespace mln
