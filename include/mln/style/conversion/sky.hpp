#pragma once

#include <mln/style/sky.hpp>
#include <mln/style/conversion.hpp>

#include <optional>

namespace mln {
namespace style {
namespace conversion {

// DuckMaps fork only, task T3. Mirrors conversion/terrain.hpp. Parses all seven of the style
// spec's `sky` root properties (https://maplibre.org/maplibre-style-spec/sky/) so a style
// carrying any of them is never rejected - see sky.hpp for which three are actually rendered.
template <>
struct Converter<Sky> {
public:
    std::optional<Sky> operator()(const Convertible& value, Error& error) const;
};

} // namespace conversion
} // namespace style
} // namespace mln
