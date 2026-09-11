// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/terrain_line_layer.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>
#include <mln/style/layer_observer.hpp>
#include <mln/style/conversion/color_ramp_property_value.hpp>
#include <mln/style/conversion/constant.hpp>
#include <mln/style/conversion/property_value.hpp>
#include <mln/style/conversion/transition_options.hpp>
#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/util/traits.hpp>

#include <mapbox/eternal.hpp>

namespace mln {
namespace style {


// static
const LayerTypeInfo* TerrainLineLayer::Impl::staticTypeInfo() noexcept {
    const static LayerTypeInfo typeInfo{.type="terrain-line",
                                        .source=LayerTypeInfo::Source::Required,
                                        .pass3d=LayerTypeInfo::Pass3D::NotRequired,
                                        .layout=LayerTypeInfo::Layout::Required,
                                        .fadingTiles=LayerTypeInfo::FadingTiles::NotRequired,
                                        .crossTileIndex=LayerTypeInfo::CrossTileIndex::NotRequired,
                                        .tileKind=LayerTypeInfo::TileKind::Geometry};
    return &typeInfo;
}

TerrainLineLayer::TerrainLineLayer(const std::string& layerID, const std::string& sourceID)
    : Layer(makeMutable<Impl>(layerID, sourceID)) {
}

TerrainLineLayer::TerrainLineLayer(Immutable<Impl> impl_)
    : Layer(std::move(impl_)) {
}

TerrainLineLayer::~TerrainLineLayer() {
    weakFactory.invalidateWeakPtrs();
}

const TerrainLineLayer::Impl& TerrainLineLayer::impl() const {
    return static_cast<const Impl&>(*baseImpl);
}

Mutable<TerrainLineLayer::Impl> TerrainLineLayer::mutableImpl() const {
    return makeMutable<Impl>(impl());
}

std::unique_ptr<Layer> TerrainLineLayer::cloneRef(const std::string& id_) const {
    auto impl_ = mutableImpl();
    impl_->id = id_;
    impl_->paint = TerrainLinePaintProperties::Transitionable();
    return std::make_unique<TerrainLineLayer>(std::move(impl_));
}

void TerrainLineLayer::Impl::stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const {
}

// Layout properties


// Paint properties

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineBlur() {
    return {0.5f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineBlur() const {
    return impl().paint.template get<TerrainLineBlur>().value;
}

void TerrainLineLayer::setTerrainLineBlur(const PropertyValue<float>& value) {
    if (value == getTerrainLineBlur())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineBlur>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineBlurTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineBlur>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineBlurTransition() const {
    return impl().paint.template get<TerrainLineBlur>().options;
}

PropertyValue<Color> TerrainLineLayer::getDefaultTerrainLineColor() {
    return {Color::black()};
}

const PropertyValue<Color>& TerrainLineLayer::getTerrainLineColor() const {
    return impl().paint.template get<TerrainLineColor>().value;
}

void TerrainLineLayer::setTerrainLineColor(const PropertyValue<Color>& value) {
    if (value == getTerrainLineColor())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineColor>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineColorTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineColor>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineColorTransition() const {
    return impl().paint.template get<TerrainLineColor>().options;
}

PropertyValue<std::vector<float>> TerrainLineLayer::getDefaultTerrainLineDasharray() {
    return {{0.f, 0.f}};
}

const PropertyValue<std::vector<float>>& TerrainLineLayer::getTerrainLineDasharray() const {
    return impl().paint.template get<TerrainLineDasharray>().value;
}

void TerrainLineLayer::setTerrainLineDasharray(const PropertyValue<std::vector<float>>& value) {
    if (value == getTerrainLineDasharray())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineDasharray>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineDasharrayTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineDasharray>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineDasharrayTransition() const {
    return impl().paint.template get<TerrainLineDasharray>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineFade() {
    return {0.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineFade() const {
    return impl().paint.template get<TerrainLineFade>().value;
}

void TerrainLineLayer::setTerrainLineFade(const PropertyValue<float>& value) {
    if (value == getTerrainLineFade())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineFade>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineFadeTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineFade>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineFadeTransition() const {
    return impl().paint.template get<TerrainLineFade>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineFadeDistance() {
    return {8000.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineFadeDistance() const {
    return impl().paint.template get<TerrainLineFadeDistance>().value;
}

void TerrainLineLayer::setTerrainLineFadeDistance(const PropertyValue<float>& value) {
    if (value == getTerrainLineFadeDistance())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineFadeDistance>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineFadeDistanceTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineFadeDistance>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineFadeDistanceTransition() const {
    return impl().paint.template get<TerrainLineFadeDistance>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineGhostOpacity() {
    return {0.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineGhostOpacity() const {
    return impl().paint.template get<TerrainLineGhostOpacity>().value;
}

void TerrainLineLayer::setTerrainLineGhostOpacity(const PropertyValue<float>& value) {
    if (value == getTerrainLineGhostOpacity())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineGhostOpacity>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineGhostOpacityTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineGhostOpacity>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineGhostOpacityTransition() const {
    return impl().paint.template get<TerrainLineGhostOpacity>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineOffset() {
    return {0.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineOffset() const {
    return impl().paint.template get<TerrainLineOffset>().value;
}

void TerrainLineLayer::setTerrainLineOffset(const PropertyValue<float>& value) {
    if (value == getTerrainLineOffset())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineOffset>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineOffsetTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineOffset>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineOffsetTransition() const {
    return impl().paint.template get<TerrainLineOffset>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineOpacity() {
    return {1.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineOpacity() const {
    return impl().paint.template get<TerrainLineOpacity>().value;
}

void TerrainLineLayer::setTerrainLineOpacity(const PropertyValue<float>& value) {
    if (value == getTerrainLineOpacity())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineOpacity>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineOpacityTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineOpacity>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineOpacityTransition() const {
    return impl().paint.template get<TerrainLineOpacity>().options;
}

PropertyValue<float> TerrainLineLayer::getDefaultTerrainLineWidth() {
    return {1.f};
}

const PropertyValue<float>& TerrainLineLayer::getTerrainLineWidth() const {
    return impl().paint.template get<TerrainLineWidth>().value;
}

void TerrainLineLayer::setTerrainLineWidth(const PropertyValue<float>& value) {
    if (value == getTerrainLineWidth())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineWidth>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainLineLayer::setTerrainLineWidthTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainLineWidth>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainLineLayer::getTerrainLineWidthTransition() const {
    return impl().paint.template get<TerrainLineWidth>().options;
}

using namespace conversion;

namespace {

constexpr uint8_t kPaintPropertyCount = 18u;

enum class Property : uint8_t {
    TerrainLineBlur,
    TerrainLineColor,
    TerrainLineDasharray,
    TerrainLineFade,
    TerrainLineFadeDistance,
    TerrainLineGhostOpacity,
    TerrainLineOffset,
    TerrainLineOpacity,
    TerrainLineWidth,
    TerrainLineBlurTransition,
    TerrainLineColorTransition,
    TerrainLineDasharrayTransition,
    TerrainLineFadeTransition,
    TerrainLineFadeDistanceTransition,
    TerrainLineGhostOpacityTransition,
    TerrainLineOffsetTransition,
    TerrainLineOpacityTransition,
    TerrainLineWidthTransition,
};

template <typename T>
constexpr uint8_t toUint8(T t) noexcept {
    return uint8_t(mln::underlying_type(t));
}

constexpr const auto layerProperties = mapbox::eternal::hash_map<mapbox::eternal::string, uint8_t>(
    {{"terrain-line-blur", toUint8(Property::TerrainLineBlur)},
     {"terrain-line-color", toUint8(Property::TerrainLineColor)},
     {"terrain-line-dasharray", toUint8(Property::TerrainLineDasharray)},
     {"terrain-line-fade", toUint8(Property::TerrainLineFade)},
     {"terrain-line-fade-distance", toUint8(Property::TerrainLineFadeDistance)},
     {"terrain-line-ghost-opacity", toUint8(Property::TerrainLineGhostOpacity)},
     {"terrain-line-offset", toUint8(Property::TerrainLineOffset)},
     {"terrain-line-opacity", toUint8(Property::TerrainLineOpacity)},
     {"terrain-line-width", toUint8(Property::TerrainLineWidth)},
     {"terrain-line-blur-transition", toUint8(Property::TerrainLineBlurTransition)},
     {"terrain-line-color-transition", toUint8(Property::TerrainLineColorTransition)},
     {"terrain-line-dasharray-transition", toUint8(Property::TerrainLineDasharrayTransition)},
     {"terrain-line-fade-transition", toUint8(Property::TerrainLineFadeTransition)},
     {"terrain-line-fade-distance-transition", toUint8(Property::TerrainLineFadeDistanceTransition)},
     {"terrain-line-ghost-opacity-transition", toUint8(Property::TerrainLineGhostOpacityTransition)},
     {"terrain-line-offset-transition", toUint8(Property::TerrainLineOffsetTransition)},
     {"terrain-line-opacity-transition", toUint8(Property::TerrainLineOpacityTransition)},
     {"terrain-line-width-transition", toUint8(Property::TerrainLineWidthTransition)}});

StyleProperty getLayerProperty(const TerrainLineLayer& layer, Property property) {
    switch (property) {
        case Property::TerrainLineBlur:
            return makeStyleProperty(layer.getTerrainLineBlur());
        case Property::TerrainLineColor:
            return makeStyleProperty(layer.getTerrainLineColor());
        case Property::TerrainLineDasharray:
            return makeStyleProperty(layer.getTerrainLineDasharray());
        case Property::TerrainLineFade:
            return makeStyleProperty(layer.getTerrainLineFade());
        case Property::TerrainLineFadeDistance:
            return makeStyleProperty(layer.getTerrainLineFadeDistance());
        case Property::TerrainLineGhostOpacity:
            return makeStyleProperty(layer.getTerrainLineGhostOpacity());
        case Property::TerrainLineOffset:
            return makeStyleProperty(layer.getTerrainLineOffset());
        case Property::TerrainLineOpacity:
            return makeStyleProperty(layer.getTerrainLineOpacity());
        case Property::TerrainLineWidth:
            return makeStyleProperty(layer.getTerrainLineWidth());
        case Property::TerrainLineBlurTransition:
            return makeStyleProperty(layer.getTerrainLineBlurTransition());
        case Property::TerrainLineColorTransition:
            return makeStyleProperty(layer.getTerrainLineColorTransition());
        case Property::TerrainLineDasharrayTransition:
            return makeStyleProperty(layer.getTerrainLineDasharrayTransition());
        case Property::TerrainLineFadeTransition:
            return makeStyleProperty(layer.getTerrainLineFadeTransition());
        case Property::TerrainLineFadeDistanceTransition:
            return makeStyleProperty(layer.getTerrainLineFadeDistanceTransition());
        case Property::TerrainLineGhostOpacityTransition:
            return makeStyleProperty(layer.getTerrainLineGhostOpacityTransition());
        case Property::TerrainLineOffsetTransition:
            return makeStyleProperty(layer.getTerrainLineOffsetTransition());
        case Property::TerrainLineOpacityTransition:
            return makeStyleProperty(layer.getTerrainLineOpacityTransition());
        case Property::TerrainLineWidthTransition:
            return makeStyleProperty(layer.getTerrainLineWidthTransition());
    }
    return {};
}

StyleProperty getLayerProperty(const TerrainLineLayer& layer, const std::string& name) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) {
        return {};
    }
    return getLayerProperty(layer, static_cast<Property>(it->second));
}

} // namespace

Value TerrainLineLayer::serialize() const {
    auto result = Layer::serialize();
    assert(result.getObject());
    for (const auto& property : layerProperties) {
        auto styleProperty = getLayerProperty(*this, static_cast<Property>(property.second));
        if (styleProperty.getKind() == StyleProperty::Kind::Undefined) continue;
        serializeProperty(result, styleProperty, property.first.c_str(), property.second < kPaintPropertyCount);
    }
    return result;
}

std::optional<Error> TerrainLineLayer::setPropertyInternal(const std::string& name, const Convertible& value) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};

    auto property = static_cast<Property>(it->second);

    if (property == Property::TerrainLineBlur || property == Property::TerrainLineFade ||
        property == Property::TerrainLineFadeDistance || property == Property::TerrainLineGhostOpacity ||
        property == Property::TerrainLineOffset || property == Property::TerrainLineOpacity ||
        property == Property::TerrainLineWidth) {
        Error error;
        const auto& typedValue = convert<PropertyValue<float>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        if (property == Property::TerrainLineBlur) {
            setTerrainLineBlur(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineFade) {
            setTerrainLineFade(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineFadeDistance) {
            setTerrainLineFadeDistance(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineGhostOpacity) {
            setTerrainLineGhostOpacity(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineOffset) {
            setTerrainLineOffset(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineOpacity) {
            setTerrainLineOpacity(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainLineWidth) {
            setTerrainLineWidth(*typedValue);
            return std::nullopt;
        }
    }
    if (property == Property::TerrainLineColor) {
        Error error;
        const auto& typedValue = convert<PropertyValue<Color>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        setTerrainLineColor(*typedValue);
        return std::nullopt;
    }
    if (property == Property::TerrainLineDasharray) {
        Error error;
        const auto& typedValue = convert<PropertyValue<std::vector<float>>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        setTerrainLineDasharray(*typedValue);
        return std::nullopt;
    }

    Error error;
    std::optional<TransitionOptions> transition = convert<TransitionOptions>(value, error);
    if (!transition) {
        return error;
    }

    if (property == Property::TerrainLineBlurTransition) {
        setTerrainLineBlurTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineColorTransition) {
        setTerrainLineColorTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineDasharrayTransition) {
        setTerrainLineDasharrayTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineFadeTransition) {
        setTerrainLineFadeTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineFadeDistanceTransition) {
        setTerrainLineFadeDistanceTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineGhostOpacityTransition) {
        setTerrainLineGhostOpacityTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineOffsetTransition) {
        setTerrainLineOffsetTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineOpacityTransition) {
        setTerrainLineOpacityTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainLineWidthTransition) {
        setTerrainLineWidthTransition(*transition);
        return std::nullopt;
    }

    return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};
}

StyleProperty TerrainLineLayer::getProperty(const std::string& name) const {
    return getLayerProperty(*this, name);
}

Mutable<Layer::Impl> TerrainLineLayer::mutableBaseImpl() const {
    return staticMutableCast<Layer::Impl>(mutableImpl());
}

} // namespace style
} // namespace mln

// clang-format on
