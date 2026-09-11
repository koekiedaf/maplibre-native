#include <mln/style/layers/terrain_contour_layer_impl.hpp>

namespace mln {
namespace style {

TerrainContourLayer::Impl::Impl(const Impl& other)
    : Layer::Impl(other),
      paint(other.paint) {}

bool TerrainContourLayer::Impl::hasLayoutDifference(const Layer::Impl&) const {
    // No bucket, no geometry - see docs/plans/2026-09-11-engine-layer-plumbing.md B3/B4. The
    // render layer rebuilds its drawables from RenderTerrain's own tile set every frame update,
    // not from a layout/bucket diff, so this can never trigger a rebuild by itself.
    return false;
}

void TerrainContourLayer::Impl::stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const {
}

} // namespace style
} // namespace mln
