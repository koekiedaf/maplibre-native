#pragma once

#include <mln/style/sky.hpp>
#include <mln/util/color.hpp>
#include <mln/util/immutable.hpp>

namespace mln {
namespace style {

// DuckMaps fork only, task T3. Plain evaluated values, no Transitioning<T> - see sky.hpp's own
// comment on why this fork does not implement sky transitions. Defaults are the style spec's
// own (https://maplibre.org/maplibre-style-spec/sky/), verified against
// container/server/app/map/vendor/maplibre-gl-6.mjs in the server checkout.
class Sky::Impl {
public:
    Impl()
        : skyColor(*Color::parse("#88C6FC")),
          horizonColor(*Color::parse("#ffffff")),
          fogColor(*Color::parse("#ffffff")),
          fogGroundBlend(0.5f),
          horizonFogBlend(0.8f),
          skyHorizonBlend(0.8f),
          atmosphereBlend(0.8f) {}

    Color skyColor;
    Color horizonColor;
    Color fogColor; // parsed, unused - see Sky::getFogColor()
    float fogGroundBlend; // parsed, unused - see Sky::getFogGroundBlend()
    float horizonFogBlend; // parsed, unused - see Sky::getHorizonFogBlend()
    float skyHorizonBlend;
    float atmosphereBlend; // parsed, unused - see Sky::getAtmosphereBlend()

    bool operator==(const Impl& other) const {
        return skyColor == other.skyColor && horizonColor == other.horizonColor &&
               fogColor == other.fogColor && fogGroundBlend == other.fogGroundBlend &&
               horizonFogBlend == other.horizonFogBlend && skyHorizonBlend == other.skyHorizonBlend &&
               atmosphereBlend == other.atmosphereBlend;
    }
};

} // namespace style
} // namespace mln
