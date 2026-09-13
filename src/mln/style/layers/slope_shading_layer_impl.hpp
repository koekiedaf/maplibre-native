#pragma once

#include <mln/style/layer_impl.hpp>
#include <mln/style/layers/slope_shading_layer.hpp>
#include <mln/style/layers/slope_shading_layer_properties.hpp>

namespace mln {
namespace style {

// DuckMaps fork only, task 2.6. Slope/aspect area fill computed per-fragment from the terrain
// DEM, drawn over RenderTerrain's own mesh - see container/server/app/map/assets/contours3d.js
// (the web reference this ports) and terrain_contour_layer_impl.hpp, whose shape this mirrors
// exactly: no source, no bucket, modelled on BackgroundLayer::Impl's shape.
class SlopeShadingLayer::Impl : public Layer::Impl {
public:
    using Layer::Impl::Impl;
    Impl(const Impl&);

    bool hasLayoutDifference(const Layer::Impl&) const override;
    void stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const override;

public:
    SlopeShadingPaintProperties::Transitionable paint;

    DECLARE_LAYER_TYPE_INFO;
};

} // namespace style
} // namespace mln
