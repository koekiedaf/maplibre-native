#include <mln/renderer/tile_pyramid.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_source.hpp>
#include <mln/renderer/tile_parameters.hpp>
#include <mln/renderer/query.hpp>
#include <mln/map/transform.hpp>
#include <mln/math/clamp.hpp>
#include <mln/actor/scheduler.hpp>
#include <mln/util/tile_cover.hpp>
#include <mln/util/tile_range.hpp>
#include <mln/util/enum.hpp>
#include <mln/util/logging.hpp>

#include <mln/algorithm/update_renderables.hpp>

#include <mapbox/geometry/envelope.hpp>

#include <cmath>
#include <algorithm>
#include <set>

namespace mln {

using namespace style;

namespace {
TileObserver nullObserver;
const std::map<OverscaledTileID, std::unique_ptr<Tile>> emptyPrefetchedTiles;
} // namespace

TilePyramid::TilePyramid(const TaggedScheduler& threadPool_)
    : cache(threadPool_),
      observer(&nullObserver) {}

TilePyramid::~TilePyramid() = default;

bool TilePyramid::isLoaded() const {
    for (const auto& pair : tiles) {
        if (!pair.second->isComplete()) {
            return false;
        }
    }

    return true;
}

Tile* TilePyramid::getTile(const OverscaledTileID& tileID) {
    auto it = tiles.find(tileID);
    return it == tiles.end() ? cache.get(tileID) : it->second.get();
}

const Tile* TilePyramid::getRenderedTile(const UnwrappedTileID& tileID) const {
    auto it = renderedTiles.find(tileID);
    return it != renderedTiles.end() ? &it->second.get() : nullptr;
}

void TilePyramid::update(const std::vector<Immutable<style::LayerProperties>>& layers,
                         const bool needsRendering,
                         const bool needsRelayout,
                         const TileParameters& parameters,
                         const style::Source::Impl& sourceImpl,
                         const uint16_t tileSize,
                         const Range<uint8_t> zoomRange,
                         std::optional<LatLngBounds> bounds,
                         std::function<std::unique_ptr<Tile>(const OverscaledTileID&, TileObserver*)> createTile) {
    // If we need a relayout, abandon any cached tiles; they're now stale.
    if (needsRelayout) {
        cache.clear();
    }

    // If we're not going to render anything, move our existing tiles into
    // the cache (if they're not stale) or abandon them, and return.
    if (!needsRendering) {
        for (auto& entry : tiles) {
            if (!needsRelayout) {
                // These tiles are invisible, we set optional necessity
                // for them and thus suppress network requests on
                // tiles expiration (see `OnlineFileRequest`).
                entry.second->setNecessity(TileNecessity::Optional);
                cache.add(entry.first, std::move(entry.second));
            } else {
                cache.deferredRelease(std::move(entry.second));
            }
        }

        tiles.clear();
        renderedTiles.clear();
        cache.deferPendingReleases();

        return;
    }

    handleWrapJump(static_cast<float>(parameters.transformState.getLatLng().longitude()));

    // Optionally shift the zoom level
    double zoom = util::clamp<double>(parameters.transformState.getZoom() + parameters.tileLodZoomShift,
                                      parameters.transformState.getMinZoom(),
                                      parameters.transformState.getMaxZoom());

    const auto type = sourceImpl.type;
    // Determine the overzooming/underzooming amounts and required tiles.
    int32_t overscaledZoom = util::coveringZoomLevel(zoom, type, tileSize);
    int32_t tileZoom = overscaledZoom;
    int32_t panZoom = zoomRange.max;

    const std::optional<uint8_t>& sourcePrefetchZoomDelta = sourceImpl.getPrefetchZoomDelta();
    const std::optional<uint8_t>& maxParentTileOverscaleFactor = sourceImpl.getMaxOverscaleFactorForParentTiles();
    const Duration minimumUpdateInterval = sourceImpl.getMinimumTileUpdateInterval();
    const bool isVolatile = sourceImpl.isVolatile();

    std::vector<OverscaledTileID> idealTiles;
    std::vector<OverscaledTileID> panTiles;

    util::TileCoverParameters tileCoverParameters = {
        .transformState = parameters.transformState,
        .tileLodMinRadius = parameters.tileLodMinRadius,
        .tileLodScale = parameters.tileLodScale,
        .tileLodPitchThreshold = parameters.tileLodPitchThreshold,
        .tileLodMode = parameters.tileLodMode,
        .roundZoom = type == SourceType::Raster || type == SourceType::Video,
        .elevationProvider = parameters.elevationProvider};

    if (std::cmp_greater_equal(overscaledZoom, zoomRange.min)) {
        int32_t idealZoom = std::min<int32_t>(zoomRange.max, overscaledZoom);

        // Make sure we're not reparsing overzoomed raster tiles.
        if (type == SourceType::Raster) {
            tileZoom = idealZoom;
        }

        // Only attempt prefetching in continuous mode.
        if (parameters.mode == MapMode::Continuous && type != style::SourceType::GeoJSON &&
            type != style::SourceType::Annotations) {
            // Request lower zoom level tiles (if configured to do so) in an attempt
            // to show something on the screen faster at the cost of a little of bandwidth.
            const uint8_t prefetchZoomDelta = sourcePrefetchZoomDelta ? *sourcePrefetchZoomDelta
                                                                      : parameters.prefetchZoomDelta;
            if (prefetchZoomDelta) {
                panZoom = std::max<int32_t>(tileZoom - prefetchZoomDelta, zoomRange.min);
            }

            if (panZoom < idealZoom) {
                panTiles = util::tileCover(tileCoverParameters, panZoom, zoomRange);
            }
        }

        idealTiles = util::tileCover(tileCoverParameters, idealZoom, zoomRange, tileZoom);
        if (parameters.mode == MapMode::Tile && type != SourceType::Raster && type != SourceType::RasterDEM &&
            idealTiles.size() > 1) {
            mln::Log::Warning(mln::Event::General,
                              "Provided camera options returned " + std::to_string(idealTiles.size()) +
                                  " tiles, only " + util::toString(idealTiles[0]) + " is taken in Tile mode.");
            idealTiles = {idealTiles[0]};
        }
    }

    // DuckMaps fork only, task M1c: fold in the tiles the terrain mesh's previous frame
    // needs from THIS source (RenderTerrain::getLastFrameMeshCover, passed through as
    // TileParameters::requiredTiles by RenderOrchestrator::createRenderTree; null for
    // every source but the terrain DEM source, so this is a no-op everywhere else).
    //
    // The mesh cover (RenderTerrain::computeMeshCover) is computed independently of this
    // cover: it uses zoomRange {0, util::DEFAULT_MAX_ZOOM} and then dilates the result by
    // one 8-neighbour ring, with neither bound tying it to this source's own zoomRange -
    // so a tile the mesh needs can sit entirely outside what idealTiles above ever
    // produces. Measured at the Gavarnie wall: mesh tile 12/2047/1510 is in the mesh
    // cover every one of eight traced runs, and its DEM tile (and every ancestor down to
    // z8) is on the server and answers in milliseconds, but this source's own cover never
    // reached it, so the mesh tile bound a z11 ancestor in four runs and the flat
    // placeholder (demZ -1) in the other four.
    //
    // For a required tile at canonical zoom z, the DEM tile that covers it is its
    // ancestor at min(z, zoomRange.max) - this source cannot serve anything deeper than
    // its own maxzoom. If that ancestor zoom is below zoomRange.min, this source has
    // nothing covering the tile at all (e.g. the mesh descended below the DEM's own
    // minzoom), so it is skipped rather than requesting a tile this source cannot serve.
    // The OverscaledTileID is built with the same overscaledZ convention util::tileCover
    // uses for an overscaled (underzoomed) tile above: overscaledZ carries the deeper zoom
    // actually wanted (here, the mesh tile's own z) while canonical sits at the ancestor
    // this source can actually load (see the `node.zoom == maxZoom ? overscaledZoom :
    // node.zoom` ternary in util::tileCover). Duplicates already present in idealTiles are
    // skipped; panTiles (prefetch) is left untouched - the mesh's need is for the tile
    // itself, not a lower-res placeholder ahead of it.
    if (parameters.requiredTiles && !parameters.requiredTiles->empty()) {
        // Performance round, Phase 1 item 4 (the frustum check). The frustum cover above,
        // computed at the ideal zoom and clamped to this source's min zoom, asks for every
        // tile out to the horizon at a pitched, zoomed-out camera: measured at Gavarnie z9.66
        // pitch 78, 1412 z8 DEM tiles resident for a 52-tile mesh whose far tiles sit at z2
        // to z7 and can never sample them (a mesh tile binds its own DEM or an ancestor,
        // never a descendant). Those tiles were 3.2 GB of textures and a 1.9 GB process - the
        // colleague's crash. A frustum tile is dropped only when it lies TWO OR MORE levels
        // beneath a mesh tile: the mesh there is coarse on purpose and cannot use it. Every
        // other frustum tile stays, in particular tiles over ground the mesh cover has not
        // reached: the elevation-aware LOD reads DEM there to decide how coarse the far
        // cover may be, and starving it holds the cover at its finest level - measured at
        // Gavarnie z17.25 pitch 63 with a mesh-only cover: 128 z21 tiles in a patch at the
        // bottom of the screen and paper everywhere else (David's reports 53110e6a and
        // d2975824, 17 September). Applied only once the mesh cover exists, so the first
        // frame bootstraps from the plain frustum.
        const auto beneathCoarseMesh = [&](const OverscaledTileID& id) {
            UnwrappedTileID t = id.toUnwrapped();
            if (t.canonical.z < 2) {
                return false;
            }
            // Ancestors from two levels up to the root.
            for (uint8_t z = t.canonical.z - 2;; --z) {
                if (parameters.requiredTiles->contains(UnwrappedTileID{t.wrap, t.canonical.scaledTo(z)})) {
                    return true;
                }
                if (z == 0) {
                    return false;
                }
            }
        };
        std::erase_if(idealTiles, beneathCoarseMesh);
        std::erase_if(panTiles, beneathCoarseMesh);
        for (const auto& required : *parameters.requiredTiles) {
            const uint8_t requiredZoom = required.canonical.z;
            const uint8_t ancestorZoom = std::min(requiredZoom, zoomRange.max);
            if (ancestorZoom < zoomRange.min) {
                continue; // this source has nothing covering this tile at all
            }
            const OverscaledTileID id{requiredZoom, required.wrap, required.canonical.scaledTo(ancestorZoom)};
            if (std::find(idealTiles.begin(), idealTiles.end(), id) == idealTiles.end()) {
                idealTiles.push_back(id);
            }
        }
    }

    // Stores a list of all the tiles that we're definitely going to retain.
    // There are two kinds of tiles we need: the ideal tiles determined by the
    // tile cover. They may not yet be in use because they're still loading. In
    // addition to that, we also need to retain all tiles that we're actively
    // using, e.g. as a replacement for tile that aren't loaded yet.
    std::set<OverscaledTileID> retain;

    auto retainTileFn = [&](Tile& tile, TileNecessity necessity) -> void {
        if (retain.emplace(tile.id).second) {
            tile.setUpdateParameters({.minimumUpdateInterval = minimumUpdateInterval, .isVolatile = isVolatile});
            tile.setNecessity(necessity);
        }

        if (needsRelayout) {
            tile.setLayers(layers);
        }
    };
    auto getTileFn = [&](const OverscaledTileID& tileID) -> Tile* {
        auto it = tiles.find(tileID);
        return it == tiles.end() ? nullptr : it->second.get();
    };

    // The min and max zoom for TileRange are based on the updateRenderables
    // algorithm. Tiles are created at the ideal tile zoom or at lower zoom
    // levels. Child tiles are used from the cache, but not created.
    std::optional<util::TileRange> tileRange = std::nullopt;
    if (bounds) {
        // A variable-zoom cover asks for tiles finer than the nominal zoom near the camera, so
        // a bounded source has to size its range to its own max zoom. Capping at the nominal
        // zoom refuses those tiles, and updateRenderables drops the parents they would have
        // replaced without a fallback, which reads as the layer blinking to the background.
        const bool variableZoom = parameters.tileLodMode == TileLodMode::Distance ||
                                  parameters.tileLodMode == TileLodMode::Adaptive;
        int32_t maxZoom = variableZoom ? zoomRange.max : std::min(tileZoom, static_cast<int32_t>(zoomRange.max));
        tileRange = util::TileRange::fromLatLngBounds(*bounds, zoomRange.min, maxZoom);
    }
    auto createTileFn = [&](const OverscaledTileID& tileID) -> Tile* {
        if (tileRange && !tileRange->contains(tileID.canonical)) {
            return nullptr;
        }
        std::unique_ptr<Tile> tile = cache.pop(tileID);
        if (!tile) {
            tile = createTile(tileID, observer);
            if (!tile) return nullptr;
            tile->setLayers(layers);
        }

        return tiles.emplace(tileID, std::move(tile)).first->second.get();
    };

    auto previouslyRenderedTiles = std::move(renderedTiles);

    auto renderTileFn = [&](const UnwrappedTileID& tileID, Tile& tile) {
        addRenderTile(tileID, tile);
        previouslyRenderedTiles.erase(tileID); // Still rendering this tile, no need for special fading logic.
        tile.markRenderedIdeal();
    };

    renderedTiles.clear();

    if (!panTiles.empty()) {
        algorithm::updateRenderables(
            getTileFn,
            createTileFn,
            retainTileFn,
            [](const UnwrappedTileID&, Tile&) {},
            panTiles,
            emptyPrefetchedTiles,
            zoomRange,
            maxParentTileOverscaleFactor);
    }

    algorithm::updateRenderables(getTileFn,
                                 createTileFn,
                                 retainTileFn,
                                 renderTileFn,
                                 idealTiles,
                                 tiles,
                                 zoomRange,
                                 maxParentTileOverscaleFactor);

    for (auto previouslyRenderedTile : previouslyRenderedTiles) {
        Tile& tile = previouslyRenderedTile.second;
        tile.markRenderedPreviously();
        if (tile.holdForFade()) {
            // Since it was rendered in the last frame, we know we have it
            // Don't mark the tile "Required" to avoid triggering a new network request
            retainTileFn(tile, TileNecessity::Optional);
            addRenderTile(previouslyRenderedTile.first, tile);
        }
    }

    if (type != SourceType::Annotations && cacheEnabled) {
        auto conservativeCacheSize = static_cast<size_t>(
            std::max(static_cast<double>(parameters.transformState.getSize().width) / tileSize, 1.0) *
            std::max(static_cast<double>(parameters.transformState.getSize().height) / tileSize, 1.0) *
            (parameters.transformState.getMaxZoom() - parameters.transformState.getMinZoom() + 1) * 0.5);
        cache.setSize(conservativeCacheSize);
    } else {
        cache.setSize(0);
    }

    // Remove stale tiles. This goes through the (sorted!) tiles map and retain
    // set in lockstep and removes items from tiles that don't have the
    // corresponding key in the retain set.
    {
        auto tilesIt = tiles.begin();
        auto retainIt = retain.begin();
        while (tilesIt != tiles.end()) {
            if (retainIt == retain.end() || tilesIt->first < *retainIt) {
                // Remove the tile from the map.
                // If it requires re-layout, discard it asynchronously, otherwise keep it in the cache
                const auto key = tilesIt->first;
                if (std::unique_ptr<Tile> tile = std::move(tiles.extract(tilesIt++).mapped())) {
                    if (needsRelayout) {
                        cache.deferredRelease(std::move(tile));
                    } else {
                        tile->setNecessity(TileNecessity::Optional);
                        cache.add(key, std::move(tile));
                    }
                }
            } else {
                if (!(*retainIt < tilesIt->first)) {
                    ++tilesIt;
                }
                ++retainIt;
            }
        }
    }

    for (auto& pair : tiles) {
        pair.second->setShowCollisionBoxes(parameters.debugOptions & MapDebugOptions::Collision);
    }

    // Initialize renderable tiles and update the contained layer render data.
    for (auto& entry : renderedTiles) {
        Tile& tile = entry.second;
        assert(tile.isRenderable());
        tile.usedByRenderedLayers = false;

        const bool holdForFade = tile.holdForFade();
        for (const auto& layerProperties : layers) {
            const auto* typeInfo = layerProperties->baseImpl->getTypeInfo();
            if (holdForFade && typeInfo->fadingTiles == LayerTypeInfo::FadingTiles::NotRequired) {
                continue;
            }
            tile.usedByRenderedLayers |= tile.layerPropertiesUpdated(layerProperties);
        }
    }

    cache.deferPendingReleases();
}

void TilePyramid::handleWrapJump(float lng) {
    // On top of the regular z/x/y values, TileIDs have a `wrap` value that specify
    // which cppy of the world the tile belongs to. For example, at `lng: 10` you
    // might render z/x/y/0 while at `lng: 370` you would render z/x/y/1.
    //
    // When lng values get wrapped (going from `lng: 370` to `long: 10`) you expect
    // to see the same thing on the screen (370 degrees and 10 degrees is the same
    // place in the world) but all the TileIDs will have different wrap values.
    //
    // In order to make this transition seamless, we calculate the rounded difference of
    // "worlds" between the last frame and the current frame. If the map panned by
    // a world, then we can assign all the tiles new TileIDs with updated wrap values.
    // For example, assign z/x/y/1 a new id: z/x/y/0. It is the same tile, just rendered
    // in a different position.
    //
    // This enables us to reuse the tiles at more ideal locations and prevent flickering.

    const float lngDifference = lng - prevLng;
    const float worldDifference = lngDifference / 360.f;
    const auto wrapDelta = static_cast<int16_t>(std::round(worldDifference));
    prevLng = lng;

    if (wrapDelta) {
        std::map<OverscaledTileID, std::unique_ptr<Tile>> newTiles;
        std::map<UnwrappedTileID, std::reference_wrapper<Tile>> newRenderTiles;
        for (auto& tile : tiles) {
            auto newID = tile.second->id.unwrapTo(tile.second->id.wrap + wrapDelta);
            tile.second->id = newID;
            newTiles.emplace(newID, std::move(tile.second));
        }
        tiles = std::move(newTiles);

        for (auto& tile : renderedTiles) {
            UnwrappedTileID newID = tile.first.unwrapTo(tile.first.wrap + wrapDelta);
            newRenderTiles.emplace(newID, tile.second);
        }
        renderedTiles = std::move(newRenderTiles);
    }
}

std::unordered_map<std::string, std::vector<Feature>> TilePyramid::queryRenderedFeatures(
    const ScreenLineString& geometry,
    const TransformState& transformState,
    const std::unordered_map<std::string, const RenderLayer*>& layers,
    const RenderedQueryOptions& options,
    const mat4& projMatrix,
    const SourceFeatureState& featureState) const {
    std::unordered_map<std::string, std::vector<Feature>> result;
    if (renderedTiles.empty() || geometry.empty()) {
        return result;
    }

    LineString<double> queryGeometry;
    queryGeometry.reserve(geometry.size());

    for (const auto& p : geometry) {
        queryGeometry.push_back(
            TileCoordinate::fromScreenCoordinate(transformState, 0, {p.x, transformState.getSize().height - p.y}).p);
    }

    mapbox::geometry::box<double> box = mapbox::geometry::envelope(queryGeometry);

    auto cmp = [](const UnwrappedTileID& a, const UnwrappedTileID& b) {
        return std::tie(a.canonical.z, a.canonical.y, a.wrap, a.canonical.x) <
               std::tie(b.canonical.z, b.canonical.y, b.wrap, b.canonical.x);
    };

    std::map<UnwrappedTileID, std::reference_wrapper<Tile>, decltype(cmp)> sortedTiles{
        renderedTiles.begin(), renderedTiles.end(), cmp};

    auto maxPitchScaleFactor = transformState.maxPitchScaleFactor();

    for (const auto& entry : sortedTiles) {
        const UnwrappedTileID& id = entry.first;
        Tile& tile = entry.second;

        const auto scale = static_cast<float>(transformState.getScale() /
                                              (1 << id.canonical.z)); // equivalent to std::pow(2,
                                                                      // transformState.getZoom() - id.canonical.z);
        auto queryPadding = maxPitchScaleFactor * tile.getQueryPadding(layers) * util::EXTENT / util::tileSize_D /
                            scale;

        GeometryCoordinate tileSpaceBoundsMin = TileCoordinate::toGeometryCoordinate(id, box.min);
        if (tileSpaceBoundsMin.x - queryPadding >= util::EXTENT ||
            tileSpaceBoundsMin.y - queryPadding >= util::EXTENT) {
            continue;
        }

        GeometryCoordinate tileSpaceBoundsMax = TileCoordinate::toGeometryCoordinate(id, box.max);
        if (tileSpaceBoundsMax.x + queryPadding < 0 || tileSpaceBoundsMax.y + queryPadding < 0) {
            continue;
        }

        GeometryCoordinates tileSpaceQueryGeometry;
        tileSpaceQueryGeometry.reserve(queryGeometry.size());
        for (const auto& c : queryGeometry) {
            tileSpaceQueryGeometry.push_back(TileCoordinate::toGeometryCoordinate(id, c));
        }

        tile.queryRenderedFeatures(
            result, tileSpaceQueryGeometry, transformState, layers, options, projMatrix, featureState);
    }

    return result;
}

std::vector<Feature> TilePyramid::querySourceFeatures(const SourceQueryOptions& options) const {
    std::vector<Feature> result;

    for (const auto& pair : tiles) {
        pair.second->querySourceFeatures(result, options);
    }

    return result;
}

void TilePyramid::setCacheEnabled(bool enable) {
    cacheEnabled = enable;
}

void TilePyramid::reduceMemoryUse() {
    cache.clear();
}

void TilePyramid::setObserver(TileObserver* observer_) {
    observer = observer_;
}

void TilePyramid::dumpDebugLogs() const {
    for (const auto& pair : tiles) {
        pair.second->dumpDebugLogs();
    }
}

void TilePyramid::clearAll() {
    fadingTiles = false;
    tiles.clear();
    renderedTiles.clear();
    cache.clear();
}

void TilePyramid::addRenderTile(const UnwrappedTileID& tileID, Tile& tile) {
    assert(tile.isRenderable());
    renderedTiles.emplace(tileID, tile);
}

void TilePyramid::updateFadingTiles() {
    fadingTiles = false;
    for (auto& entry : renderedTiles) {
        Tile& tile = entry.second;
        if (tile.holdForFade()) {
            fadingTiles = true;
            tile.performedFadePlacement();
        }
    }
}

} // namespace mln
