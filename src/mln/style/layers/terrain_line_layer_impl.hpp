#pragma once

#include <mln/style/layer_impl.hpp>
#include <mln/style/layers/terrain_line_layer.hpp>
#include <mln/style/layers/terrain_line_layer_properties.hpp>

namespace mln {
namespace style {

// DuckMaps fork only. terrain-line has no bespoke layout properties of its own (only the
// universal `visibility`, which the base Layer::Impl already carries and which the generator
// therefore excludes from TerrainLine*Properties - see scripts/generate-style-code.mjs's
// layoutProperties filter). So, unlike LineLayer::Impl, there is no
// `TerrainLineLayoutProperties::Unevaluated layout;` member here: no such generated type exists
// because the generated layout-property list for this type is empty. This mirrors
// ColorReliefLayer::Impl's shape (also no layout member) even though, unlike color-relief,
// terrain-line does require Layout (LayerTypeInfo::Layout::Required) to reach the vector-tile
// Bucket/Layout machinery for its per-feature geometry (see TerrainLineBucket).
class TerrainLineLayer::Impl : public Layer::Impl {
public:
    using Layer::Impl::Impl;

    bool hasLayoutDifference(const Layer::Impl&) const override;
    void stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const override;

    expression::Dependency getDependencies() const noexcept override { return paint.getDependencies(); }

    TerrainLinePaintProperties::Transitionable paint;

    DECLARE_LAYER_TYPE_INFO;
};

} // namespace style
} // namespace mln
