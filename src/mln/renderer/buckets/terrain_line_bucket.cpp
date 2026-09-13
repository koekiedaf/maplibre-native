#include <mln/renderer/buckets/terrain_line_bucket.hpp>

#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/terrain_line_layer_properties.hpp>

#include <algorithm>
#include <cmath>

namespace mln {

TerrainLineBucket::~TerrainLineBucket() {
    sharedVertices->release();
}

void TerrainLineBucket::upload([[maybe_unused]] gfx::UploadPass& uploadPass) {
    uploaded = true;
}

bool TerrainLineBucket::hasData() const {
    return !segments.empty();
}

float TerrainLineBucket::getQueryRadius(const RenderLayer& layer) const {
    const auto& evaluated =
        static_cast<const style::TerrainLineLayerProperties&>(*layer.evaluatedProperties).evaluated;
    // No data-driven paint on this layer type (see this file's header comment and
    // TerrainLineLayerProperties's own), so these are already-evaluated constants for the
    // current zoom, not per-feature bindings - unlike LineBucket, no `feature`/`zoom` argument is
    // needed to read them.
    const float widthPx = std::max(evaluated.get<style::TerrainLineWidth>(), evaluated.get<style::TerrainLineHaloWidth>());
    return widthPx / 2.0f + std::abs(evaluated.get<style::TerrainLineOffset>());
}

} // namespace mln
