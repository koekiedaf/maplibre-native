#pragma once
#include <mln/geometry/feature_index.hpp>
#include <mln/layout/layout.hpp>
#include <mln/layout/pattern_layout.hpp>
#include <mln/renderer/bucket_parameters.hpp>
#include <mln/renderer/buckets/terrain_line_bucket.hpp>
#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/terrain_line_layer_impl.hpp>
#include <mln/util/containers.hpp>

#include <cmath>
#include <limits>

namespace mln {

// DuckMaps fork only. The geometry-building driver for terrain-line, mirroring CircleLayout's
// shape (a bespoke Layout subclass, not the generic PatternLayout<> - we have no pattern and no
// data-driven properties to cross-fade, see the task brief) rather than LineBucket's mitre/join
// tessellation, since the web engine's ribbon is a chain of independent quads with no joins.
class TerrainLineLayout final : public Layout {
public:
    TerrainLineLayout(const BucketParameters& parameters,
                      const std::vector<Immutable<style::LayerProperties>>& group,
                      std::unique_ptr<GeometryTileLayer> sourceLayer_)
        : sourceLayer(std::move(sourceLayer_)),
          zoom(static_cast<float>(parameters.tileID.overscaledZ)),
          canonical(parameters.tileID.canonical) {
        assert(!group.empty());
        auto leaderLayerProperties = staticImmutableCast<style::TerrainLineLayerProperties>(group.front());
        sourceLayerID = leaderLayerProperties->layerImpl().sourceLayer;
        bucketLeaderID = leaderLayerProperties->layerImpl().id;

        for (const auto& layerProperties : group) {
            const std::string& layerId = layerProperties->baseImpl->id;
            layerPropertiesMap.emplace(layerId, layerProperties);
        }

        const size_t featureCount = sourceLayer->featureCount();
        for (size_t i = 0; i < featureCount; ++i) {
            auto feature = sourceLayer->getFeature(i);
            if (!leaderLayerProperties->layerImpl().filter(
                    style::expression::EvaluationContext(zoom, feature.get()).withCanonicalTileID(&canonical))) {
                continue;
            }
            features.push_back(std::move(feature));
        }
    }

    bool hasDependencies() const override { return false; }

    void createBucket(const ImagePositions&,
                      std::unique_ptr<FeatureIndex>& featureIndex,
                      mln::unordered_map<std::string, LayerRenderData>& renderData,
                      const bool,
                      const bool,
                      const CanonicalTileID& canonicalOut) override {
        auto bucket = std::make_shared<TerrainLineBucket>();

        // Metres spanned by one tile-local EXTENT unit at this tile's own latitude - the exact
        // formula contours3d.js's tileMetres() uses (EQUATOR_M * cos(lat) / 2^z), divided by
        // EXTENT to go from "metres across the whole tile" to "metres per EXTENT unit". Used only
        // to turn the fixed 12 m ground densify step (STEP_M) into EXTENT units below; it has no
        // bearing on a_dist itself, which accumulates directly in EXTENT-unit coordinate deltas
        // and needs no metric conversion at all.
        const double n = std::exp2(static_cast<double>(canonical.z));
        const double lat = std::atan(std::sinh(M_PI - 2.0 * M_PI * (canonical.y + 0.5) / n));
        constexpr double equatorM = 40075016.686;
        const double metresPerExtentUnit = (equatorM * std::cos(lat) / n) / util::EXTENT;
        const double stepExtent = metresPerExtentUnit > 0.0 ? (kStepM / metresPerExtentUnit)
                                                            : static_cast<double>(util::EXTENT);

        for (std::size_t i = 0; i < features.size(); ++i) {
            const std::unique_ptr<GeometryTileFeature>& feature = features[i];
            const GeometryCollection& geometries = feature->getGeometries();
            addLine(*bucket, geometries, stepExtent);
            bucket->addFeature(*feature, geometries, {}, PatternLayerMap(), i, canonicalOut);
            featureIndex->insert(geometries, i, sourceLayerID, bucketLeaderID);
        }

        if (!bucket->hasData()) {
            return;
        }

        for (const auto& pair : layerPropertiesMap) {
            renderData.emplace(pair.first, LayerRenderData{.bucket = bucket, .layerProperties = pair.second});
        }
    }

private:
    // routes3d.js:59 (STEP_M) and :75 (MAX_STEPS_PER_SEGMENT), quoted in
    // docs/plans/grounding/ground-web-engine.md.
    static constexpr double kStepM = 12.0;
    static constexpr int kMaxStepsPerSegment = 200;

    void addLine(TerrainLineBucket& bucket, const GeometryCollection& geometry, double stepExtent) {
        auto& segments = bucket.segments;
        auto& vertices = bucket.vertices;
        auto& triangles = bucket.triangles;

        for (const auto& line : geometry) {
            if (line.size() < 2) {
                continue;
            }

            // Accumulated along the ORIGINAL (un-densified) line, in this tile's EXTENT units -
            // so the dash pattern does not restart at every 12 m densified sub-segment.
            double distanceSoFar = 0.0;

            for (std::size_t i = 0; i + 1 < line.size(); ++i) {
                const auto& a = line[i];
                const auto& b = line[i + 1];
                const double dx = static_cast<double>(b.x) - static_cast<double>(a.x);
                const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
                const double segLen = std::sqrt(dx * dx + dy * dy);
                if (segLen < 1e-6) {
                    continue;
                }

                // Densify to kStepM on the ground, capped at kMaxStepsPerSegment: beyond that,
                // use a coarser step for just this segment rather than more steps (a very long
                // segment inside one tile - e.g. an unusually simplified way - would otherwise
                // blow up the vertex count).
                int steps = stepExtent > 0.0 ? static_cast<int>(std::ceil(segLen / stepExtent)) : 1;
                steps = std::max(steps, 1);
                if (steps > kMaxStepsPerSegment) {
                    steps = kMaxStepsPerSegment;
                }

                Point<int16_t> prev{static_cast<int16_t>(a.x), static_cast<int16_t>(a.y)};
                const double subLen = segLen / steps;
                for (int s = 1; s <= steps; ++s) {
                    const double t = static_cast<double>(s) / steps;
                    const Point<int16_t> next{static_cast<int16_t>(std::lround(a.x + dx * t)),
                                              static_cast<int16_t>(std::lround(a.y + dy * t))};

                    if (segments.empty() || segments.back().vertexLength + 4 > std::numeric_limits<uint16_t>::max()) {
                        segments.emplace_back(vertices.elements(), triangles.elements(), 0ul, 0ul);
                    }
                    auto& segment = segments.back();
                    const auto index = static_cast<uint16_t>(segment.vertexLength);

                    // Four vertices per sub-segment - start-left, start-right, end-left,
                    // end-right - each carrying its own position, the OTHER end's position (both
                    // elevated independently in the vertex shader via get_elevation()), a flag
                    // pair (x = direction sign, +1 at the start vertex / -1 at the end vertex; y
                    // = side, -1/+1) and the accumulated distance. No joins between sub-segments -
                    // the vertex shader produces square caps at every one.
                    const auto d0 = static_cast<float>(distanceSoFar);
                    const auto d1 = static_cast<float>(distanceSoFar + subLen);
                    vertices.emplace_back(TerrainLineBucket::layoutVertex(prev, next, 1, -1, d0));
                    vertices.emplace_back(TerrainLineBucket::layoutVertex(prev, next, 1, 1, d0));
                    vertices.emplace_back(TerrainLineBucket::layoutVertex(next, prev, -1, -1, d1));
                    vertices.emplace_back(TerrainLineBucket::layoutVertex(next, prev, -1, 1, d1));

                    triangles.emplace_back(index, index + 1, index + 2);
                    triangles.emplace_back(index + 1, index + 3, index + 2);

                    segment.vertexLength += 4;
                    segment.indexLength += 6;

                    distanceSoFar += subLen;
                    prev = next;
                }
            }
        }
    }

    std::map<std::string, Immutable<style::LayerProperties>> layerPropertiesMap;
    std::string bucketLeaderID;
    std::string sourceLayerID;

    const std::unique_ptr<GeometryTileLayer> sourceLayer;
    std::vector<std::unique_ptr<GeometryTileFeature>> features;

    const float zoom;
    const CanonicalTileID canonical;
};

} // namespace mln
