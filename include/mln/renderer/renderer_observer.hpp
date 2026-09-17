#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/util/font_stack.hpp>
#include <mln/gfx/rendering_stats.hpp>
#include <mln/text/glyph_range.hpp>
#include <mln/tile/tile_operation.hpp>
#include <mln/gfx/backend.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/util/symbol_error_observer.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>

namespace mln {

namespace gfx {
class ShaderRegistry;
}

class RendererObserver : public SymbolErrorObserver {
public:
    ~RendererObserver() override = default;

    enum class RenderMode : uint32_t {
        Partial,
        Full
    };

    /// Signals that a repaint is required
    virtual void onInvalidate() {}

    /// Resource failed to download / parse
    virtual void onResourceError(std::exception_ptr) {}

    /// First frame
    virtual void onWillStartRenderingMap() {}

    /// Start of frame, initial is the first frame for this map
    virtual void onWillStartRenderingFrame() {}

    /// End of frame, booleans flags that a repaint is required and that placement changed.
    virtual void onDidFinishRenderingFrame(RenderMode, bool /*repaint*/, bool /*placementChanged*/) {}

    /// End of frame, booleans flags that a repaint is required and that placement changed.
    virtual void onDidFinishRenderingFrame(RenderMode mode,
                                           bool repaint,
                                           bool placementChanged,
                                           double /*frameEncodingTime*/,
                                           double /*frameRenderingTime*/) {
        onDidFinishRenderingFrame(mode, repaint, placementChanged);
    }

    virtual void onDidFinishRenderingFrame(RenderMode mode,
                                           bool repaint,
                                           bool placementChanged,
                                           const gfx::RenderingStats& stats) {
        onDidFinishRenderingFrame(mode, repaint, placementChanged, stats.encodingTime, stats.renderingTime);
    }

    /// Final frame
    virtual void onDidFinishRenderingMap() {}

    /// The rendered terrain height under the map centre changed. Only the render side knows
    /// it - the DEM lives there - so a map that wants its centre to ride the terrain
    /// (Map::setCenterClampedToGround) learns of it here, one frame behind.
    virtual void onTerrainCenterElevationChanged(double /*elevationMeters*/) {}
    /// Task C7: the highest ground along the flight line ahead, already discounted by the climb
    /// gradient, in metres above sea level; nullopt when no DEM along that line could be read.
    /// Absolute rather than relative to the centre, because the camera's altitude is no longer
    /// tied to the centre.
    virtual void onTerrainForwardRequirementChanged(std::optional<double> /*requirementMsl*/) {}
    /// Task C9 (16 September 2026): where the ray through the CENTRE of the screen first meets
    /// the terrain, marched against the DEM on the render thread. This is the pivot David asked
    /// for in so many words - "a laser pointer through the centre of the screen; whatever
    /// terrain it hits first is the pivot" - and it differs from the ground under the centre's
    /// map coordinate exactly on a slope facing the camera, where the ray meets the face nearer
    /// and higher than the DEM under the centre's sea-level coordinate. nullopt when no DEM
    /// along the ray could be read or the ray meets nothing before its far end.
    struct CenterRayHit {
        double longitude;
        double latitude;
        double altitudeMeters;
        /// Straight-line distance from the camera to the hit, in metres, for the trace.
        double distanceMeters;
    };
    virtual void onTerrainCenterRayHitChanged(std::optional<CenterRayHit> /*hit*/) {}
    /// Task E (17 September 2026): the camera's altitude minus the highest terrain along the
    /// centre ray between the camera and its first hit, in metres. A tilt stops where this
    /// would fall below the tilt clearance. nullopt when nothing along the ray could be read.
    virtual void onTerrainCenterRayClearanceChanged(std::optional<double> /*metres*/) {}
    /// Task E: the flattest pitch (radians) at which an orbit at the current radius about the
    /// centre-ray hit still keeps the tilt clearance along the ray; nullopt when unknown.
    virtual void onTerrainCenterRayMaxPitchChanged(std::optional<double> /*radians*/) {}
    /// Round G: the number of terrain mesh tiles the last rendered frame drew, for the app's
    /// telemetry recorder. Reported when it changes.
    virtual void onTerrainMeshTileCountChanged(std::size_t /*count*/) {}
    /// Performance round: drape render targets the texture pool holds after this frame.
    virtual void onTerrainDrapeTargetCountChanged(std::size_t /*count*/) {}

    /// The camera-ground RISE changed (task 2.0b): the rendered terrain height under the
    /// camera's own ground point MINUS the rendered terrain height under the map centre, both
    /// sampled in the same render frame. Reporting the difference rather than an absolute height
    /// means the map thread's clamp never has to line this sample up against its own centre
    /// altitude, which can still be catching up to the terrain at the moment the clamp runs -
    /// the rise is already self-consistent. `std::nullopt` when there is no render terrain this
    /// frame, meaning the camera-above-terrain clamp should be off entirely rather than clamped
    /// to a flat sea level.
    virtual void onTerrainCameraGroundRiseChanged(std::optional<double> /*riseMeters*/) {}

    /// DuckMaps fork only: item 6 of the band-aid audit
    /// (docs/plans/2026-09-11-band-aids.md). `Renderer::Impl::render` holds four bounded
    /// counters (`terrainCoverRetryFrames`, `centerElevationUnknownFrames`,
    /// `centerElevationSettleFrames`, `terrainSettleFrames`, all declared in
    /// `renderer_impl.hpp`) that keep a frame reported as `RenderMode::Partial` while terrain
    /// is still converging. Each is a real bound for a real reason, but once one is exhausted
    /// while the underlying condition is STILL true, the frame is reported
    /// `RenderMode::Full` exactly as if the condition had genuinely resolved - with nothing
    /// distinguishing "settled" from "gave up" anywhere outside the renderer. This call is
    /// that distinction, made visible: `std::nullopt` when the most recently rendered frame
    /// needed no bound to give up (the common, healthy case, including every frame reported
    /// `Partial`), or a comma-joined list of the counter member name(s) above that gave up
    /// keeping this frame back, in their declaration order, when one or more did. Only fired
    /// when the value changes from the previous frame, the same way
    /// `onTerrainCameraGroundRiseChanged` above is.
    virtual void onSettleBoundGivenUp(const std::optional<std::string>& /*boundNames*/) {}

    /// Style is missing an image
    using StyleImageMissingCallback = std::function<void()>;
    virtual void onStyleImageMissing(const std::string&, const StyleImageMissingCallback& done) { done(); }
    virtual void onRemoveUnusedStyleImages(const std::vector<std::string>&) {}

    // Entry point for custom shader registration
    virtual void onRegisterShaders(gfx::ShaderRegistry&) {};
    virtual void onPreCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) {}
    virtual void onPostCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) {}
    virtual void onShaderCompileFailed(shaders::BuiltIn, gfx::Backend::Type, const std::string&) {}

    // Glyph loading
    virtual void onGlyphsLoaded(const FontStack&, const GlyphRange&) {}
    virtual void onGlyphsError(const FontStack&, const GlyphRange&, std::exception_ptr) {}
    virtual void onGlyphsRequested(const FontStack&, const GlyphRange&) {}

    // Tile loading
    virtual void onTileAction(TileOperation, const OverscaledTileID&, const std::string&) {}

    /// Render layer or drawable failed
    virtual void onRenderError(std::exception_ptr) {}
};

} // namespace mln
