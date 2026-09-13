#pragma once
#include <mln/renderer/bucket.hpp>
#include <mln/gfx/vertex_vector.hpp>
#include <mln/gfx/index_vector.hpp>
#include <mln/shaders/segment.hpp>
#include <mln/style/layers/terrain_line_layer_properties.hpp>

namespace mln {

class BucketParameters;

// DuckMaps fork only. terrain-line's ribbon is a chain of INDEPENDENT quads, four vertices and
// two triangles each, no joins - the web engine's shape (routes3d.js pushSeg(), see
// docs/plans/2026-09-11-engine-layer-plumbing.md), NOT LineBucket's mitre/join tessellation.
// There are no data-driven paint properties on this layer type (see
// TerrainLineLayer::Impl::hasLayoutDifference's comment), so unlike LineBucket/CircleBucket
// there is no paintPropertyBinders map here at all - one uniform colour/width/dash per drawable,
// filled straight from TerrainLinePaintProperties::PossiblyEvaluated by the tweaker.
using TerrainLineLayoutVertex =
    gfx::Vertex<TypeList<attributes::pos, attributes::other, attributes::flag, attributes::dist>>;

class TerrainLineBucket final : public Bucket {
public:
    TerrainLineBucket() = default;
    ~TerrainLineBucket() override;

    bool hasData() const override;

    void upload(gfx::UploadPass&) override;

    // Bookkeeping fix (task: terrain-line hit testing, 13 Sept 2026): LineBucket's own
    // getQueryRadius() is what tells FeatureIndex::query how far around the tap point in tile
    // units to widen its spatial-grid search before the precise per-feature test
    // (RenderTerrainLineLayer::queryIntersectsFeature) runs at all - a candidate outside this
    // radius is never even offered to that test. The base Bucket class defaults this to 0, which
    // meant every terrain-line tile was searched with no width tolerance whatsoever. Mirrors
    // LineBucket::getQueryRadius(): half the wider of the line and its halo, plus the offset this
    // layer's ladder-rail trick applies (see RenderTerrainLineLayer::queryIntersectsFeature).
    float getQueryRadius(const RenderLayer&) const override;

    static TerrainLineLayoutVertex layoutVertex(
        Point<int16_t> pos, Point<int16_t> other, int16_t flagDirection, int16_t flagSide, float dist) {
        return TerrainLineLayoutVertex{{{pos.x, pos.y}}, {{other.x, other.y}}, {{flagDirection, flagSide}}, {{dist}}};
    }

    using VertexVector = gfx::VertexVector<TerrainLineLayoutVertex>;
    const std::shared_ptr<VertexVector> sharedVertices = std::make_shared<VertexVector>();
    VertexVector& vertices = *sharedVertices;

    using TriangleIndexVector = gfx::IndexVector<gfx::Triangles>;
    const std::shared_ptr<TriangleIndexVector> sharedTriangles = std::make_shared<TriangleIndexVector>();
    TriangleIndexVector& triangles = *sharedTriangles;

    SegmentVector segments;
};

} // namespace mln
