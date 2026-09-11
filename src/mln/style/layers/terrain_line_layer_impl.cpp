#include <mln/style/layers/terrain_line_layer_impl.hpp>

namespace mln {
namespace style {

bool TerrainLineLayer::Impl::hasLayoutDifference(const Layer::Impl& other) const {
    assert(other.getTypeInfo() == getTypeInfo());
    const auto& impl = static_cast<const style::TerrainLineLayer::Impl&>(other);
    // No data-driven paint properties exist on this type (all nine are PropertyValue<T>, never
    // DataDrivenPropertyValue<T> - see docs/plans/2026-09-11-engine-layer-plumbing.md), and there
    // are no bespoke layout properties beyond the universal filter/visibility every layer carries,
    // so those two are the whole story for whether the bucket needs rebuilding.
    return filter != impl.filter || visibility != impl.visibility;
}

} // namespace style
} // namespace mln
