#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"

namespace Luma::Import {

enum class NodeKind : u8 { File, Mesh, Material, Texture, Sound };

using NodeId = u32;
inline constexpr NodeId kNone = static_cast<NodeId>(-1);

struct TextureRef {
    NodeId texture = kNone;
};

struct MaterialDesc {
    Math::Vec4 baseColor{1, 1, 1, 1};
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    f32 specular = 0.5f;
    Math::Vec3 emissive{0, 0, 0};
    f32 opacityMaskClip = 0.0f;
    bool twoSided = false;
    TextureRef baseColorTex;
    TextureRef normalTex;
    TextureRef metallicRoughnessTex;  // G = roughness, B = metal
    TextureRef occlusionTex;
    TextureRef emissiveTex;
    TextureRef opacityTex;
};

struct ImportNode {
    NodeKind kind{};
    std::string name;
    std::filesystem::path sourceFile;
    NodeId parent = kNone;
    bool enabled = true;
    std::string error;
    std::vector<NodeId> meshMaterials;
    u32 meshVertexCount = 0;
    u32 meshTriangleCount = 0;
    MaterialDesc material;
    std::filesystem::path textureFile;
    std::vector<u8> textureEmbedded;
    std::string textureEmbeddedExt;
    bool textureIsNormalMap = false;
    bool textureIsLinear = false;
    u32 soundChannels = 0;
    u32 soundSampleRate = 0;
    f64 soundSeconds = 0.0;
};

struct ImportScene {
    std::vector<ImportNode> nodes;

    NodeId AddNode(ImportNode n);
    std::vector<NodeId> Children(NodeId file) const;
    std::vector<NodeId> OfKind(NodeKind k) const;
    void Append(const ImportScene& other);
};

}  // namespace Luma::Import
