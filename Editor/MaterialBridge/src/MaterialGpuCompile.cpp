#include "Luma/Editor/MaterialGpuCompile.h"

#include <cstdio>

#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/MaterialCompiler/Translator.h"
#include "Luma/MaterialGraph/ExpressionRegistry.h"
#include "Luma/ShaderCompiler/ShaderCompiler.h"

namespace Luma::Editor {
namespace {

namespace MC = Luma::MaterialCompiler;

GpuSamplerFilter FilterOf(MC::SamplerState::Filter f) {
    switch (f) {
        case MC::SamplerState::Filter::Nearest: return GpuSamplerFilter::Nearest;
        case MC::SamplerState::Filter::Linear: return GpuSamplerFilter::Linear;
        default: return GpuSamplerFilter::Anisotropic;
    }
}

GpuSamplerAddress AddressOf(MC::SamplerState::Address a) {
    switch (a) {
        case MC::SamplerState::Address::Clamp: return GpuSamplerAddress::Clamp;
        case MC::SamplerState::Address::Mirror: return GpuSamplerAddress::Mirror;
        default: return GpuSamplerAddress::Wrap;
    }
}

bool Translucent(Material::BlendMode b) {
    return b == Material::BlendMode::Translucent || b == Material::BlendMode::Additive ||
           b == Material::BlendMode::Modulate;
}

}  // namespace

u64 FoldShaderKey(u64 setKey, u64 stageKey) { return (setKey ^ stageKey) * 1099511628211ull; }

GpuMaterialBuild PrepareGpuMaterial(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                                    std::span<const Material::ParameterOverride> overrides,
                                    std::optional<f32> clip, std::optional<bool> twoSided) {
    GpuMaterialBuild out;
    MC::StaticSwitchSet switches;
    for (const Material::ParameterOverride& o : overrides) {
        if (o.kind == Material::ParameterOverride::Kind::StaticSwitch) switches[o.name] = o.staticSwitch;
    }
    const MC::CompiledMaterial compiled = MC::Translator(material, library, switches).Finish();
    if (!compiled.Ok()) {
        for (const MC::CompileError& e : compiled.errors) {
            if (!out.errors.empty()) out.errors += "; ";
            const MaterialGraph::Node* node = material.graph.Find(e.node);
            const MaterialGraph::ExpressionDesc* desc =
                node ? MaterialGraph::ExpressionRegistry::Get().Find(node->type) : nullptr;
            out.errors += (desc ? desc->displayName : std::string("Material")) + " - " + e.message;
        }
        return out;
    }
    for (usize p = 0; p < compiled.passes.size(); ++p) {
        if (!compiled.passes[p]) continue;
        out.sources.push_back({static_cast<GpuPass>(p), true, compiled.passes[p]->vertex});
        out.sources.push_back({static_cast<GpuPass>(p), false, compiled.passes[p]->fragment});
    }
    u64 fp = kShaderKeySeed;
    for (const GpuShaderSource& s : out.sources) {
        for (unsigned char ch : s.source) fp = (fp ^ ch) * 1099511628211ull;
        fp = (fp ^ 0xFF) * 1099511628211ull;
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fp));
    out.fingerprint = hex;

    CompiledMaterialGpu& gpu = out.gpu;
    const Material::MaterialSettings& s = material.settings;
    gpu.blend = static_cast<GpuBlendMode>(s.blendMode);
    gpu.shading = static_cast<GpuShadingModel>(s.shadingModel);
    gpu.twoSided = twoSided.value_or(s.twoSided);
    gpu.opacityMaskClip = clip.value_or(s.opacityMaskClip);
    gpu.writesCustomData = s.shadingModel == Material::ShadingModel::Subsurface ||
                           s.shadingModel == Material::ShadingModel::ClearCoat ||
                           s.shadingModel == Material::ShadingModel::TwoSidedFoliage;
    for (const MC::MaterialOutputValue& o : compiled.outputs) {
        if (o.pin == "WorldPositionOffset" && o.code != "vec3(0.0, 0.0, 0.0)") gpu.usesWPO = true;
    }
    gpu.usesRefraction = Translucent(s.blendMode) && s.refraction != Material::RefractionMethod::None;
    gpu.ppLocation = static_cast<GpuPostProcessLocation>(s.ppLocation);
    gpu.ppPriority = s.ppPriority;

    for (const MC::TextureSlot& slot : compiled.layout.textures) {
        std::string texture = slot.defaultTexture;
        for (const Material::ParameterOverride& o : overrides) {
            if (!slot.parameter.empty() && o.name == slot.parameter &&
                o.kind == Material::ParameterOverride::Kind::Texture) {
                texture = o.texture;
            }
        }
        GpuTextureBinding binding;
        binding.slot = slot.slot;
        binding.srgb = slot.srgb;
        binding.cube = slot.cube;
        binding.sampler = GpuSamplerIndex(FilterOf(slot.sampler.filter), AddressOf(slot.sampler.address));
        gpu.textures.push_back(binding);
        out.texturePaths.push_back(std::move(texture));
    }

    const MC::ParameterValueSource collections =
        library ? MC::CollectionValues(*library) : MC::ParameterValueSource{};
    out.block.values = MC::EvaluatePreshader(compiled.preshader, compiled.layout, overrides, collections, nullptr);
    out.preshader = compiled.preshader;
    out.layout = compiled.layout;
    out.ok = true;
    return out;
}

GpuMaterialBuild BuildGpuMaterial(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                                  std::span<const Material::ParameterOverride> overrides,
                                  std::optional<f32> clip, std::optional<bool> twoSided,
                                  ShaderCompiler::Compiler& shaders, std::string_view label) {
    GpuMaterialBuild out = PrepareGpuMaterial(material, library, overrides, clip, twoSided);
    if (!out.ok) return out;
    u64 shaderKey = kShaderKeySeed;
    for (const GpuShaderSource& s : out.sources) {
        ShaderCompiler::CompileRequest request;
        request.source = s.source;
        request.stage = s.vertex ? ShaderCompiler::ShaderStage::Vertex : ShaderCompiler::ShaderStage::Fragment;
        request.name = std::string(label) + (s.vertex ? ".vert" : ".frag");
        ShaderCompiler::CompileResult result = shaders.CompileSync(request);
        if (!result.ok) {
            out.ok = false;
            out.errors = "shader compile failed: " + (result.errors.empty() ? result.log : result.errors.front().second);
            return out;
        }
        auto& pass = out.gpu.passes[static_cast<usize>(s.pass)];
        if (!pass) pass.emplace();
        (s.vertex ? pass->vertex : pass->fragment) = std::move(result.spirv);
        shaderKey = FoldShaderKey(shaderKey, result.key);
        pass->key = shaderKey;
    }
    out.gpu.shaderKey = shaderKey;
    return out;
}

}  // namespace Luma::Editor
