#include <mln/renderer/dem_elevation_provider.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/renderer/buckets/hillshade_bucket.hpp>
#include <mln/renderer/render_source.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/tile/raster_dem_tile.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace mln {

namespace {

// Debug-only instrumentation for the DUCKMAPS_ELEVATION_TRACE diagnosis. Checked once
// (getenv is not free to call every query) and otherwise entirely inert.
bool elevationTraceEnabled() {
    static const bool enabled = [] {
        const char* path = std::getenv("DUCKMAPS_ELEVATION_TRACE");
        return path && *path;
    }();
    return enabled;
}

std::map<std::string, std::string>& elevationTraceLog() {
    static std::map<std::string, std::string> log;
    return log;
}

std::string tileIdKey(const CanonicalTileID& id) {
    return std::to_string(static_cast<int>(id.z)) + "/" + std::to_string(id.x) + "/" + std::to_string(id.y);
}

} // namespace

std::string DEMElevationProvider::debugDrainElevationQueries() {
    if (!elevationTraceEnabled()) {
        return "[]";
    }
    auto& log = elevationTraceLog();
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& entry : log) {
        if (!first) {
            os << ",";
        }
        first = false;
        os << "{\"id\":\"" << entry.first << "\"," << entry.second << "}";
    }
    os << "]";
    log.clear();
    return os.str();
}

DEMElevationProvider::DEMElevationProvider(const RenderSource* demSource_, double exaggeration_)
    : demSource(demSource_),
      exaggeration(exaggeration_) {
    // Precompute the aggregate elevation range of every loaded DEM tile (once, here,
    // not per query). It is the fallback for tiles with no loaded DEM, so the cover
    // dilation can re-test a not-yet-loaded frontier neighbour as if it were about as
    // tall/deep as the terrain already in view.
    if (!demSource) {
        return;
    }
    const auto renderTiles = demSource->getRawRenderTiles();
    double minEle = std::numeric_limits<double>::max();
    double maxEle = std::numeric_limits<double>::lowest();
    for (const auto& renderTile : *renderTiles) {
        const auto& tile = renderTile.getTile();
        if (tile.kind != Tile::Kind::RasterDEM) {
            continue;
        }
        const auto* demTile = static_cast<const RasterDEMTile*>(&tile);
        const auto* bucket = const_cast<RasterDEMTile*>(demTile)->getBucket();
        if (!bucket) {
            continue;
        }
        const auto& demData = bucket->getDEMData();
        minEle = std::min(minEle, static_cast<double>(demData.getMinElevation()));
        maxEle = std::max(maxEle, static_cast<double>(demData.getMaxElevation()));
    }
    if (minEle <= maxEle) {
        loadedRange = Range<double>{minEle * exaggeration, maxEle * exaggeration};
    }
}

std::optional<Range<double>> DEMElevationProvider::getTileElevationRange(const CanonicalTileID& id) const {
    if (!demSource) {
        return std::nullopt;
    }

    const auto renderTiles = demSource->getRawRenderTiles();
    if (renderTiles->empty()) {
        return std::nullopt;
    }

    // The deepest loaded STRICT ancestor of this tile, never the tile's own DEM. An
    // ancestor's range contains its descendants' by construction, so this stays
    // conservative, just looser.
    //
    // Answering from the tile's own DEM is what made the cover oscillate at frame rate
    // (task C1, and proved in task C2c's trace). This provider is the elevation input to
    // the DEM source's own tile cover: RenderOrchestrator::createRenderTree builds one
    // per frame and hands the same instance to every source's update, the DEM source
    // included. So while the tile's own DEM could answer, the answer for a tile depended
    // on whether that tile was currently rendered, and the cover is what decides that.
    // Measured at Gavarnie with the camera provably still, four z15 tiles alternated
    // every frame between their own range (15/16387/12078: 2517 to 2784 m, a 267 m box
    // up at two and a half kilometres, which fails the elevated frustum test and drops
    // the tile) and the aggregate `loadedRange` fallback that answered the moment it was
    // dropped (0 to 3346 m, a box three kilometres tall starting at sea level, which
    // passes and puts it back). 602 changes in 605 still frames.
    //
    // Restricting the answer to strict ancestors breaks that dependency rather than
    // hiding it. A tile's elevation answer now comes only from levels ABOVE it, and the
    // cover walk is itself top-down and terminates at the root, so the dependency graph
    // has no cycle left to oscillate around. It is also TIGHTER than what the tiles in
    // question were actually getting: a z14 parent's range over four z15 tiles is far
    // narrower than the whole view's 0 to 3346. Nothing is smoothed, held for a number
    // of frames or clamped, and a tile that genuinely leaves the view still leaves it.
    const DEMData* best = nullptr;
    uint8_t bestZoom = 0;
    for (const auto& renderTile : *renderTiles) {
        const auto& tile = renderTile.getTile();
        if (tile.kind != Tile::Kind::RasterDEM) {
            continue;
        }
        const auto& candidate = renderTile.id.canonical;
        // Strictly coarser, and covering. `isChildOf` reports true for a z0 parent even
        // when the tile is itself z0, so the level test is spelled out rather than left
        // to it.
        const bool covers = candidate.z < id.z && id.isChildOf(candidate);
        if (!covers || (best && candidate.z <= bestZoom)) {
            continue;
        }
        const auto* demTile = static_cast<const RasterDEMTile*>(&tile);
        const auto* bucket = const_cast<RasterDEMTile*>(demTile)->getBucket();
        if (!bucket) {
            continue;
        }
        best = &bucket->getDEMData();
        bestZoom = candidate.z;
    }

    if (!best) {
        // No DEM covers this tile. Fall back to the range of terrain loaded in view
        // (nullopt only if nothing is loaded), so the cover dilation's frustumCull can
        // decide the tile on its (assumed) elevation instead of treating it as flat.
        if (elevationTraceEnabled()) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2);
            if (loadedRange) {
                os << "\"branch\":\"loaded\",\"min\":" << loadedRange->min << ",\"max\":" << loadedRange->max;
            } else {
                os << "\"branch\":\"none\"";
            }
            elevationTraceLog()[tileIdKey(id)] = os.str();
        }
        return loadedRange;
    }

    // Exaggeration is applied to the mesh in the terrain vertex shader, so the bounds
    // have to carry it too, or an exaggerated peak would still be culled.
    const Range<double> result{best->getMinElevation() * exaggeration, best->getMaxElevation() * exaggeration};
    if (elevationTraceEnabled()) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "\"branch\":\"" << (bestZoom == id.z ? "own" : "ancestor") << "\",\"srcZ\":"
           << static_cast<int>(bestZoom) << ",\"min\":" << result.min << ",\"max\":" << result.max;
        elevationTraceLog()[tileIdKey(id)] = os.str();
    }
    return result;
}

} // namespace mln
