#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/renderer/render_target.hpp>
#include <optional>
#include <set>

namespace mln {
class TexturePool {
public:
    TexturePool(uint32_t tilesize);
    ~TexturePool();

    std::shared_ptr<RenderTarget> getRenderTarget(const UnwrappedTileID& id) const;
    std::shared_ptr<RenderTarget> getRenderTargetAncestorOrDescendant(
        const UnwrappedTileID& id, std::optional<UnwrappedTileID>& terrainTileID) const;
    void createRenderTarget(gfx::Context& context, const UnwrappedTileID& id, const Color& backgroundColor);
    /// Performance round, Phase 2 dial 1: the same, at an explicit size. An existing target of
    /// another size is replaced (and so re-baked); the caller applies the hysteresis.
    void createRenderTarget(gfx::Context& context,
                            const UnwrappedTileID& id,
                            const Color& backgroundColor,
                            uint32_t size);
    uint32_t defaultTileSize() const { return tileSize; }
    /// The size of the target held for `id`, 0 if none.
    uint32_t renderTargetSize(const UnwrappedTileID& id) const;
    /// Colour texture bytes of every target held (RGBA8), for the panel and the trace.
    std::size_t colorBytes() const;

    /// Remove render targets for tiles that are no longer part of the given set
    void removeStaleRenderTargets(const std::set<UnwrappedTileID>& currentTiles);

    /// Drape render targets currently held.
    std::size_t size() const { return renderTargets.size(); }

    template <typename Func /* void(std::shared_ptr<RenderTarget>&) */>
    void visitRenderTargets(Func f) {
        for (auto& pair : renderTargets) {
            if (pair.second) {
                f(pair.second);
            }
        }
    }

    template <typename Func /* void(std::shared_ptr<RenderTarget>&) */>
    void visitRenderTargets(Func f) const {
        for (const auto& pair : renderTargets) {
            if (pair.second) {
                f(pair.second);
            }
        }
    }

private:
    uint32_t tileSize;

    std::map<UnwrappedTileID, std::shared_ptr<RenderTarget>> renderTargets;
};

} // namespace mln
