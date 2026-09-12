#pragma once

#include <mln/util/color.hpp>
#include <mln/util/immutable.hpp>

namespace mln {
namespace style {

class SkyObserver;

/**
 * @brief Sky configuration for a full-screen sky gradient above the horizon.
 *
 * DuckMaps fork only, task T3. Mirrors Terrain's own plumbing shape (see terrain.hpp) as the
 * precedent for a style root property: Parser reads the "sky" root key into a Parser::sky, the
 * whole Style::Impl re-parse path calls setSky()/onSkyChanged() the same way it does
 * setTerrain(), and the evaluated Impl reaches the renderer through
 * UpdateParameters::sky -> RenderOrchestrator -> Renderer::Impl::render.
 *
 * The style spec's `sky` root property (https://maplibre.org/maplibre-style-spec/sky/) declares
 * its seven properties as paint-style properties with transitions in maplibre-gl-js. This fork
 * does NOT implement transitions for sky: Impl holds plain evaluated values, applied instantly
 * on style (re)load, exactly as instructed by the task that added this file. A style swap will
 * therefore cut to the new sky rather than fade to it.
 *
 * Only three of the seven properties are actually drawn (sky-color, horizon-color,
 * sky-horizon-blend, in Renderer::Impl::render's sky pass). The other four (fog-color,
 * fog-ground-blend, horizon-fog-blend, atmosphere-blend) are parsed and stored here so a style
 * carrying them is never rejected, but nothing reads them - see their getters below.
 */
class Sky {
public:
    Sky();

    /// sky-color, default #88C6FC. RENDERED: the colour at the top of the sky gradient.
    Color getSkyColor() const;

    /// horizon-color, default #ffffff. RENDERED: the colour at the horizon line, blended with
    /// sky-color over sky-horizon-blend.
    Color getHorizonColor() const;

    /// fog-color, default #ffffff. PARSED AND STORED ONLY - this fork draws no atmospheric fog,
    /// so nothing reads this value. Kept so a style carrying it is not rejected.
    Color getFogColor() const;

    /// fog-ground-blend, default 0.5. PARSED AND STORED ONLY - no fog-on-ground blending is
    /// implemented; nothing reads this value.
    float getFogGroundBlend() const;

    /// horizon-fog-blend, default 0.8. PARSED AND STORED ONLY - no horizon/fog blending is
    /// implemented; nothing reads this value.
    float getHorizonFogBlend() const;

    /// sky-horizon-blend, default 0.8. RENDERED: how far, in the same screen-space units as
    /// u_horizon, the sky-to-horizon gradient extends above the horizon line.
    float getSkyHorizonBlend() const;

    /// atmosphere-blend, default 0.8. PARSED AND STORED ONLY - this fork has no atmosphere
    /// (globe) projection, so nothing reads this value; u_sky_blend (projectionTransition) is
    /// hardcoded to 0 at the draw site instead, matching mercator on the web.
    float getAtmosphereBlend() const;

    // Internal implementation
    class Impl;
    Immutable<Impl> impl;
    explicit Sky(Immutable<Impl>);
    Mutable<Impl> mutableImpl() const;

    SkyObserver* observer = nullptr;
    void setObserver(SkyObserver*);
};

} // namespace style
} // namespace mln
