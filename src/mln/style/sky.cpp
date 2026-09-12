#include <mln/style/sky.hpp>
#include <mln/style/sky_impl.hpp>
#include <mln/style/sky_observer.hpp>
#include <utility>

namespace mln {
namespace style {

namespace {
SkyObserver nullObserver;
}

Sky::Sky(Immutable<Sky::Impl> impl_)
    : impl(std::move(impl_)),
      observer(&nullObserver) {}

Sky::Sky()
    : Sky(makeMutable<Impl>()) {}

void Sky::setObserver(SkyObserver* observer_) {
    observer = observer_ ? observer_ : &nullObserver;
}

Mutable<Sky::Impl> Sky::mutableImpl() const {
    return makeMutable<Impl>(*impl);
}

Color Sky::getSkyColor() const {
    return impl->skyColor;
}

Color Sky::getHorizonColor() const {
    return impl->horizonColor;
}

Color Sky::getFogColor() const {
    return impl->fogColor;
}

float Sky::getFogGroundBlend() const {
    return impl->fogGroundBlend;
}

float Sky::getHorizonFogBlend() const {
    return impl->horizonFogBlend;
}

float Sky::getSkyHorizonBlend() const {
    return impl->skyHorizonBlend;
}

float Sky::getAtmosphereBlend() const {
    return impl->atmosphereBlend;
}

float Sky::calculateFogBlendOpacity(double pitchDegrees) {
    // Ported verbatim - see the doc comment in sky.hpp for the exact source line and why the
    // thresholds must not change.
    return pitchDegrees < 60.0 ? 0.0f
           : pitchDegrees < 70.0 ? static_cast<float>((pitchDegrees - 60.0) / 10.0)
                                 : 1.0f;
}

} // namespace style
} // namespace mln
