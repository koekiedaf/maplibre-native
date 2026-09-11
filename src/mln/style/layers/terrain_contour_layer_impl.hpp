#pragma once

#include <mln/style/layer_impl.hpp>
#include <mln/style/layers/terrain_contour_layer.hpp>
#include <mln/style/layers/terrain_contour_layer_properties.hpp>

namespace mln {
namespace style {

// DuckMaps fork only, task 2.4a. See docs/plans/2026-09-11-engine-layer-plumbing.md and
// container/server/app/map/assets/contours3d.js (the web reference this ports). No source, no
// bucket: modelled on BackgroundLayer::Impl's shape (src/mln/style/layers/background_layer_impl.hpp).
class TerrainContourLayer::Impl : public Layer::Impl {
public:
    using Layer::Impl::Impl;
    Impl(const Impl&);

    bool hasLayoutDifference(const Layer::Impl&) const override;
    void stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const override;

public:
    TerrainContourPaintProperties::Transitionable paint;

    DECLARE_LAYER_TYPE_INFO;
};

} // namespace style
} // namespace mln
