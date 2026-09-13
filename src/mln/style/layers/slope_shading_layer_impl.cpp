#include <mln/style/layers/slope_shading_layer_impl.hpp>

namespace mln {
namespace style {

SlopeShadingLayer::Impl::Impl(const Impl& other)
    : Layer::Impl(other),
      paint(other.paint) {}

bool SlopeShadingLayer::Impl::hasLayoutDifference(const Layer::Impl&) const {
    // No bucket, no geometry - see terrain_contour_layer_impl.cpp's own comment. The render
    // layer rebuilds its drawables from RenderTerrain's own tile set every frame update, not
    // from a layout/bucket diff, so this can never trigger a rebuild by itself.
    return false;
}

// stringifyLayout is deliberately NOT defined here - see this class's own header comment for
// why: slope_shading_layer.cpp (generated) already defines it, and a second definition here
// would be an ODR violation.

} // namespace style
} // namespace mln
