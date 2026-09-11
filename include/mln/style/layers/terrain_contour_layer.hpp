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

class TerrainContourLayer final : public Layer {
public:
    TerrainContourLayer(const std::string& layerID);
    ~TerrainContourLayer() override;

    // Paint properties

    static PropertyValue<float> getDefaultTerrainContourFadeHi();
    const PropertyValue<float>& getTerrainContourFadeHi() const;
    void setTerrainContourFadeHi(const PropertyValue<float>&);
    void setTerrainContourFadeHiTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourFadeHiTransition() const;

    static PropertyValue<float> getDefaultTerrainContourFadeLo();
    const PropertyValue<float>& getTerrainContourFadeLo() const;
    void setTerrainContourFadeLo(const PropertyValue<float>&);
    void setTerrainContourFadeLoTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourFadeLoTransition() const;

    static PropertyValue<Color> getDefaultTerrainContourIndexColor();
    const PropertyValue<Color>& getTerrainContourIndexColor() const;
    void setTerrainContourIndexColor(const PropertyValue<Color>&);
    void setTerrainContourIndexColorTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourIndexColorTransition() const;

    static PropertyValue<float> getDefaultTerrainContourIndexInterval();
    const PropertyValue<float>& getTerrainContourIndexInterval() const;
    void setTerrainContourIndexInterval(const PropertyValue<float>&);
    void setTerrainContourIndexIntervalTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourIndexIntervalTransition() const;

    static PropertyValue<float> getDefaultTerrainContourIndexOpacity();
    const PropertyValue<float>& getTerrainContourIndexOpacity() const;
    void setTerrainContourIndexOpacity(const PropertyValue<float>&);
    void setTerrainContourIndexOpacityTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourIndexOpacityTransition() const;

    static PropertyValue<float> getDefaultTerrainContourIndexWidth();
    const PropertyValue<float>& getTerrainContourIndexWidth() const;
    void setTerrainContourIndexWidth(const PropertyValue<float>&);
    void setTerrainContourIndexWidthTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourIndexWidthTransition() const;

    static PropertyValue<Color> getDefaultTerrainContourMinorColor();
    const PropertyValue<Color>& getTerrainContourMinorColor() const;
    void setTerrainContourMinorColor(const PropertyValue<Color>&);
    void setTerrainContourMinorColorTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourMinorColorTransition() const;

    static PropertyValue<float> getDefaultTerrainContourMinorInterval();
    const PropertyValue<float>& getTerrainContourMinorInterval() const;
    void setTerrainContourMinorInterval(const PropertyValue<float>&);
    void setTerrainContourMinorIntervalTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourMinorIntervalTransition() const;

    static PropertyValue<float> getDefaultTerrainContourMinorOpacity();
    const PropertyValue<float>& getTerrainContourMinorOpacity() const;
    void setTerrainContourMinorOpacity(const PropertyValue<float>&);
    void setTerrainContourMinorOpacityTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourMinorOpacityTransition() const;

    static PropertyValue<float> getDefaultTerrainContourMinorWidth();
    const PropertyValue<float>& getTerrainContourMinorWidth() const;
    void setTerrainContourMinorWidth(const PropertyValue<float>&);
    void setTerrainContourMinorWidthTransition(const TransitionOptions&);
    TransitionOptions getTerrainContourMinorWidthTransition() const;

    // Private implementation

    class Impl;
    const Impl& impl() const;

    Mutable<Impl> mutableImpl() const;
    TerrainContourLayer(Immutable<Impl>);
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
