// clang-format off

// This file is generated. Edit scripts/generate-style-code.js, then run `make style-code`.

#include <mln/style/layers/slope_shading_layer.hpp>
#include <mln/style/layers/slope_shading_layer_impl.hpp>
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
const LayerTypeInfo* SlopeShadingLayer::Impl::staticTypeInfo() noexcept {
    const static LayerTypeInfo typeInfo{.type="slope-shading",
                                        .source=LayerTypeInfo::Source::NotRequired,
                                        .pass3d=LayerTypeInfo::Pass3D::NotRequired,
                                        .layout=LayerTypeInfo::Layout::NotRequired,
                                        .fadingTiles=LayerTypeInfo::FadingTiles::NotRequired,
                                        .crossTileIndex=LayerTypeInfo::CrossTileIndex::NotRequired,
                                        .tileKind=LayerTypeInfo::TileKind::NotRequired};
    return &typeInfo;
}

SlopeShadingLayer::SlopeShadingLayer(const std::string& layerID)
    : Layer(makeMutable<Impl>(layerID, std::string())) {
}

SlopeShadingLayer::SlopeShadingLayer(Immutable<Impl> impl_)
    : Layer(std::move(impl_)) {
}

SlopeShadingLayer::~SlopeShadingLayer() {
    weakFactory.invalidateWeakPtrs();
}

const SlopeShadingLayer::Impl& SlopeShadingLayer::impl() const {
    return static_cast<const Impl&>(*baseImpl);
}

Mutable<SlopeShadingLayer::Impl> SlopeShadingLayer::mutableImpl() const {
    return makeMutable<Impl>(impl());
}

std::unique_ptr<Layer> SlopeShadingLayer::cloneRef(const std::string& id_) const {
    auto impl_ = mutableImpl();
    impl_->id = id_;
    impl_->paint = SlopeShadingPaintProperties::Transitionable();
    return std::make_unique<SlopeShadingLayer>(std::move(impl_));
}

void SlopeShadingLayer::Impl::stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const {
}

// Layout properties


// Paint properties

PropertyValue<float> SlopeShadingLayer::getDefaultSlopeShadingOpacity() {
    return {0.55f};
}

const PropertyValue<float>& SlopeShadingLayer::getSlopeShadingOpacity() const {
    return impl().paint.template get<SlopeShadingOpacity>().value;
}

void SlopeShadingLayer::setSlopeShadingOpacity(const PropertyValue<float>& value) {
    if (value == getSlopeShadingOpacity())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<SlopeShadingOpacity>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void SlopeShadingLayer::setSlopeShadingOpacityTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<SlopeShadingOpacity>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions SlopeShadingLayer::getSlopeShadingOpacityTransition() const {
    return impl().paint.template get<SlopeShadingOpacity>().options;
}

PropertyValue<float> SlopeShadingLayer::getDefaultSlopeShadingPreset() {
    return {0.f};
}

const PropertyValue<float>& SlopeShadingLayer::getSlopeShadingPreset() const {
    return impl().paint.template get<SlopeShadingPreset>().value;
}

void SlopeShadingLayer::setSlopeShadingPreset(const PropertyValue<float>& value) {
    if (value == getSlopeShadingPreset())
        return;
    auto impl_ = mutableImpl();
    impl_->paint.template get<SlopeShadingPreset>().value = value;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void SlopeShadingLayer::setSlopeShadingPresetTransition(const TransitionOptions& options) {
    auto impl_ = mutableImpl();
    impl_->paint.template get<SlopeShadingPreset>().options = options;
    baseImpl = std::move(impl_);
}

TransitionOptions SlopeShadingLayer::getSlopeShadingPresetTransition() const {
    return impl().paint.template get<SlopeShadingPreset>().options;
}

using namespace conversion;

namespace {

constexpr uint8_t kPaintPropertyCount = 4u;

enum class Property : uint8_t {
    SlopeShadingOpacity,
    SlopeShadingPreset,
    SlopeShadingOpacityTransition,
    SlopeShadingPresetTransition,
};

template <typename T>
constexpr uint8_t toUint8(T t) noexcept {
    return uint8_t(mln::underlying_type(t));
}

constexpr const auto layerProperties = mapbox::eternal::hash_map<mapbox::eternal::string, uint8_t>(
    {{"slope-shading-opacity", toUint8(Property::SlopeShadingOpacity)},
     {"slope-shading-preset", toUint8(Property::SlopeShadingPreset)},
     {"slope-shading-opacity-transition", toUint8(Property::SlopeShadingOpacityTransition)},
     {"slope-shading-preset-transition", toUint8(Property::SlopeShadingPresetTransition)}});

StyleProperty getLayerProperty(const SlopeShadingLayer& layer, Property property) {
    switch (property) {
        case Property::SlopeShadingOpacity:
            return makeStyleProperty(layer.getSlopeShadingOpacity());
        case Property::SlopeShadingPreset:
            return makeStyleProperty(layer.getSlopeShadingPreset());
        case Property::SlopeShadingOpacityTransition:
            return makeStyleProperty(layer.getSlopeShadingOpacityTransition());
        case Property::SlopeShadingPresetTransition:
            return makeStyleProperty(layer.getSlopeShadingPresetTransition());
    }
    return {};
}

StyleProperty getLayerProperty(const SlopeShadingLayer& layer, const std::string& name) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) {
        return {};
    }
    return getLayerProperty(layer, static_cast<Property>(it->second));
}

} // namespace

Value SlopeShadingLayer::serialize() const {
    auto result = Layer::serialize();
    assert(result.getObject());
    for (const auto& property : layerProperties) {
        auto styleProperty = getLayerProperty(*this, static_cast<Property>(property.second));
        if (styleProperty.getKind() == StyleProperty::Kind::Undefined) continue;
        serializeProperty(result, styleProperty, property.first.c_str(), property.second < kPaintPropertyCount);
    }
    return result;
}

std::optional<Error> SlopeShadingLayer::setPropertyInternal(const std::string& name, const Convertible& value) {
    const auto it = layerProperties.find(name.c_str());
    if (it == layerProperties.end()) return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};

    auto property = static_cast<Property>(it->second);

    if (property == Property::SlopeShadingOpacity || property == Property::SlopeShadingPreset) {
        Error error;
        const auto& typedValue = convert<PropertyValue<float>>(value, error, false, false);
        if (!typedValue) {
            return error;
        }

        if (property == Property::SlopeShadingOpacity) {
            setSlopeShadingOpacity(*typedValue);
            return std::nullopt;
        }

        if (property == Property::SlopeShadingPreset) {
            setSlopeShadingPreset(*typedValue);
            return std::nullopt;
        }
    }

    Error error;
    std::optional<TransitionOptions> transition = convert<TransitionOptions>(value, error);
    if (!transition) {
        return error;
    }

    if (property == Property::SlopeShadingOpacityTransition) {
        setSlopeShadingOpacityTransition(*transition);
        return std::nullopt;
    }

    if (property == Property::SlopeShadingPresetTransition) {
        setSlopeShadingPresetTransition(*transition);
        return std::nullopt;
    }

    return Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};
}

StyleProperty SlopeShadingLayer::getProperty(const std::string& name) const {
    return getLayerProperty(*this, name);
}

Mutable<Layer::Impl> SlopeShadingLayer::mutableBaseImpl() const {
    return staticMutableCast<Layer::Impl>(mutableImpl());
}

} // namespace style
} // namespace mln

// clang-format on
