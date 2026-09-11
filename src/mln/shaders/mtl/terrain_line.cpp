#include <mln/shaders/mtl/terrain_line.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using TerrainLineShaderSource = ShaderSource<BuiltIn::TerrainLineShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 4> TerrainLineShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Short2, terrainLineUBOCount + 0, idTerrainLinePosVertexAttribute},
    AttributeInfo{1, gfx::AttributeDataType::Short2, terrainLineUBOCount + 0, idTerrainLineOtherVertexAttribute},
    AttributeInfo{2, gfx::AttributeDataType::Short2, terrainLineUBOCount + 0, idTerrainLineFlagVertexAttribute},
    AttributeInfo{3, gfx::AttributeDataType::Float, terrainLineUBOCount + 0, idTerrainLineDistVertexAttribute},
};
const std::array<TextureInfo, 2> TerrainLineShaderSource::textures = {
    TextureInfo{0, idTerrainLineDEMTexture},
    TextureInfo{1, idTerrainLineDepthTexture},
};

} // namespace shaders
} // namespace mln
