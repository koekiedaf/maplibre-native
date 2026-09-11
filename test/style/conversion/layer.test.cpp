#include <mln/test/util.hpp>

#include <mln/style/conversion/filter.hpp>
#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion/layer.hpp>
#include <mln/style/layers/background_layer_impl.hpp>
#include <mln/style/layers/terrain_line_layer.hpp>

#include <rapidjson/prettywriter.h>

using namespace mln;
using namespace mln::style;
using namespace mln::style::conversion;
using namespace std::literals::chrono_literals;

std::unique_ptr<Layer> parseLayer(const std::string& src) {
    Error error;
    auto layer = convertJSON<std::unique_ptr<Layer>>(src, error);
    if (layer) return std::move(*layer);
    return nullptr;
}

Filter parseFilter(const std::string& expression) {
    Error error;
    return *convertJSON<Filter>(expression, error);
}

std::string stringifyLayer(const Value& value) {
    rapidjson::StringBuffer s;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
    writer.SetIndent(' ', 2u);
    stringify(writer, value);
    return s.GetString();
}

TEST(StyleConversion, LayerTransition) {
    auto layer = parseLayer(R"JSON({
        "type": "background",
        "id": "background",
        "paint": {
            "background-color-transition": {
                "duration": 400,
                "delay": 500
            }
        }
    })JSON");
    ASSERT_STREQ("background", layer->getTypeInfo()->type);
    ASSERT_EQ(400ms, *static_cast<BackgroundLayer*>(layer.get())->impl().paint.get<BackgroundColor>().options.duration);
    ASSERT_EQ(500ms, *static_cast<BackgroundLayer*>(layer.get())->impl().paint.get<BackgroundColor>().options.delay);
}

TEST(StyleConversion, SerializeDefaults) {
    auto layer = parseLayer(R"JSON({
        "type": "background",
        "id": "background"
    })JSON");

    EXPECT_NE(nullptr, layer);
    auto value = layer->serialize();
    EXPECT_NE(nullptr, value.getObject());
    EXPECT_EQ(2u, value.getObject()->size());
}

TEST(StyleConversion, RoundtripWithTransitions) {
    auto layer = parseLayer(R"JSON({
        "type": "background",
        "id": "background",
        "paint": {
            "background-color-transition": {
                "duration": 400,
                "delay": 500
            }
        }
    })JSON");

    EXPECT_NE(nullptr, layer);
    auto value = layer->serialize();
    EXPECT_NE(nullptr, value.getObject());
    EXPECT_EQ(3u, value.getObject()->size());

    auto roundTripped = parseLayer(stringifyLayer(value));
    EXPECT_NE(nullptr, roundTripped);
    auto roundTrippedValue = roundTripped->serialize();
    EXPECT_NE(nullptr, roundTrippedValue.getObject());
    EXPECT_EQ(3u, roundTrippedValue.getObject()->size());
}

TEST(StyleConversion, OverrideDefaults) {
    auto layer = parseLayer(R"JSON({
        "type": "symbol",
        "id": "symbol",
        "source": "composite",
        "source-layer": "landmarks",
        "filter": ["has", "monuments"],
        "minzoom": 12,
        "maxzoom": 18,
        "layout": {
            "visibility": "none",
            "text-field": ["format",
                            "Hello",
                            ["image", ["get", "world"]],
                            "Example", {"text-color": "rgba(128, 255, 39, 1)"}
                          ],
            "icon-image": ["image", ["get", "landmark_image"]],
            "text-size": 24
        },
        "paint": {
            "text-color": "turquoise",
            "text-color-transition": {
                "duration": 300,
                "delay": 100
            }
        }
    })JSON");

    EXPECT_NE(nullptr, layer);
    auto value = layer->serialize();
    EXPECT_NE(nullptr, value.getObject());
    const auto& object = *(value.getObject());
    EXPECT_EQ(9u, object.size());
    EXPECT_EQ(4u, object.at("layout").getObject()->size());
    EXPECT_EQ(2u, object.at("paint").getObject()->size());

    auto roundTripped = parseLayer(stringifyLayer(value));
    EXPECT_NE(nullptr, roundTripped);
    auto roundTrippedValue = roundTripped->serialize();
    const auto& roundTrippedObject = *(roundTrippedValue.getObject());
    EXPECT_NE(nullptr, roundTrippedValue.getObject());
    EXPECT_EQ(9u, roundTrippedObject.size());
    EXPECT_EQ(4u, roundTrippedObject.at("layout").getObject()->size());
    EXPECT_EQ(2u, roundTrippedObject.at("paint").getObject()->size());
}

TEST(StyleConversion, SetGenericProperties) {
    auto layer = parseLayer(R"JSON({
        "type": "symbol",
        "id": "symbol",
        "source": "composite",
        "source-layer": "landmarks",
        "filter": ["has", "monuments"],
        "minzoom": 12,
        "maxzoom": 18
    })JSON");

    ASSERT_NE(nullptr, layer);
    EXPECT_EQ(parseFilter(R"FILTER(["has", "monuments"])FILTER").serialize(), layer->getFilter().serialize());
    EXPECT_EQ(12.0f, layer->getMinZoom());
    EXPECT_EQ(18.0f, layer->getMaxZoom());
    EXPECT_EQ("landmarks", layer->getSourceLayer());

    const JSValue newComposite("composite_2");
    layer->setProperty("source", Convertible(&newComposite));
    EXPECT_EQ("composite_2", layer->getSourceID());

    const JSValue newSourceLayer("poi");
    layer->setProperty("source-layer", Convertible(&newSourceLayer));
    EXPECT_EQ("poi", layer->getSourceLayer());

    const JSValue newMinZoom(1.0f);
    layer->setProperty("minzoom", Convertible(&newMinZoom));
    EXPECT_EQ(1.0f, layer->getMinZoom());

    const JSValue newMaxZoom(22.0f);
    layer->setProperty("maxzoom", Convertible(&newMaxZoom));
    EXPECT_EQ(22.0f, layer->getMaxZoom());
}

// DuckMaps fork only, task 2.2a - style-parse proof (docs/plans/2026-09-11-engine-layer-plumbing.md
// acceptance step 3): a style JSON terrain-line layer with all nine paint properties set parses
// through the core and reads back exactly the values given.
TEST(StyleConversion, TerrainLineProperties) {
    auto layer = parseLayer(R"JSON({
        "type": "terrain-line",
        "id": "terrain-line-test",
        "source": "outdoor",
        "source-layer": "outdoor",
        "filter": ["in", ["get", "class"], ["literal", ["path", "bridleway"]]],
        "minzoom": 10,
        "maxzoom": 18,
        "layout": {
            "visibility": "visible"
        },
        "paint": {
            "terrain-line-color": "#ff00aa",
            "terrain-line-opacity": 0.75,
            "terrain-line-width": 4.5,
            "terrain-line-blur": 1.5,
            "terrain-line-dasharray": [4, 2],
            "terrain-line-offset": 3,
            "terrain-line-ghost-opacity": 0.3,
            "terrain-line-fade": 0.6,
            "terrain-line-fade-distance": 5000
        }
    })JSON");

    ASSERT_NE(nullptr, layer);
    ASSERT_STREQ("terrain-line", layer->getTypeInfo()->type);
    EXPECT_EQ(10.0f, layer->getMinZoom());
    EXPECT_EQ(18.0f, layer->getMaxZoom());
    EXPECT_EQ("outdoor", layer->getSourceLayer());
    EXPECT_EQ(parseFilter(R"FILTER(["in", ["get", "class"], ["literal", ["path", "bridleway"]]])FILTER").serialize(),
             layer->getFilter().serialize());

    auto* terrainLine = static_cast<TerrainLineLayer*>(layer.get());
    EXPECT_EQ((PropertyValue<Color>{Color{1.0f, 0.0f, 2.0f / 3.0f, 1.0f}}), terrainLine->getTerrainLineColor());
    EXPECT_EQ((PropertyValue<float>{0.75f}), terrainLine->getTerrainLineOpacity());
    EXPECT_EQ((PropertyValue<float>{4.5f}), terrainLine->getTerrainLineWidth());
    EXPECT_EQ((PropertyValue<float>{1.5f}), terrainLine->getTerrainLineBlur());
    EXPECT_EQ((PropertyValue<std::array<float, 2>>{std::array<float, 2>{{4.f, 2.f}}}),
             terrainLine->getTerrainLineDasharray());
    EXPECT_EQ((PropertyValue<float>{3.0f}), terrainLine->getTerrainLineOffset());
    EXPECT_EQ((PropertyValue<float>{0.3f}), terrainLine->getTerrainLineGhostOpacity());
    EXPECT_EQ((PropertyValue<float>{0.6f}), terrainLine->getTerrainLineFade());
    EXPECT_EQ((PropertyValue<float>{5000.0f}), terrainLine->getTerrainLineFadeDistance());
}
