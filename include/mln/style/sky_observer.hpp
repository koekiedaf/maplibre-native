#pragma once

namespace mln {
namespace style {

class Sky;

// DuckMaps fork only, task T3. Mirrors terrain_observer.hpp exactly; see sky.hpp's own comment
// for why this exists despite Sky having no public mutation API - Style::Impl::setSky() still
// needs a way to signal Style::Impl::observer->onUpdate() on style (re)load.
class SkyObserver {
public:
    virtual ~SkyObserver() = default;

    virtual void onSkyChanged(const Sky&) {}
};

} // namespace style
} // namespace mln
