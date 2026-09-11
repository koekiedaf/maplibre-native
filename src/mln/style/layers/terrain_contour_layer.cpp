// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/terrain_contour_layer.hpp>
#include <mln/style/layers/terrain_contour_layer_impl.hpp>
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
const LayerTypeInfo* TerrainContourLayer::Impl::staticTypeInfo() noexcept {
    const static LayerTypeInfo typeInfo{.type="terrain-contour",
                                        .source=LayerTypeInfo::Source::NotRequired,
                                        .pass3d=LayerTypeInfo::Pass3D::NotRequired,
                                        .layout=LayerTypeInfo::Layout::NotRequired,
                                        .fadingTiles=LayerTypeInfo::FadingTiles::NotRequired,
                                        .crossTileIndex=LayerTypeInfo::CrossTileIndex::NotRequired,
                                        .tileKind=LayerTypeInfo::TileKind::NotRequired};
    return &typeInfo;
}

TerrainContourLayer::TerrainContourLayer(const std::string& layerID)
    : Layer(makeMutable<Impl>(layerID, std::string())) {
}

TerrainContourLayer::TerrainContourLayer(Immutable<Impl> impl_)
    : Layer(std::move(impl_)) {
}

TerrainContourLayer::~TerrainContourLayer() {
    weakFactory.invalidateWeakPtrs();
}

const TerrainContourLayer::Impl& TerrainContourLayer::impl() const {
    return static_cast<const Impl&>(*baseImpl);
}

Mutable<TerrainContourLayer::Impl> TerrainContourLayer::mutableImpl() const {
    return makeMutable<Impl>(impl());
}

std::unique_ptr<Layer> TerrainContourLayer::cloneRef(const std::string& id_) const {
    auto impl_ = mutableImpl();
    impl_->id = id_;
    impl_->paint = TerrainContourPaintProperties::Transitionable();
    return std::make_unique<TerrainContourLayer>(std::move(impl_));
}

void TerrainContourLayer::Impl::stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const {
}

// Layout properties


// Paint properties

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourFadeHi() {
    return {2.2f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourFadeHi() const {
    return impl().paint.template get<TerrainContourFadeHi>().value;
}

void TerrainContourLayer::setTerrainContourFadeHi(const PropertyValue<float>& value) {
    if (value == getTerrainContourFadeHi())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourFadeHi>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourFadeHiTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourFadeHi>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourFadeHiTransition() const {
    return impl().paint.template get<TerrainContourFadeHi>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourFadeLo() {
    return {0.6f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourFadeLo() const {
    return impl().paint.template get<TerrainContourFadeLo>().value;
}

void TerrainContourLayer::setTerrainContourFadeLo(const PropertyValue<float>& value) {
    if (value == getTerrainContourFadeLo())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourFadeLo>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourFadeLoTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourFadeLo>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourFadeLoTransition() const {
    return impl().paint.template get<TerrainContourFadeLo>().options;
}

PropertyValue<Color> TerrainContourLayer::getDefaultTerrainContourIndexColor() {
    return {{ 0.792156862745098, 0.6549019607843137, 0.5058823529411764, 1 }};
}

const PropertyValue<Color>& TerrainContourLayer::getTerrainContourIndexColor() const {
    return impl().paint.template get<TerrainContourIndexColor>().value;
}

void TerrainContourLayer::setTerrainContourIndexColor(const PropertyValue<Color>& value) {
    if (value == getTerrainContourIndexColor())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexColor>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourIndexColorTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexColor>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourIndexColorTransition() const {
    return impl().paint.template get<TerrainContourIndexColor>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourIndexInterval() {
    return {100.f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourIndexInterval() const {
    return impl().paint.template get<TerrainContourIndexInterval>().value;
}

void TerrainContourLayer::setTerrainContourIndexInterval(const PropertyValue<float>& value) {
    if (value == getTerrainContourIndexInterval())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexInterval>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourIndexIntervalTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexInterval>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourIndexIntervalTransition() const {
    return impl().paint.template get<TerrainContourIndexInterval>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourIndexOpacity() {
    return {1.f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourIndexOpacity() const {
    return impl().paint.template get<TerrainContourIndexOpacity>().value;
}

void TerrainContourLayer::setTerrainContourIndexOpacity(const PropertyValue<float>& value) {
    if (value == getTerrainContourIndexOpacity())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexOpacity>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourIndexOpacityTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexOpacity>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourIndexOpacityTransition() const {
    return impl().paint.template get<TerrainContourIndexOpacity>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourIndexWidth() {
    return {2.1f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourIndexWidth() const {
    return impl().paint.template get<TerrainContourIndexWidth>().value;
}

void TerrainContourLayer::setTerrainContourIndexWidth(const PropertyValue<float>& value) {
    if (value == getTerrainContourIndexWidth())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexWidth>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourIndexWidthTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourIndexWidth>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourIndexWidthTransition() const {
    return impl().paint.template get<TerrainContourIndexWidth>().options;
}

PropertyValue<Color> TerrainContourLayer::getDefaultTerrainContourMinorColor() {
    return {{ 0.7803921568627451, 0.6352941176470588, 0.4196078431372549, 1 }};
}

const PropertyValue<Color>& TerrainContourLayer::getTerrainContourMinorColor() const {
    return impl().paint.template get<TerrainContourMinorColor>().value;
}

void TerrainContourLayer::setTerrainContourMinorColor(const PropertyValue<Color>& value) {
    if (value == getTerrainContourMinorColor())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorColor>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourMinorColorTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorColor>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourMinorColorTransition() const {
    return impl().paint.template get<TerrainContourMinorColor>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourMinorInterval() {
    return {20.f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourMinorInterval() const {
    return impl().paint.template get<TerrainContourMinorInterval>().value;
}

void TerrainContourLayer::setTerrainContourMinorInterval(const PropertyValue<float>& value) {
    if (value == getTerrainContourMinorInterval())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorInterval>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourMinorIntervalTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorInterval>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourMinorIntervalTransition() const {
    return impl().paint.template get<TerrainContourMinorInterval>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourMinorOpacity() {
    return {1.f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourMinorOpacity() const {
    return impl().paint.template get<TerrainContourMinorOpacity>().value;
}

void TerrainContourLayer::setTerrainContourMinorOpacity(const PropertyValue<float>& value) {
    if (value == getTerrainContourMinorOpacity())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorOpacity>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourMinorOpacityTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorOpacity>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourMinorOpacityTransition() const {
    return impl().paint.template get<TerrainContourMinorOpacity>().options;
}

PropertyValue<float> TerrainContourLayer::getDefaultTerrainContourMinorWidth() {
    return {1.3f};
}

const PropertyValue<float>& TerrainContourLayer::getTerrainContourMinorWidth() const {
    return impl().paint.template get<TerrainContourMinorWidth>().value;
}

void TerrainContourLayer::setTerrainContourMinorWidth(const PropertyValue<float>& value) {
    if (value == getTerrainContourMinorWidth())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorWidth>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void TerrainContourLayer::setTerrainContourMinorWidthTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<TerrainContourMinorWidth>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions TerrainContourLayer::getTerrainContourMinorWidthTransition() const {
    return impl().paint.template get<TerrainContourMinorWidth>().options;
}

using namespace conversion;

namespace {

constexpr uint8_t kPaintPropertyCount = 20u;

enum class Property : uint8_t {
    TerrainContourFadeHi,
    TerrainContourFadeLo,
    TerrainContourIndexColor,
    TerrainContourIndexInterval,
    TerrainContourIndexOpacity,
    TerrainContourIndexWidth,
    TerrainContourMinorColor,
    TerrainContourMinorInterval,
    TerrainContourMinorOpacity,
    TerrainContourMinorWidth,
    TerrainContourFadeHiTransition,
    TerrainContourFadeLoTransition,
    TerrainContourIndexColorTransition,
    TerrainContourIndexIntervalTransition,
    TerrainContourIndexOpacityTransition,
    TerrainContourIndexWidthTransition,
    TerrainContourMinorColorTransition,
    TerrainContourMinorIntervalTransition,
    TerrainContourMinorOpacityTransition,
    TerrainContourMinorWidthTransition,
};

template <typename T>
constexpr uint8_t toUint8(T t) noexcept {
    return uint8_t(mln::underlying_type(t));
}

constexpr const auto layerProperties = mapbox::eternal::hash_map<mapbox::eternal::string, uint8_t>(
    {{"terrain-contour-fade-hi", toUint8(Property::TerrainContourFadeHi)},
     {"terrain-contour-fade-lo", toUint8(Property::TerrainContourFadeLo)},
     {"terrain-contour-index-color", toUint8(Property::TerrainContourIndexColor)},
     {"terrain-contour-index-interval", toUint8(Property::TerrainContourIndexInterval)},
     {"terrain-contour-index-opacity", toUint8(Property::TerrainContourIndexOpacity)},
     {"terrain-contour-index-width", toUint8(Property::TerrainContourIndexWidth)},
     {"terrain-contour-minor-color", toUint8(Property::TerrainContourMinorColor)},
     {"terrain-contour-minor-interval", toUint8(Property::TerrainContourMinorInterval)},
     {"terrain-contour-minor-opacity", toUint8(Property::TerrainContourMinorOpacity)},
     {"terrain-contour-minor-width", toUint8(Property::TerrainContourMinorWidth)},
     {"terrain-contour-fade-hi-transition", toUint8(Property::TerrainContourFadeHiTransition)},
     {"terrain-contour-fade-lo-transition", toUint8(Property::TerrainContourFadeLoTransition)},
     {"terrain-contour-index-color-transition", toUint8(Property::TerrainContourIndexColorTransition)},
     {"terrain-contour-index-interval-transition", toUint8(Property::TerrainContourIndexIntervalTransition)},
     {"terrain-contour-index-opacity-transition", toUint8(Property::TerrainContourIndexOpacityTransition)},
     {"terrain-contour-index-width-transition", toUint8(Property::TerrainContourIndexWidthTransition)},
     {"terrain-contour-minor-color-transition", toUint8(Property::TerrainContourMinorColorTransition)},
     {"terrain-contour-minor-interval-transition", toUint8(Property::TerrainContourMinorIntervalTransition)},
     {"terrain-contour-minor-opacity-transition", toUint8(Property::TerrainContourMinorOpacityTransition)},
     {"terrain-contour-minor-width-transition", toUint8(Property::TerrainContourMinorWidthTransition)}});

StyleProperty getLayerProperty(const TerrainContourLayer& layer, Property property) {
    switch (property) {
        case Property::TerrainContourFadeHi:
            return makeStyleProperty(layer.getTerrainContourFadeHi());
        case Property::TerrainContourFadeLo:
            return makeStyleProperty(layer.getTerrainContourFadeLo());
        case Property::TerrainContourIndexColor:
            return makeStyleProperty(layer.getTerrainContourIndexColor());
        case Property::TerrainContourIndexInterval:
            return makeStyleProperty(layer.getTerrainContourIndexInterval());
        case Property::TerrainContourIndexOpacity:
            return makeStyleProperty(layer.getTerrainContourIndexOpacity());
        case Property::TerrainContourIndexWidth:
            return makeStyleProperty(layer.getTerrainContourIndexWidth());
        case Property::TerrainContourMinorColor:
            return makeStyleProperty(layer.getTerrainContourMinorColor());
        case Property::TerrainContourMinorInterval:
            return makeStyleProperty(layer.getTerrainContourMinorInterval());
        case Property::TerrainContourMinorOpacity:
            return makeStyleProperty(layer.getTerrainContourMinorOpacity());
        case Property::TerrainContourMinorWidth:
            return makeStyleProperty(layer.getTerrainContourMinorWidth());
        case Property::TerrainContourFadeHiTransition:
            return makeStyleProperty(layer.getTerrainContourFadeHiTransition());
        case Property::TerrainContourFadeLoTransition:
            return makeStyleProperty(layer.getTerrainContourFadeLoTransition());
        case Property::TerrainContourIndexColorTransition:
            return makeStyleProperty(layer.getTerrainContourIndexColorTransition());
        case Property::TerrainContourIndexIntervalTransition:
            return makeStyleProperty(layer.getTerrainContourIndexIntervalTransition());
        case Property::TerrainContourIndexOpacityTransition:
            return makeStyleProperty(layer.getTerrainContourIndexOpacityTransition());
        case Property::TerrainContourIndexWidthTransition:
            return makeStyleProperty(layer.getTerrainContourIndexWidthTransition());
        case Property::TerrainContourMinorColorTransition:
            return makeStyleProperty(layer.getTerrainContourMinorColorTransition());
        case Property::TerrainContourMinorIntervalTransition:
            return makeStyleProperty(layer.getTerrainContourMinorIntervalTransition());
        case Property::TerrainContourMinorOpacityTransition:
            return makeStyleProperty(layer.getTerrainContourMinorOpacityTransition());
        case Property::TerrainContourMinorWidthTransition:
            return makeStyleProperty(layer.getTerrainContourMinorWidthTransition());
    }
    return {};
}

StyleProperty getLayerProperty(const TerrainContourLayer& layer, const std::string& name) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) {
        return {};
    }
    return getLayerProperty(layer, static_cast<Property>(it->second));
}

} // namespace

Value TerrainContourLayer::serialize() const {
    auto result = Layer::serialize();
    assert(result.getObject());
    for (const auto& property : layerProperties) {
        auto styleProperty = getLayerProperty(*this, static_cast<Property>(property.second));
        if (styleProperty.getKind() == StyleProperty::Kind::Undefined) continue;
        serializeProperty(result, styleProperty, property.first.c_str(), property.second < kPaintPropertyCount);
    }
    return result;
}

std::optional<Error> TerrainContourLayer::setPropertyInternal(const std::string& name, const Convertible& value) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};

    auto property = static_cast<Property>(it->second);

    if (property == Property::TerrainContourFadeHi || property == Property::TerrainContourFadeLo ||
        property == Property::TerrainContourIndexInterval || property == Property::TerrainContourIndexOpacity ||
        property == Property::TerrainContourIndexWidth || property == Property::TerrainContourMinorInterval ||
        property == Property::TerrainContourMinorOpacity || property == Property::TerrainContourMinorWidth) {
        Error error;
        const auto& typedValue = convert<PropertyValue<float>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        if (property == Property::TerrainContourFadeHi) {
            setTerrainContourFadeHi(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourFadeLo) {
            setTerrainContourFadeLo(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourIndexInterval) {
            setTerrainContourIndexInterval(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourIndexOpacity) {
            setTerrainContourIndexOpacity(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourIndexWidth) {
            setTerrainContourIndexWidth(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourMinorInterval) {
            setTerrainContourMinorInterval(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourMinorOpacity) {
            setTerrainContourMinorOpacity(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourMinorWidth) {
            setTerrainContourMinorWidth(*typedValue);
            return std::nullopt;
        }
    }
    if (property == Property::TerrainContourIndexColor || property == Property::TerrainContourMinorColor) {
        Error error;
        const auto& typedValue = convert<PropertyValue<Color>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        if (property == Property::TerrainContourIndexColor) {
            setTerrainContourIndexColor(*typedValue);
            return std::nullopt;
        }

        if (property == Property::TerrainContourMinorColor) {
            setTerrainContourMinorColor(*typedValue);
            return std::nullopt;
        }
    }

    Error error;
    std::optional<TransitionOptions> transition = convert<TransitionOptions>(value, error);
    if (!transition) {
        return error;
    }

    if (property == Property::TerrainContourFadeHiTransition) {
        setTerrainContourFadeHiTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourFadeLoTransition) {
        setTerrainContourFadeLoTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourIndexColorTransition) {
        setTerrainContourIndexColorTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourIndexIntervalTransition) {
        setTerrainContourIndexIntervalTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourIndexOpacityTransition) {
        setTerrainContourIndexOpacityTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourIndexWidthTransition) {
        setTerrainContourIndexWidthTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourMinorColorTransition) {
        setTerrainContourMinorColorTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourMinorIntervalTransition) {
        setTerrainContourMinorIntervalTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourMinorOpacityTransition) {
        setTerrainContourMinorOpacityTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::TerrainContourMinorWidthTransition) {
        setTerrainContourMinorWidthTransition(*transition);
        return std::nullopt;
    }

    return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};
}

StyleProperty TerrainContourLayer::getProperty(const std::string& name) const {
    return getLayerProperty(*this, name);
}

Mutable<Layer::Impl> TerrainContourLayer::mutableBaseImpl() const {
    return staticMutableCast<Layer::Impl>(mutableImpl());
}

} // namespace style
} // namespace mln

// clang-format on
