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
 * Six of the seven properties are drawn: sky-color, horizon-color, sky-horizon-blend in
 * Renderer::Impl::render's sky pass, and fog-color, fog-ground-blend, horizon-fog-blend in the
 * terrain ground fog (TerrainLayerTweaker::execute, mtl/terrain.hpp's fragmentMain) - MapLibre
 * GL JS's own terrain fog, ported from container/server/app/map/vendor/maplibre-gl-6.mjs. Only
 * atmosphere-blend remains parsed and stored only: this fork has no globe projection, so nothing
 * reads it - see its getter below.
 */
class Sky {
public:
    Sky();

    /// sky-color, default #88C6FC. RENDERED: the colour at the top of the sky gradient.
    Color getSkyColor() const;

    /// horizon-color, default #ffffff. RENDERED: the colour at the horizon line, blended with
    /// sky-color over sky-horizon-blend.
    Color getHorizonColor() const;

    /// fog-color, default #ffffff. RENDERED: the colour the ground fades to as terrain
    /// approaches the horizon (TerrainLayerTweaker::execute's TerrainEvaluatedPropsUBO, sampled
    /// in mtl/terrain.hpp's fragmentMain).
    Color getFogColor() const;

    /// fog-ground-blend, default 0.5. RENDERED: the fog depth (0 at the camera, 1 at the far
    /// plane) beyond which the terrain ground starts blending into the fog/horizon colour.
    float getFogGroundBlend() const;

    /// horizon-fog-blend, default 0.8. RENDERED: how far past fog-ground-blend the ground
    /// blends from fog-color into horizon-color, before the sky's own horizon-color takes over.
    float getHorizonFogBlend() const;

    /// sky-horizon-blend, default 0.8. RENDERED: how far, in the same screen-space units as
    /// u_horizon, the sky-to-horizon gradient extends above the horizon line.
    float getSkyHorizonBlend() const;

    /// atmosphere-blend, default 0.8. PARSED AND STORED ONLY - this fork has no atmosphere
    /// (globe) projection, so nothing reads this value; u_sky_blend (projectionTransition) is
    /// hardcoded to 0 at the draw site instead, matching mercator on the web.
    float getAtmosphereBlend() const;

    /// Ports maplibre-gl-6.mjs's own `Sky.calculateFogBlendOpacity` (maplibre-gl-js source:
    /// src/style/sky.ts) VERBATIM - confirmed against the bundle's minified
    /// `calculateFogBlendOpacity(e){return e<60?0:e<70?(e-60)/10:1}`. Ramps the terrain ground
    /// fog's opacity from 0 to 1 as pitch goes from 60 to 70 degrees; below 60 there is no fog
    /// at all, at 70 and above it is fully opaque (subject to fog-ground-blend/horizon-fog-blend
    /// still gating where on the ground it starts). `pitchDegrees` matches the web's own
    /// `transform.pitch`, which is DEGREES - TransformState::getPitch() is radians, so callers
    /// must util::rad2deg() first. Do NOT change the 60/70 thresholds or the linear ramp between
    /// them; they are the web's own numbers, not tunable here.
    static float calculateFogBlendOpacity(double pitchDegrees);

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
