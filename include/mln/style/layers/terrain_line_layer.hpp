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

class TerrainLineLayer final : public Layer {
public:
    TerrainLineLayer(const std::string& layerID, const std::string& sourceID);
    ~TerrainLineLayer() override;

    // Paint properties

    static PropertyValue<float> getDefaultTerrainLineBlur();
    const PropertyValue<float>& getTerrainLineBlur() const;
    void setTerrainLineBlur(const PropertyValue<float>&);
    void setTerrainLineBlurTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineBlurTransition() const;

    static PropertyValue<Color> getDefaultTerrainLineColor();
    const PropertyValue<Color>& getTerrainLineColor() const;
    void setTerrainLineColor(const PropertyValue<Color>&);
    void setTerrainLineColorTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineColorTransition() const;

    static PropertyValue<std::vector<float>> getDefaultTerrainLineDasharray();
    const PropertyValue<std::vector<float>>& getTerrainLineDasharray() const;
    void setTerrainLineDasharray(const PropertyValue<std::vector<float>>&);
    void setTerrainLineDasharrayTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineDasharrayTransition() const;

    static PropertyValue<float> getDefaultTerrainLineFade();
    const PropertyValue<float>& getTerrainLineFade() const;
    void setTerrainLineFade(const PropertyValue<float>&);
    void setTerrainLineFadeTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineFadeTransition() const;

    static PropertyValue<float> getDefaultTerrainLineFadeDistance();
    const PropertyValue<float>& getTerrainLineFadeDistance() const;
    void setTerrainLineFadeDistance(const PropertyValue<float>&);
    void setTerrainLineFadeDistanceTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineFadeDistanceTransition() const;

    static PropertyValue<float> getDefaultTerrainLineGhostOpacity();
    const PropertyValue<float>& getTerrainLineGhostOpacity() const;
    void setTerrainLineGhostOpacity(const PropertyValue<float>&);
    void setTerrainLineGhostOpacityTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineGhostOpacityTransition() const;

    static PropertyValue<float> getDefaultTerrainLineHaloBlur();
    const PropertyValue<float>& getTerrainLineHaloBlur() const;
    void setTerrainLineHaloBlur(const PropertyValue<float>&);
    void setTerrainLineHaloBlurTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineHaloBlurTransition() const;

    static PropertyValue<Color> getDefaultTerrainLineHaloColor();
    const PropertyValue<Color>& getTerrainLineHaloColor() const;
    void setTerrainLineHaloColor(const PropertyValue<Color>&);
    void setTerrainLineHaloColorTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineHaloColorTransition() const;

    static PropertyValue<float> getDefaultTerrainLineHaloWidth();
    const PropertyValue<float>& getTerrainLineHaloWidth() const;
    void setTerrainLineHaloWidth(const PropertyValue<float>&);
    void setTerrainLineHaloWidthTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineHaloWidthTransition() const;

    static PropertyValue<float> getDefaultTerrainLineOffset();
    const PropertyValue<float>& getTerrainLineOffset() const;
    void setTerrainLineOffset(const PropertyValue<float>&);
    void setTerrainLineOffsetTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineOffsetTransition() const;

    static PropertyValue<float> getDefaultTerrainLineOpacity();
    const PropertyValue<float>& getTerrainLineOpacity() const;
    void setTerrainLineOpacity(const PropertyValue<float>&);
    void setTerrainLineOpacityTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineOpacityTransition() const;

    static PropertyValue<float> getDefaultTerrainLineWidth();
    const PropertyValue<float>& getTerrainLineWidth() const;
    void setTerrainLineWidth(const PropertyValue<float>&);
    void setTerrainLineWidthTransition(const TransitionOptions&);
    TransitionOptions getTerrainLineWidthTransition() const;

    // Private implementation

    class Impl;
    const Impl& impl() const;

    Mutable<Impl> mutableImpl() const;
    TerrainLineLayer(Immutable<Impl>);
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
