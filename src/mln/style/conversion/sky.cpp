#include <mln/style/conversion/sky.hpp>
#include <mln/style/conversion/constant.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/sky_impl.hpp>

namespace mln {
namespace style {
namespace conversion {

namespace {

// Returns false only on a present-but-wrongly-typed value; a missing member keeps `out` at
// its spec default (Sky::Impl's own constructor already set it).
bool parseColorMember(const Convertible& value, const char* name, Color& out, Error& error) {
    std::optional<Convertible> member = objectMember(value, name);
    if (!member) {
        return true;
    }
    std::optional<Color> converted = convert<Color>(*member, error);
    if (!converted) {
        return false;
    }
    out = *converted;
    return true;
}

bool parseFloatMember(const Convertible& value, const char* name, float& out, Error& error) {
    std::optional<Convertible> member = objectMember(value, name);
    if (!member) {
        return true;
    }
    std::optional<float> converted = convert<float>(*member, error);
    if (!converted) {
        return false;
    }
    out = *converted;
    return true;
}

} // namespace

std::optional<Sky> Converter<Sky>::operator()(const Convertible& value, Error& error) const {
    if (!isObject(value)) {
        error.message = "sky must be an object";
        return std::nullopt;
    }

    // Defaults come from Sky::Impl's own constructor (the style spec's own defaults, see
    // sky_impl.hpp) - each property below only overwrites its default when present and
    // correctly typed.
    auto impl = makeMutable<Sky::Impl>();

    // RENDERED
    if (!parseColorMember(value, "sky-color", impl->skyColor, error)) return std::nullopt;
    if (!parseColorMember(value, "horizon-color", impl->horizonColor, error)) return std::nullopt;
    if (!parseFloatMember(value, "sky-horizon-blend", impl->skyHorizonBlend, error)) return std::nullopt;

    // PARSED AND STORED ONLY - see sky.hpp's getters for each of these four.
    if (!parseColorMember(value, "fog-color", impl->fogColor, error)) return std::nullopt;
    if (!parseFloatMember(value, "fog-ground-blend", impl->fogGroundBlend, error)) return std::nullopt;
    if (!parseFloatMember(value, "horizon-fog-blend", impl->horizonFogBlend, error)) return std::nullopt;
    if (!parseFloatMember(value, "atmosphere-blend", impl->atmosphereBlend, error)) return std::nullopt;

    return Sky(std::move(impl));
}

} // namespace conversion
} // namespace style
} // namespace mln
