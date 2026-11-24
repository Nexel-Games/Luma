#include "Luma/AssetImport/Translators/ModelTranslator.h"

#include "Luma/AssetImport/Translators/AxisConversion.h"
#include "Luma/Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#pragma warning(push, 0)
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma warning(pop)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace Luma::Import {
namespace {

constexpr unsigned kBaseFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                aiProcess_SortByPType | aiProcess_ValidateDataStructure |
                                aiProcess_GenUVCoords | aiProcess_FlipUVs;

void Fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
    LUMA_USER_ERROR("Import", "{}", message);
}

std::string ExtensionOf(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

std::string CleanNodeName(const char* raw) {
    std::string name = raw != nullptr ? raw : "";
    const auto pivot = name.find("_$AssimpFbx$");
    if (pivot != std::string::npos) {
        name.resize(pivot);
    }
    return name;
}

struct MeshNode {
    std::string name;
    aiMatrix4x4 world;
    std::vector<unsigned> meshes;
};

void CollectMeshNodes(const aiNode* node, const aiMatrix4x4& parent, std::vector<MeshNode>& out) {
    if (node == nullptr) {
        return;
    }
    const aiMatrix4x4 world = parent * node->mTransformation;
    if (node->mNumMeshes > 0) {
        MeshNode item;
        item.name = CleanNodeName(node->mName.C_Str());
        item.world = world;
        item.meshes.assign(node->mMeshes, node->mMeshes + node->mNumMeshes);
        if (!item.name.empty()) {
            out.push_back(std::move(item));
        }
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) {
        CollectMeshNodes(node->mChildren[i], world, out);
    }
}

const aiScene* ReadScene(Assimp::Importer& importer, const std::filesystem::path& file,
                         const ModelPayloadOptions& options, bool forceSmoothNormals) {
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, options.smoothingAngleDeg);

    unsigned flags = kBaseFlags;
    if (!options.importNormals || forceSmoothNormals) {
        importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_NORMALS);
        flags |= aiProcess_RemoveComponent | aiProcess_GenSmoothNormals;
    }
    if (options.computeTangents) {
        flags |= aiProcess_CalcTangentSpace;
    }
    return importer.ReadFile(file.string(), flags);
}

const aiScene* ReadForPayload(Assimp::Importer& importer, const std::filesystem::path& file,
                              const ModelPayloadOptions& options, std::string* error) {
    const aiScene* scene = ReadScene(importer, file, options, false);
    if (scene == nullptr || scene->mRootNode == nullptr) {
        Fail(error, "Failed to read '" + file.filename().string() + "': " + importer.GetErrorString());
        return nullptr;
    }
    if (options.importNormals) {
        bool missing = false;
        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            if (scene->mMeshes[i] != nullptr && !scene->mMeshes[i]->HasNormals()) {
                missing = true;
                break;
            }
        }
        if (missing) {
            scene = ReadScene(importer, file, options, true);
            if (scene == nullptr || scene->mRootNode == nullptr) {
                Fail(error, "Failed to read '" + file.filename().string() + "': " +
                                importer.GetErrorString());
                return nullptr;
            }
        }
    }
    return scene;
}

Math::Vec3 TransformPoint(const Math::Mat4& m, Math::Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

Math::Vec3 TransformVector(const Math::Mat4& m, Math::Vec3 v) {
    return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}

Math::Vec3 TransformNormal(const Math::Mat4& m, Math::Vec3 n) {
    const f32 a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    const f32 a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    const f32 a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    const f32 c00 = a11 * a22 - a12 * a21;
    const f32 c01 = a12 * a20 - a10 * a22;
    const f32 c02 = a10 * a21 - a11 * a20;
    const f32 c10 = a02 * a21 - a01 * a22;
    const f32 c11 = a00 * a22 - a02 * a20;
    const f32 c12 = a01 * a20 - a00 * a21;
    const f32 c20 = a01 * a12 - a02 * a11;
    const f32 c21 = a02 * a10 - a00 * a12;
    const f32 c22 = a00 * a11 - a01 * a10;
    const f32 det = a00 * c00 + a01 * c01 + a02 * c02;
    if (std::fabs(det) < 1.0e-12f) {
        return n;
    }
    return Math::Normalize({c00 * n.x + c01 * n.y + c02 * n.z, c10 * n.x + c11 * n.y + c12 * n.z,
                            c20 * n.x + c21 * n.y + c22 * n.z});
}

SourceAxes AxesFor(const aiScene& scene, ModelPayloadOptions::AxisOverride overrideAxis) {
    SourceAxes axes = AxesFromAssimpMetadata(scene);
    if (overrideAxis == ModelPayloadOptions::AxisOverride::YUp) {
        axes.up = 1;
        axes.upSign = 1;
        axes.front = 2;
        axes.frontSign = 1;
        axes.coord = 0;
        axes.coordSign = 1;
    } else if (overrideAxis == ModelPayloadOptions::AxisOverride::ZUp) {
        axes.up = 2;
        axes.upSign = 1;
        axes.front = 1;
        axes.frontSign = -1;
        axes.coord = 0;
        axes.coordSign = 1;
    }
    return axes;
}

Math::Mat4 UserTransform(const ModelPayloadOptions& options) {
    const Math::Vec3& r = options.offsetRotationDeg;
    return Math::Translate(options.offsetTranslation) * Math::RotateZ(Math::Radians(r.z)) *
           Math::RotateY(Math::Radians(r.y)) * Math::RotateX(Math::Radians(r.x)) *
           Math::Scale({options.uniformScale, options.uniformScale, options.uniformScale});
}

void ApplyPivot(Luma::LumaMeshData& mesh, ModelPayloadOptions::Pivot pivot) {
    if (pivot == ModelPayloadOptions::Pivot::Source || mesh.vertices.empty()) {
        mesh.ComputeBounds();
        return;
    }
    mesh.ComputeBounds();
    const Math::Vec3 shift = pivot == ModelPayloadOptions::Pivot::BoundsCenter
                                 ? mesh.bounds.center
                                 : Math::Vec3{mesh.bounds.center.x, mesh.bounds.min.y,
                                              mesh.bounds.center.z};
    for (Luma::LumaVertex& vertex : mesh.vertices) {
        vertex.position = vertex.position - shift;
    }
    mesh.ComputeBounds();
}

std::string FormatHint(const aiTexture& tex) {
    std::string hint;
    for (int i = 0; i < 8 && tex.achFormatHint[i] != '\0'; ++i) {
        hint.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tex.achFormatHint[i]))));
    }
    return hint;
}

void WritePng(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<u8>*>(context);
    const auto* bytes = static_cast<const u8*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

struct PendingTexture {
    std::string key;
    std::string name;
    std::filesystem::path file;
    std::vector<u8> embedded;
    std::string ext;
    std::string error;
    bool isNormal = false;
    bool isLinear = false;
};

PendingTexture ResolveTexture(const aiScene& scene, const std::filesystem::path& source,
                              const aiString& path) {
    PendingTexture pending;
    const auto embedded = scene.GetEmbeddedTextureAndIndex(path.C_Str());
    if (embedded.first != nullptr) {
        const aiTexture& tex = *embedded.first;
        pending.key = "embedded:" + std::to_string(embedded.second);
        pending.name = CleanNodeName(tex.mFilename.C_Str());
        if (pending.name.empty()) {
            pending.name = "Texture_" + std::to_string(embedded.second);
        }
        if (tex.mHeight == 0) {
            const auto* bytes = reinterpret_cast<const u8*>(tex.pcData);
            pending.embedded.assign(bytes, bytes + tex.mWidth);
            pending.ext = FormatHint(tex);
            if (pending.ext.empty()) {
                pending.ext = "png";
            }
        } else {
            std::vector<u8> rgba(static_cast<usize>(tex.mWidth) * tex.mHeight * 4);
            for (unsigned i = 0; i < tex.mWidth * tex.mHeight; ++i) {
                rgba[i * 4 + 0] = tex.pcData[i].r;
                rgba[i * 4 + 1] = tex.pcData[i].g;
                rgba[i * 4 + 2] = tex.pcData[i].b;
                rgba[i * 4 + 3] = tex.pcData[i].a;
            }
            stbi_write_png_to_func(WritePng, &pending.embedded, static_cast<int>(tex.mWidth),
                                   static_cast<int>(tex.mHeight), 4, rgba.data(),
                                   static_cast<int>(tex.mWidth * 4));
            pending.ext = "png";
        }
        return pending;
    }

    const std::filesystem::path relative(path.C_Str());
    const std::filesystem::path fileName = relative.filename();
    const std::filesystem::path folder = source.parent_path();
    const std::filesystem::path candidates[] = {
        folder / relative,
        folder / fileName,
        folder / "textures" / fileName,
        folder / "Textures" / fileName,
        folder / "images" / fileName,
    };
    pending.name = fileName.stem().string();
    if (pending.name.empty()) {
        pending.name = relative.stem().string();
    }
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec) {
            pending.file = candidate;
            pending.key = std::filesystem::weakly_canonical(candidate, ec).string();
            if (pending.key.empty()) {
                pending.key = candidate.string();
            }
            return pending;
        }
    }
    pending.key = "missing:" + (folder / relative).string();
    pending.error = "Texture '" + pending.name + "' not found next to " + source.filename().string();
    // Shown in the Import window and reported as a warning by the import.
    LUMA_LOG_INFO("Import", "{}", pending.error);
    return pending;
}

int FindTexture(std::vector<PendingTexture>& textures, const PendingTexture& incoming) {
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        if (textures[static_cast<usize>(i)].key == incoming.key) {
            textures[static_cast<usize>(i)].isNormal =
                textures[static_cast<usize>(i)].isNormal || incoming.isNormal;
            textures[static_cast<usize>(i)].isLinear =
                textures[static_cast<usize>(i)].isLinear || incoming.isLinear;
            return i;
        }
    }
    textures.push_back(incoming);
    return static_cast<int>(textures.size() - 1);
}

int SlotTexture(const aiScene& scene, const std::filesystem::path& source, const aiMaterial& mat,
                const aiTextureType* types, int typeCount, bool isNormal, bool isLinear,
                std::vector<PendingTexture>& textures) {
    aiString path;
    for (int i = 0; i < typeCount; ++i) {
        if (mat.GetTexture(types[i], 0, &path) == AI_SUCCESS && path.length > 0) {
            PendingTexture pending = ResolveTexture(scene, source, path);
            pending.isNormal = isNormal;
            pending.isLinear = isLinear || isNormal;
            return FindTexture(textures, pending);
        }
    }
    return -1;
}

float Luminance(const aiColor4D& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

MaterialDesc ReadMaterial(const aiMaterial& mat, int baseColor, int normal, int metallicRoughness,
                          int occlusion, int emissive, int opacity, const std::vector<NodeId>& textureIds) {
    MaterialDesc desc;
    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (mat.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS) {
        mat.Get(AI_MATKEY_COLOR_DIFFUSE, color);
    }
    desc.baseColor = {color.r, color.g, color.b, color.a};

    const bool hasRoughness = mat.Get(AI_MATKEY_ROUGHNESS_FACTOR, desc.roughness) == AI_SUCCESS;
    if (!hasRoughness) {
        float shininess = 0.0f;
        if (mat.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
            // OBJ/FBX legacy: Ns → roughness = sqrt(2 / (Ns + 2)), clamped to [0.04, 1].
            desc.roughness = std::sqrt(2.0f / (shininess + 2.0f));
            desc.roughness = std::clamp(desc.roughness, 0.04f, 1.0f);
        }
    }

    const bool hasMetallic = mat.Get(AI_MATKEY_METALLIC_FACTOR, desc.metallic) == AI_SUCCESS;
    if (!hasMetallic) {
        aiColor4D specular(0.0f, 0.0f, 0.0f, 1.0f);
        if (mat.Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS && Luminance(specular) > 0.5f &&
            baseColor < 0) {
            // Ks luminance above 0.5 and no diffuse texture → metallic.
            desc.metallic = 1.0f;
        }
    }

    aiColor4D emissiveColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (mat.Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
        desc.emissive = {emissiveColor.r, emissiveColor.g, emissiveColor.b};
    }
    int twoSided = 0;
    if (mat.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        desc.twoSided = twoSided != 0;
    }
    float opacityValue = 1.0f;
    if (mat.Get(AI_MATKEY_OPACITY, opacityValue) == AI_SUCCESS && opacityValue < 1.0f) {
        desc.opacityMaskClip = 0.333f;
    }

    const auto ref = [&](int index) {
        TextureRef result;
        if (index >= 0) {
            result.texture = textureIds[static_cast<usize>(index)];
        }
        return result;
    };
    desc.baseColorTex = ref(baseColor);
    desc.normalTex = ref(normal);
    desc.metallicRoughnessTex = ref(metallicRoughness);
    desc.occlusionTex = ref(occlusion);
    desc.emissiveTex = ref(emissive);
    desc.opacityTex = ref(opacity);
    return desc;
}

void AppendMesh(const aiScene& scene, const MeshNode& node, unsigned meshIndex, const Math::Mat4& xform,
                bool flipUVs, Luma::LumaMeshData& out) {
    const aiMesh* mesh = scene.mMeshes[meshIndex];
    if (mesh == nullptr) {
        return;
    }
    Luma::LumaSubmesh section;
    section.vertexOffset = static_cast<u32>(out.vertices.size());
    section.indexOffset = static_cast<u32>(out.indices.size());
    section.vertexCount = mesh->mNumVertices;
    if (mesh->mMaterialIndex < scene.mNumMaterials && scene.mMaterials[mesh->mMaterialIndex] != nullptr) {
        section.materialName = scene.mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
    }

    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        const aiVector3D local = mesh->mVertices[v];
        const aiVector3D world = node.world * local;
        Luma::LumaVertex vertex;
        vertex.position = TransformPoint(xform, {world.x, world.y, world.z});

        Math::Vec3 normal{0.0f, 1.0f, 0.0f};
        if (mesh->HasNormals()) {
            normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};
        }
        Math::Vec3 tangent{1.0f, 0.0f, 0.0f};
        Math::Vec3 bitangent{0.0f, 0.0f, 1.0f};
        const bool hasTangent = mesh->HasTangentsAndBitangents();
        if (hasTangent) {
            tangent = {mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z};
            bitangent = {mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z};
        }
        const Math::Vec3 normalOut = TransformNormal(xform, normal);
        const Math::Vec3 tangentOut = Math::Normalize(TransformVector(xform, tangent));
        f32 handedness = 1.0f;
        if (hasTangent) {
            const Math::Vec3 bitangentOut = TransformVector(xform, bitangent);
            if (Math::Dot(Math::Cross(normalOut, tangentOut), bitangentOut) < 0.0f) {
                handedness = -1.0f;
            }
        }
        vertex.normal = normalOut;
        vertex.tangent = tangentOut;
        vertex.bitangent = Math::Cross(normalOut, tangentOut) * handedness;

        vertex.texCoord = {0.0f, 0.0f};
        if (mesh->HasTextureCoords(0)) {
            vertex.texCoord.x = mesh->mTextureCoords[0][v].x;
            vertex.texCoord.y = mesh->mTextureCoords[0][v].y;
            if (flipUVs) {
                vertex.texCoord.y = 1.0f - vertex.texCoord.y;
            }
        }
        vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
        if (mesh->HasVertexColors(0)) {
            vertex.color = {mesh->mColors[0][v].r, mesh->mColors[0][v].g, mesh->mColors[0][v].b,
                            mesh->mColors[0][v].a};
        }
        out.vertices.push_back(vertex);
    }

    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) {
            continue;
        }
        for (unsigned i = 0; i < 3; ++i) {
            out.indices.push_back(face.mIndices[i] + section.vertexOffset);
        }
    }
    section.indexCount = static_cast<u32>(out.indices.size() - section.indexOffset);
    out.submeshes.push_back(section);
}

}  // namespace

std::string_view ModelTranslator::Name() const {
    return "Luma.Import.Model";
}

std::vector<std::string> ModelTranslator::Extensions() const {
    return {".gltf", ".glb", ".fbx", ".obj"};
}

bool ModelTranslator::Translate(const std::filesystem::path& file, ImportScene& out, std::string* error) {
    if (error != nullptr) {
        *error = {};
    }
    if (!CanTranslate(file)) {
        Fail(error, "Unsupported model '" + file.filename().string() + "'");
        return false;
    }

    ModelPayloadOptions describe;
    describe.computeTangents = false;
    Assimp::Importer importer;
    const aiScene* scene = ReadForPayload(importer, file, describe, error);
    if (scene == nullptr) {
        return false;
    }

    ImportNode fileNode;
    fileNode.kind = NodeKind::File;
    fileNode.name = file.filename().string();
    fileNode.sourceFile = file;
    const NodeId fileId = out.AddNode(std::move(fileNode));

    std::vector<bool> materialUsed(scene->mNumMaterials, false);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (mesh != nullptr && mesh->mMaterialIndex < scene->mNumMaterials) {
            materialUsed[mesh->mMaterialIndex] = true;
        }
    }

    std::vector<PendingTexture> textures;
    struct Slots {
        int base = -1;
        int normal = -1;
        int metallicRoughness = -1;
        int occlusion = -1;
        int emissive = -1;
        int opacity = -1;
    };
    std::vector<Slots> slots(scene->mNumMaterials);
    const aiTextureType baseTypes[] = {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE};
    const aiTextureType normalTypes[] = {aiTextureType_NORMALS, aiTextureType_HEIGHT};
    const aiTextureType mrTypes[] = {aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_UNKNOWN};
    const aiTextureType occlusionTypes[] = {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP};
    const aiTextureType emissiveTypes[] = {aiTextureType_EMISSIVE};
    const aiTextureType opacityTypes[] = {aiTextureType_OPACITY};

    for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
        const aiMaterial* mat = scene->mMaterials[m];
        if (mat == nullptr || !materialUsed[m]) {
            continue;
        }
        Slots& slot = slots[m];
        slot.base = SlotTexture(*scene, file, *mat, baseTypes, 2, false, false, textures);
        slot.normal = SlotTexture(*scene, file, *mat, normalTypes, 2, true, true, textures);
        slot.metallicRoughness = SlotTexture(*scene, file, *mat, mrTypes, 2, false, true, textures);
        slot.occlusion = SlotTexture(*scene, file, *mat, occlusionTypes, 2, false, true, textures);
        slot.emissive = SlotTexture(*scene, file, *mat, emissiveTypes, 1, false, false, textures);
        slot.opacity = SlotTexture(*scene, file, *mat, opacityTypes, 1, false, true, textures);
        if (slot.metallicRoughness >= 0 && slot.metallicRoughness == slot.base) {
            slot.metallicRoughness = -1;
        }
    }

    std::vector<NodeId> textureIds;
    textureIds.reserve(textures.size());
    for (PendingTexture& texture : textures) {
        ImportNode node;
        node.kind = NodeKind::Texture;
        node.name = texture.name;
        node.sourceFile = file;
        node.parent = fileId;
        node.textureFile = texture.file;
        node.textureEmbedded = std::move(texture.embedded);
        node.textureEmbeddedExt = std::move(texture.ext);
        node.textureIsNormalMap = texture.isNormal;
        node.textureIsLinear = texture.isLinear;
        node.error = std::move(texture.error);
        textureIds.push_back(out.AddNode(std::move(node)));
    }

    std::vector<NodeId> materialIds(scene->mNumMaterials, kNone);
    for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
        const aiMaterial* mat = scene->mMaterials[m];
        if (mat == nullptr || !materialUsed[m]) {
            continue;
        }
        const Slots& slot = slots[m];
        ImportNode node;
        node.kind = NodeKind::Material;
        node.name = mat->GetName().C_Str();
        if (node.name.empty()) {
            node.name = "Material_" + std::to_string(m);
        }
        node.sourceFile = file;
        node.parent = fileId;
        node.material = ReadMaterial(*mat, slot.base, slot.normal, slot.metallicRoughness, slot.occlusion,
                                     slot.emissive, slot.opacity, textureIds);
        materialIds[m] = out.AddNode(std::move(node));
    }

    std::vector<MeshNode> meshes;
    CollectMeshNodes(scene->mRootNode, aiMatrix4x4(), meshes);
    for (const MeshNode& meshNode : meshes) {
        ImportNode node;
        node.kind = NodeKind::Mesh;
        node.name = meshNode.name;
        node.sourceFile = file;
        node.parent = fileId;
        for (unsigned index : meshNode.meshes) {
            const aiMesh* mesh = scene->mMeshes[index];
            if (mesh == nullptr) {
                continue;
            }
            node.meshVertexCount += mesh->mNumVertices;
            for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
                if (mesh->mFaces[f].mNumIndices == 3) {
                    ++node.meshTriangleCount;
                }
            }
            if (mesh->mMaterialIndex < materialIds.size() && materialIds[mesh->mMaterialIndex] != kNone) {
                node.meshMaterials.push_back(materialIds[mesh->mMaterialIndex]);
            }
        }
        out.AddNode(std::move(node));
    }

    LUMA_LOG_INFO("Import", "Translated {} ({} meshes, {} materials, {} textures)", file.filename().string(),
                  meshes.size(), scene->mNumMaterials, textures.size());
    return true;
}

struct ModelPayloadCache::Entry {
    std::string path;
    bool importNormals = true;
    f32 smoothingAngleDeg = 0.0f;
    bool computeTangents = true;
    Assimp::Importer importer;  // owns `scene`
    const aiScene* scene = nullptr;

    // Only the options that change how Assimp reads the file; scale, offsets,
    // pivot, axis, UV flip and combining are applied afterwards.
    bool Matches(const std::string& key, const ModelPayloadOptions& o) const {
        return path == key && importNormals == o.importNormals && smoothingAngleDeg == o.smoothingAngleDeg &&
               computeTangents == o.computeTangents;
    }
};

ModelPayloadCache::ModelPayloadCache() = default;
ModelPayloadCache::~ModelPayloadCache() = default;

std::vector<Luma::LumaMeshData> ModelTranslator::BuildMeshes(const std::filesystem::path& file,
                                                            const std::vector<std::string>& meshNodeNames,
                                                            const ModelPayloadOptions& options,
                                                            std::string* error,
                                                            ModelPayloadCache* cache) {
    if (error != nullptr) {
        *error = {};
    }
    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    if (cache != nullptr) {
        const std::string key = file.lexically_normal().generic_string();
        if (cache->m_entry == nullptr || !cache->m_entry->Matches(key, options)) {
            cache->m_entry.reset();  // release the previous model before reading the next
            auto entry = std::make_unique<ModelPayloadCache::Entry>();
            entry->scene = ReadForPayload(entry->importer, file, options, error);
            if (entry->scene == nullptr) {
                return {};
            }
            entry->path = key;
            entry->importNormals = options.importNormals;
            entry->smoothingAngleDeg = options.smoothingAngleDeg;
            entry->computeTangents = options.computeTangents;
            ++cache->m_reads;
            cache->m_entry = std::move(entry);
        }
        scene = cache->m_entry->scene;
    } else {
        scene = ReadForPayload(importer, file, options, error);
    }
    if (scene == nullptr) {
        return {};
    }

    std::vector<MeshNode> meshes;
    CollectMeshNodes(scene->mRootNode, aiMatrix4x4(), meshes);
    std::unordered_map<std::string, usize> byName;
    for (usize i = 0; i < meshes.size(); ++i) {
        byName.emplace(meshes[i].name, i);
    }

    std::vector<const MeshNode*> selected;
    selected.reserve(meshNodeNames.size());
    for (const std::string& name : meshNodeNames) {
        const auto found = byName.find(name);
        if (found == byName.end()) {
            Fail(error, "Mesh node '" + name + "' was not found in " + file.filename().string());
            return {};
        }
        selected.push_back(&meshes[found->second]);
    }

    const Math::Mat4 xform = UserTransform(options) * SourceToLuma(AxesFor(*scene, options.axis));
    std::vector<Luma::LumaMeshData> result;
    if (options.combineMeshes) {
        Luma::LumaMeshData combined;
        combined.name = meshNodeNames.size() == 1 ? meshNodeNames[0] : file.stem().string();
        combined.sourceFile = file.string();
        for (const MeshNode* node : selected) {
            for (unsigned index : node->meshes) {
                AppendMesh(*scene, *node, index, xform, options.flipUVs, combined);
            }
        }
        ApplyPivot(combined, options.pivot);
        result.push_back(std::move(combined));
        return result;
    }

    for (const MeshNode* node : selected) {
        Luma::LumaMeshData mesh;
        mesh.name = node->name;
        mesh.sourceFile = file.string();
        for (unsigned index : node->meshes) {
            AppendMesh(*scene, *node, index, xform, options.flipUVs, mesh);
        }
        ApplyPivot(mesh, options.pivot);
        result.push_back(std::move(mesh));
    }
    return result;
}

}  // namespace Luma::Import
