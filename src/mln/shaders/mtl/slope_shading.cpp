#include <mln/shaders/mtl/slope_shading.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using SlopeShadingShaderSource = ShaderSource<BuiltIn::SlopeShadingShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 1> SlopeShadingShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Short4, slopeShadingUBOCount + 0, idSlopeShadingPosVertexAttribute},
};
const std::array<TextureInfo, 2> SlopeShadingShaderSource::textures = {
    TextureInfo{0, idSlopeShadingDEMTexture},
    TextureInfo{1, idSlopeShadingLutTexture},
};

} // namespace shaders
} // namespace mln
