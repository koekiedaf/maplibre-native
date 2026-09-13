// clang-format off

// This file is generated. Do not edit.

#pragma once

#include <mln/style/layer.hpp>
#include <mln/style/filter.hpp>
#include <mln/style/property_value.hpp>
#include <mln/util/color.hpp>

namespace mln {
namespace style {

class TransitionOptions;

class SlopeShadingLayer final : public Layer {
public:
    SlopeShadingLayer(const std::string& layerID);
    ~SlopeShadingLayer() override;

    // Paint properties

    static PropertyValue<float> getDefaultSlopeShadingOpacity();
    const PropertyValue<float>& getSlopeShadingOpacity() const;
    void setSlopeShadingOpacity(const PropertyValue<float>&);
    void setSlopeShadingOpacityTransition(const TransitionOptions&);
    TransitionOptions getSlopeShadingOpacityTransition() const;

    static PropertyValue<float> getDefaultSlopeShadingPreset();
    const PropertyValue<float>& getSlopeShadingPreset() const;
    void setSlopeShadingPreset(const PropertyValue<float>&);
    void setSlopeShadingPresetTransition(const TransitionOptions&);
    TransitionOptions getSlopeShadingPresetTransition() const;

    // Private implementation

    class Impl;
    const Impl& impl() const;

    Mutable<Impl> mutableImpl() const;
    SlopeShadingLayer(Immutable<Impl>);
    std::unique_ptr<Layer> cloneRef(const std::string& id) const final;

protected:
    // Dynamic properties
    std::optional<conversion::Error> setPropertyInternal(const std::string& name, const conversion::Convertible& value) final;

    StyleProperty getProperty(const std::string& name) const final;
    Value serialize() const final;

    Mutable<Layer::Impl> mutableBaseImpl() const final;
};

} // namespace style
} // namespace mln

// clang-format on
