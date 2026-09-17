#include <mln/renderer/texture_pool.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/util/logging.hpp>

namespace mln {
TexturePool::TexturePool(uint32_t tilesize)
    : tileSize(tilesize) {}

TexturePool::~TexturePool() {}

void TexturePool::createRenderTarget(gfx::Context& context, const UnwrappedTileID& id, const Color& backgroundColor) {
    createRenderTarget(context, id, backgroundColor, tileSize);
}

uint32_t TexturePool::renderTargetSize(const UnwrappedTileID& id) const {
    const auto it = renderTargets.find(id);
    if (it == renderTargets.end() || !it->second || !it->second->getTexture()) {
        return 0;
    }
    return it->second->getTexture()->getSize().width;
}

std::size_t TexturePool::colorBytes() const {
    std::size_t bytes = 0;
    for (const auto& [id, target] : renderTargets) {
        if (target && target->getTexture()) {
            const auto size = target->getTexture()->getSize();
            bytes += static_cast<std::size_t>(size.width) * size.height * 4;
        }
    }
    return bytes;
}

void TexturePool::createRenderTarget(gfx::Context& context,
                                     const UnwrappedTileID& id,
                                     const Color& backgroundColor,
                                     uint32_t size) {
    // Keep an existing render target; recreating it every frame churns GL
    // texture memory and invalidates the texture bound to the terrain drawable
    if (auto it = renderTargets.find(id); it != renderTargets.end() && it->second) {
        if (renderTargetSize(id) == size) {
            it->second->setClearColor(backgroundColor);
            return;
        }
        renderTargets.erase(it);
    }
    // Drape targets render several overlapping draped tiles (a parent standing in for missing
    // children beside already-loaded children); the stencil clips them against each other,
    // as maplibre-gl-js does for its render-to-texture framebuffer.
    auto renderTarget = context.createRenderTarget(
        {size, size}, gfx::TextureChannelDataType::UnsignedByte, /*stencil=*/true);
    renderTarget->setClearColor(backgroundColor);
    renderTarget->setDrapeTileID(id);
    renderTargets[id] = std::move(renderTarget);
}

void TexturePool::removeStaleRenderTargets(const std::set<UnwrappedTileID>& currentTiles) {
    for (auto it = renderTargets.begin(); it != renderTargets.end();) {
        if (!currentTiles.contains(it->first)) {
            it = renderTargets.erase(it);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<RenderTarget> TexturePool::getRenderTarget(const UnwrappedTileID& id) const {
    return renderTargets.contains(id) ? renderTargets.at(id) : nullptr;
}

std::shared_ptr<RenderTarget> TexturePool::getRenderTargetAncestorOrDescendant(
    const UnwrappedTileID& id, std::optional<UnwrappedTileID>& terrainTileID) const {
    terrainTileID = std::nullopt;
    std::shared_ptr<RenderTarget> bestRenderTarget;
    for (const auto& [tileID, renderTarget] : renderTargets) {
        if (tileID == id || tileID.isChildOf(id) || id.isChildOf(tileID)) {
            if (!terrainTileID || tileID.canonical.z > terrainTileID->canonical.z) {
                terrainTileID = tileID;
                bestRenderTarget = renderTarget;
            }
        }
    }
    return bestRenderTarget;
}
} // namespace mln
