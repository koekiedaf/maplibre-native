#pragma once

#include <mln/map/mode.hpp>
#include <mln/actor/scheduler.hpp>

#include <memory>
#include <numbers>
#include <set>

#include <mapbox/std/weak.hpp>

namespace mln {

class TransformState;
class FileSource;
class AnnotationManager;
class ImageManager;
class GlyphManager;
class UnwrappedTileID;

namespace gfx {
class DynamicTextureAtlas;
using DynamicTextureAtlasPtr = std::shared_ptr<gfx::DynamicTextureAtlas>;
} // namespace gfx

namespace util {
class TileElevationProvider;
} // namespace util

class TileParameters {
public:
    const float pixelRatio;
    const MapDebugOptions debugOptions;
    const TransformState& transformState;
    std::shared_ptr<FileSource> fileSource;
    const MapMode mode;
    mapbox::base::WeakPtr<AnnotationManager> annotationManager;
    std::shared_ptr<ImageManager> imageManager;
    std::shared_ptr<GlyphManager> glyphManager;
    const uint8_t prefetchZoomDelta;
    TaggedScheduler threadPool;
    double tileLodMinRadius = 3;
    double tileLodScale = 1;
    double tileLodPitchThreshold = (60.0 / 180.0) * std::numbers::pi;
    double tileLodZoomShift = 0;
    TileLodMode tileLodMode = TileLodMode::Default;
    gfx::DynamicTextureAtlasPtr dynamicTextureAtlas;
    bool isUpdateSynchronous = false;
    /// Terrain elevation for the tile cover; null when there is no terrain, which
    /// leaves the cover flat. See util::TileElevationProvider.
    const util::TileElevationProvider* elevationProvider = nullptr;
    /// DuckMaps fork only, task M1c: tiles this source must load in addition to its own
    /// cover, because another consumer (the terrain mesh) needs them; null for every
    /// source that has no such consumer. Set only on the terrain DEM source, from the
    /// terrain mesh's previous frame's cover (RenderTerrain::getLastFrameMeshCover), and
    /// consumed by TilePyramid::update to fold into idealTiles.
    const std::set<UnwrappedTileID>* requiredTiles = nullptr;
    /// Round 8 (David: "pieces of terrain flash white during a turn"): for the DRAPED sources
    /// (every source but the terrain's DEM), `requiredTiles` is the terrain mesh cover and is
    /// folded in at this source's own zooms - a mesh tile finer than the source's ideal zoom
    /// asks for the ideal-zoom tile (the very id the source's own cover would use, so no
    /// duplicate overzoomed parse), a coarser one asks for itself. The mesh cover reaches
    /// one tile ring past the view, so the ground a turn brings in has its draped tiles
    /// already loading before its drape target bakes; measured on the 13 mini, a target
    /// baked empty (paper) for 1 to 3 frames, 70 to 400 ms, until the vector tiles arrived.
    bool requiredTilesAreMeshCover = false;
};

} // namespace mln
