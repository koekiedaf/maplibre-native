#include <mln/renderer/buckets/terrain_line_bucket.hpp>

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

} // namespace mln
