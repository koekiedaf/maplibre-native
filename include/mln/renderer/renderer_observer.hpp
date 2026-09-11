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

    /// The camera-ground RISE changed (task 2.0b): the rendered terrain height under the
    /// camera's own ground point MINUS the rendered terrain height under the map centre, both
    /// sampled in the same render frame. Reporting the difference rather than an absolute height
    /// means the map thread's clamp never has to line this sample up against its own centre
    /// altitude, which can still be catching up to the terrain at the moment the clamp runs -
    /// the rise is already self-consistent. `std::nullopt` when there is no render terrain this
    /// frame, meaning the camera-above-terrain clamp should be off entirely rather than clamped
    /// to a flat sea level.
    virtual void onTerrainCameraGroundRiseChanged(std::optional<double> /*riseMeters*/) {}

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
