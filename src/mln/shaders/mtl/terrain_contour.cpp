#include <mln/shaders/mtl/terrain_contour.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using TerrainContourShaderSource = ShaderSource<BuiltIn::TerrainContourShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 1> TerrainContourShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Short4, terrainContourUBOCount + 0, idTerrainContourPosVertexAttribute},
};
const std::array<TextureInfo, 1> TerrainContourShaderSource::textures = {
    TextureInfo{0, idTerrainContourDEMTexture},
};

} // namespace shaders
} // namespace mln
