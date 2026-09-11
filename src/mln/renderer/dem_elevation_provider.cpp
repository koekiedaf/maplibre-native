#include <mln/renderer/dem_elevation_provider.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/renderer/buckets/hillshade_bucket.hpp>
#include <mln/renderer/render_source.hpp>
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
    //
    // Deliberately getLoadedTiles(), not getRawRenderTiles(): this aggregate must not
    // depend on which tiles this source's own cover happened to retain last frame, or
    // it inherits the same self-reference getTileElevationRange below is built to avoid.
    if (!demSource) {
        return;
    }
    loadedTiles = demSource->getLoadedTiles();
    double minEle = std::numeric_limits<double>::max();
    double maxEle = std::numeric_limits<double>::lowest();
    for (const auto* tile : loadedTiles) {
        if (!tile || tile->kind != Tile::Kind::RasterDEM) {
            continue;
        }
        const auto* demTile = static_cast<const RasterDEMTile*>(tile);
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

    // getLoadedTiles(), not getRawRenderTiles(): a tile that has loaded keeps answering
    // from its own data every frame from here on, whether or not this frame's cover
    // retains it for rendering. That is what makes the answer for a given tile id
    // monotone once loaded - see TilePyramid::getLoadedTiles() and the header comment
    // on this class for the closed loop this breaks: a tile whose own true elevation
    // range fails the frustum test would otherwise be dropped from the render set,
    // which used to make its OWN next query fall back to the (taller) aggregate range,
    // pass the test, and be added back - forever alternating between the two answers.
    if (loadedTiles.empty()) {
        return std::nullopt;
    }

    // The tile's own DEM, or failing that the deepest loaded ancestor: an ancestor's
    // range covers this tile's area, so it stays conservative, just looser.
    const DEMData* best = nullptr;
    uint8_t bestZoom = 0;
    for (const auto* tile : loadedTiles) {
        if (!tile || tile->kind != Tile::Kind::RasterDEM) {
            continue;
        }
        const auto& candidate = tile->id.canonical;
        const bool covers = candidate == id || id.isChildOf(candidate);
        if (!covers || (best && candidate.z <= bestZoom)) {
            continue;
        }
        const auto* demTile = static_cast<const RasterDEMTile*>(tile);
        const auto* bucket = const_cast<RasterDEMTile*>(demTile)->getBucket();
        if (!bucket) {
            continue;
        }
        best = &bucket->getDEMData();
        bestZoom = candidate.z;
        if (candidate == id) {
            break; // exact match; nothing looser can improve on it
        }
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
