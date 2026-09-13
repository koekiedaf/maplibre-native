#pragma once

#include <mln/tile/tile.hpp>
#include <mln/tile/tile_loader.hpp>
#include <mln/tile/raster_dem_tile_worker.hpp>
#include <mln/actor/actor.hpp>

namespace mln {

class Tileset;
class TileParameters;
class HillshadeBucket;

enum class DEMTileNeighbors : uint8_t {
    // 0b00000000
    Empty = 0 << 1,

    // 0b00000001
    Left = 1 << 0,
    // 0b00000010
    Right = 1 << 1,
    // 0b00000100
    TopLeft = 1 << 2,
    // 0b00001000
    TopCenter = 1 << 3,
    // 0b00010000
    TopRight = 1 << 4,
    // 0b00100000
    BottomLeft = 1 << 5,
    // 0b01000000
    BottomCenter = 1 << 6,
    // 0b10000000
    BottomRight = 1 << 7,

    // helper enums for tiles with no upper/lower neighbors
    // and completely backfilled tiles

    // 0b00011100
    NoUpper = 0b00011100,
    // 0b11100000
    NoLower = 0b11100000,
    // 0b11111111
    Complete = 0b11111111
};

inline DEMTileNeighbors operator|(DEMTileNeighbors a, DEMTileNeighbors b) {
    return static_cast<DEMTileNeighbors>(int(a) | int(b));
};

inline DEMTileNeighbors operator&(DEMTileNeighbors a, DEMTileNeighbors b) {
    return static_cast<DEMTileNeighbors>(int(a) & int(b));
}

inline bool operator!=(DEMTileNeighbors a, DEMTileNeighbors b) {
    return static_cast<unsigned char>(a) != static_cast<unsigned char>(b);
}

namespace style {
class Layer;
} // namespace style

class RasterDEMTile final : public Tile {
public:
    RasterDEMTile(
        const OverscaledTileID&, std::string, const TileParameters&, const Tileset&, TileObserver* observer = nullptr);
    ~RasterDEMTile() override;

    std::unique_ptr<TileRenderData> createRenderData() override;
    void setNecessity(TileNecessity) override;
    void setUpdateParameters(const TileUpdateParameters&) override;

    void setError(std::exception_ptr);
    // DuckMaps fork only: `unbuiltGround` carries Response::unbuiltGround (see that header's own
    // comment) - true when our terrain endpoint answered this exact tile with the flat sea-level
    // filler rather than real archive relief. Defaulted so RasterTile/VectorTile, which share this
    // call site in tile_loader_impl.hpp's templated TileLoader<T>::loadedData, need not know about
    // a parameter only this tile kind acts on.
    void setMetadata(std::optional<Timestamp> modified, std::optional<Timestamp> expires,
                     bool unbuiltGround = false);
    void setData(const std::shared_ptr<const std::string>& data);

    // DuckMaps fork only: read by RenderTerrain when it caches this tile's decoded DEM texture, so
    // the slope-shading layer can tell "no honest relief here" (unbuilt) apart from "real relief
    // that happens to measure flat" - see render_terrain.hpp's DEMTextureEntry/TerrainData.
    bool isUnbuiltGround() const { return unbuiltGround; }

    bool layerPropertiesUpdated(const Immutable<style::LayerProperties>& layerProperties) override;

    HillshadeBucket* getBucket() const;
    void backfillBorder(const RasterDEMTile& borderTile, DEMTileNeighbors mask);

    // neighboringTiles is a bitmask for which neighboring tiles have been backfilled
    // there are max 8 possible neighboring tiles, so each bit represents one neighbor
    DEMTileNeighbors neighboringTiles = DEMTileNeighbors::Empty;

    void setMask(TileMask&&) override;

    void onParsed(std::unique_ptr<HillshadeBucket> result, uint64_t correlationID);
    void onError(std::exception_ptr, uint64_t correlationID);

    void cancel() override;

private:
    void markObsolete();

    TileLoader<RasterDEMTile> loader;

    TaggedScheduler threadPool;
    std::shared_ptr<Mailbox> mailbox;
    Actor<RasterDEMTileWorker> worker;

    uint64_t correlationID = 0;
    Tileset::RasterEncoding encoding;

    // Contains the Bucket object for the tile. Buckets are render
    // objects and they get added by tile parsing operations.
    std::shared_ptr<HillshadeBucket> bucket;

    bool obsolete = false;

    // DuckMaps fork only - see isUnbuiltGround() above.
    bool unbuiltGround = false;
};

} // namespace mln
