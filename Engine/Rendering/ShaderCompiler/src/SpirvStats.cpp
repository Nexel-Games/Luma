// Shader statistics from SPIR-V (the Material Editor's Stats panel):
// instructions inside functions, texture reads and sampler bindings.

#include "Luma/ShaderCompiler/ShaderCompiler.h"

#include <unordered_map>

namespace Luma::ShaderCompiler {
namespace {

// SPIR-V opcodes (SPIR-V spec, section 3.52).
enum Op : u32 {
    OpTypeSampler = 26,
    OpTypeSampledImage = 27,
    OpTypeArray = 28,
    OpTypeRuntimeArray = 29,
    OpTypePointer = 32,
    OpFunction = 54,
    OpFunctionParameter = 55,
    OpFunctionEnd = 56,
    OpVariable = 59,
    OpImageSampleImplicitLod = 87,
    OpImageDrefGather = 97,
    OpLabel = 248,
};
constexpr u32 kStorageUniformConstant = 0;

}  // namespace

SpirvStats Compiler::Stats(std::span<const u32> spirv) {
    SpirvStats stats;
    if (spirv.size() < 5 || spirv[0] != 0x07230203u) return stats;

    std::unordered_map<u32, u32> typeOp;       // type id -> its opcode
    std::unordered_map<u32, u32> elementType;  // array / pointer id -> element / pointee type
    bool inFunction = false;
    for (usize i = 5; i < spirv.size();) {
        const u32 words = spirv[i] >> 16;
        const u32 op = spirv[i] & 0xFFFFu;
        if (words == 0 || i + words > spirv.size()) break;  // malformed
        const u32* w = &spirv[i];
        switch (op) {
            case OpTypeSampler:
            case OpTypeSampledImage: typeOp[w[1]] = op; break;
            case OpTypeArray:
            case OpTypeRuntimeArray:
                typeOp[w[1]] = op;
                elementType[w[1]] = w[2];
                break;
            case OpTypePointer:
                typeOp[w[1]] = op;
                elementType[w[1]] = w[3];
                break;
            case OpVariable:
                if (!inFunction && words >= 4 && w[3] == kStorageUniformConstant) {
                    // Follow pointer -> arrays -> element.
                    u32 type = w[1];
                    for (int depth = 0; depth < 8 && elementType.contains(type); ++depth) type = elementType[type];
                    const auto it = typeOp.find(type);
                    if (it != typeOp.end() && (it->second == OpTypeSampler || it->second == OpTypeSampledImage)) {
                        ++stats.samplers;
                    }
                }
                break;
            case OpFunction: inFunction = true; break;
            case OpFunctionEnd: inFunction = false; break;
            default: break;
        }
        if (inFunction && op != OpFunction && op != OpFunctionParameter && op != OpLabel && op != OpVariable) {
            ++stats.instructions;
        }
        if (op >= OpImageSampleImplicitLod && op <= OpImageDrefGather) ++stats.textureSamples;
        i += words;
    }
    return stats;
}

}  // namespace Luma::ShaderCompiler
