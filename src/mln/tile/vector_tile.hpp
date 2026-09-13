#pragma once

#include <mln/tile/geometry_tile.hpp>
#include <mln/tile/tile_loader.hpp>

namespace mln {

class Tileset;
class TileParameters;

class VectorTile : public GeometryTile {
public:
    VectorTile(const OverscaledTileID&,
               std::string sourceID,
               const TileParameters&,
               const Tileset&,
               TileObserver* observer = nullptr);
    ~VectorTile() override;

    void setNecessity(TileNecessity) final;
    void setUpdateParameters(const TileUpdateParameters&) final;
    // The third parameter exists only so this shares tile_loader_impl.hpp's templated
    // TileLoader<T>::loadedData call site with RasterDEMTile (DuckMaps fork only - see that
    // class's own setMetadata comment); a vector tile has no use for it.
    void setMetadata(std::optional<Timestamp> modified, std::optional<Timestamp> expires,
                     bool unbuiltGround = false);

    virtual void setData(const std::shared_ptr<const std::string>&) = 0;

protected:
    // this needs to be explicitly deleted in the most-derived destructor
    // see `~VectorMVTTile`
    std::unique_ptr<TileLoader<VectorTile>> loader;
};

} // namespace mln
