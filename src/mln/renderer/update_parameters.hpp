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
    /// sized to its tile's screen footprint: two texels for every screen pixel of the tile's
    /// width (MapLibre GL JS's qualityFactor: a tile 512 pixels wide keeps the full 1024
    /// target), the width scaled by the tile's foreshortening (the sine of the view's
    /// elevation angle at the tile) raised to `drapeDistanceCurve` - 0 sizes by width alone,
    /// 0.5 by the tile's screen area, 1 by its screen height - rounded up to a power of two,
    /// never above 1024 and never below 1024 * `drapeFarSizeFactor`. A factor of 1.0
    /// switches the dial off.
    double drapeDistanceCurve = 0.5;
    /// Bumped by Map::setDrapeTextureDial so the renderer resizes every drape target in view
    /// at once (hysteresis skipped for that frame): a dial moved in the panel must show.
    double drapeFarSizeFactor = 0.25;
    /// Texels of drape texture per screen pixel of a tile's width: 2 is GL JS's quality
    /// factor; 1 halves every target's edge (a quarter of the memory), visibly softer.
    /// The one drape dial whose effect can be seen: the size-by-footprint rule above keeps
    /// every target at this density whatever the distance, so the floor and the curve only
    /// ever move memory, not the picture (measured 17 September: floor 64 with curve 1
    /// against the defaults differs in 0.08 percent of pixels at Gavarnie pitch 80).
    double drapeTexelsPerPixel = 2.0;
    std::uint64_t drapeDialEpoch = 0;
    // Debug: when set, RenderTerrain logs the camera eye's clearance over the terrain
    // (ABOVE-GROUND ...). Off by default; the per-frame elevation sampling is skipped entirely
    // when off, so it has no cost unless explicitly enabled (Map::setDebugAboveGroundLog).
    bool debugAboveGroundLog = false;
};

} // namespace mln
