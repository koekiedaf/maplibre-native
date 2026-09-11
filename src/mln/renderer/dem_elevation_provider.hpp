#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/util/range.hpp>
#include <mln/util/tile_cover.hpp>

#include <optional>
#include <vector>
#include <string>

namespace mln {

class RenderSource;
class Tile;

/// Answers "how high is the terrain here?" for util::tileCover, from the DEM source's
/// loaded tiles, so that sources are asked for the tiles the terrain mesh actually
/// needs (see util::TileElevationProvider).
///
/// A tile's own DEM is used when loaded, otherwise the nearest loaded ancestor's range,
/// which contains it. The answer only has to be conservative: too large an elevation
/// range loads a tile that turns out not to be visible, too small drops one that is.
///
/// Deliberately reads RenderSource::getLoadedTiles() (retained-for-rendering tiles plus
/// whatever this source's tile cache still holds fully loaded), not getRawRenderTiles()
/// (retained-for-rendering only). This provider is itself the elevation input to the DEM
/// source's own tile cover (RenderOrchestrator::createRenderTree hands the same instance
/// to every source's update, the DEM source included), so if a tile's answer depended on
/// whether it is *currently rendered*, the loop would be closed: a tile whose true range
/// fails the frustum test drops out of the render set, which would make its own next
/// query fall back to the coarser aggregate below, pass the test, and get added back -
/// alternating every frame, forever. Keying the lookup on "loaded" rather than
/// "currently rendered" makes a tile's answer monotone: once its DEM has loaded, the
/// same tight range comes back whether or not this frame's cover retains it, so the
/// frustum verdict for that tile stops flipping.
///
/// For a tile with *no* loaded DEM covering it, it reports the aggregate range of the
/// terrain currently loaded in view rather than "unknown". This is what lets the
/// one-ring cover dilation in RenderTerrain::computeMeshCover re-test a not-yet-loaded
/// frontier neighbour against the elevation-extended frustum: an unknown tile is assumed
/// about as tall (or, for bathymetry, as deep) as its loaded neighbours, so the tile that
/// belongs in view is kept and requested instead of assumed flat and dropped. nullopt
/// only when nothing at all is loaded (cover stays flat, exactly as before).
class DEMElevationProvider final : public util::TileElevationProvider {
public:
    /// `demSource` may be null (no terrain, or its source not yet resolved), in which
    /// case every query reports unknown and the cover stays flat.
    explicit DEMElevationProvider(const RenderSource* demSource, double exaggeration);

    std::optional<Range<double>> getTileElevationRange(const CanonicalTileID&) const override;

    /// Debug-only, for the DUCKMAPS_ELEVATION_TRACE diagnosis (see Renderer::Impl::render):
    /// every getTileElevationRange query made anywhere this frame is recorded, deduplicated
    /// by tile id (the answer is deterministic per id within one frame, so last write wins),
    /// and drained into the trace line once per frame. Returns "[]" and touches nothing when
    /// the trace variable is unset - no cost, no behaviour change.
    static std::string debugDrainElevationQueries();

private:
    const RenderSource* demSource;
    double exaggeration;
    /// The DEM tiles this source holds fully loaded, collected once at construction. The
    /// list is walked by every `getTileElevationRange` call, and `util::tileCover` makes
    /// one of those per candidate tile, so collecting it per query would allocate a
    /// vector in the cover's hot loop. The provider is rebuilt each frame anyway
    /// (RenderOrchestrator::createRenderTree), so a per-frame snapshot is exactly as
    /// fresh as a per-query one.
    std::vector<const Tile*> loadedTiles;
    /// Aggregate elevation range (exaggeration applied) of all loaded DEM tiles, computed
    /// once at construction; the fallback for tiles with no loaded DEM. nullopt when
    /// nothing is loaded.
    std::optional<Range<double>> loadedRange;
};

} // namespace mln
