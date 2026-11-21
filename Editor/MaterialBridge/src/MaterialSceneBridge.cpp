#include "Luma/Editor/MaterialSceneBridge.h"

#include <algorithm>
#include <array>
#include <cstring>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Luma/Asset/LumaMesh.h"
#include "Luma/Asset/TextureColorSpace.h"
#include "Luma/Core/Log.h"
#include "Luma/Mesh/MeshConversion.h"

namespace Luma::Editor {
namespace {

using Material::MaterialParameter;

constexpr std::array<std::string_view, 7> kImageExtensions = {"png", "jpg", "jpeg", "tga",
                                                             "bmp", "psd", "gif"};

// FNV-1a: stable across runs and platforms (keys only need to be unique).
u64 Hash(std::string_view text, u64 seed = 1469598103934665603ull) {
    u64 h = seed;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

u64 MaterialKey(const std::string& slotKey) {
    if (slotKey.empty()) return kDefaultMaterialKey;
    const u64 h = Hash(slotKey);
    return h == kDefaultMaterialKey ? 1 : h;
}

u64 TextureKey(const std::string& path) { return Hash(path, 0x9e3779b97f4a7c15ull); }

u64 MeshKey(std::string_view path) { return Hash(path, 0x6a09e667f3bcc909ull); }

// Inspector label for a section: the material asset's name without its
// M_/MI_ prefix ("/Game/M_Paint" -> "Paint"), the raw source name for
// non-asset names ("lambert1"), else "Element N".
std::string SlotNameFor(const std::string& materialName, u32 index) {
    if (materialName.empty()) return "Element " + std::to_string(index);
    if (materialName.front() != '/') return materialName;
    std::string stem = materialName.substr(materialName.find_last_of('/') + 1);
    for (std::string_view prefix : {std::string_view("MI_"), std::string_view("M_")}) {
        if (stem.size() > prefix.size() && stem.compare(0, prefix.size(), prefix) == 0) {
            stem.erase(0, prefix.size());
            break;
        }
    }
    return stem.empty() ? "Element " + std::to_string(index) : stem;
}

// One CPU image ready for upload.
struct LoadedImage {
    TextureUploadDesc desc;
    std::vector<u8> bytes;
};

std::optional<LoadedImage> LoadImage(const std::filesystem::path& file) {
    const std::string name = file.string();
    int w = 0, h = 0, comp = 0;
    if (!stbi_info(name.c_str(), &w, &h, &comp)) return std::nullopt;
    LoadedImage img;
    if (stbi_is_16_bit(name.c_str())) {
        const int want = comp == 1 ? 1 : 4;
        stbi_us* px = stbi_load_16(name.c_str(), &w, &h, &comp, want);
        if (!px) return std::nullopt;
        img.desc.format = want == 1 ? TexturePixelFormat::R16 : TexturePixelFormat::RGBA16;
        img.bytes.assign(reinterpret_cast<u8*>(px),
                         reinterpret_cast<u8*>(px) + static_cast<usize>(w) * h * want * 2);
        stbi_image_free(px);
    } else {
        const int want = comp <= 2 ? comp : 4;
        stbi_uc* px = stbi_load(name.c_str(), &w, &h, &comp, want);
        if (!px) return std::nullopt;
        img.desc.format = want == 1   ? TexturePixelFormat::R8
                          : want == 2 ? TexturePixelFormat::RG8
                                      : TexturePixelFormat::RGBA8;
        img.bytes.assign(px, px + static_cast<usize>(w) * h * want);
        stbi_image_free(px);
    }
    img.desc.width = static_cast<u32>(w);
    img.desc.height = static_cast<u32>(h);
    return img;
}

}  // namespace

std::string MaterialSceneBridge::SlotKey(std::string_view path) {
    if (path.empty() || path == Material::kDefaultMaterialPath) return {};
    return std::string(path);
}

void MaterialSceneBridge::Sync(const Scene& scene, SceneRenderer& renderer,
                               Material::MaterialLibrary& library) {
    // Changed texture files: drop the GPU copy; materials sampling it rebuild.
    for (const std::string& path : m_staleTextures) {
        if (auto it = m_textures.find(path); it != m_textures.end()) {
            if (it->second.handle != kInvalidHandle) renderer.ReleaseTexture(it->second.handle);
            m_textures.erase(it);
        }
        for (auto& [key, material] : m_materials) {
            if (std::find(material.textures.begin(), material.textures.end(), path) !=
                material.textures.end()) {
                material.uploaded = false;
            }
        }
    }
    m_staleTextures.clear();
    for (const std::string& path : m_staleMeshes) {
        if (auto it = m_meshes.find(path); it != m_meshes.end()) {
            if (it->second.handle != kInvalidHandle) renderer.ReleaseMesh(it->second.handle);
            m_meshes.erase(it);
        }
    }
    m_staleMeshes.clear();

    std::vector<std::string> used{std::string{}};  // the Default Material, always
    auto view = scene.Registry().view<const TransformComponent, const MeshRendererComponent>();
    for (Entity e : view) {
        const auto& mr = view.get<const MeshRendererComponent>(e);
        if (mr.UsesMeshAsset()) {
            SyncMesh(mr.mesh, renderer, library.Paths());
            // The mesh's own materials are the defaults for empty slots.
            if (const auto* sections = Sections(mr.mesh)) {
                for (const MeshSectionInfo& s : *sections) used.push_back(SlotKey(s.defaultMaterial));
            }
        }
        for (const std::string& slot : mr.materials) used.push_back(SlotKey(slot));
    }
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    for (const std::string& path : used) SyncMaterial(path, renderer, library);
}

void MaterialSceneBridge::SyncMaterial(const std::string& path, SceneRenderer& renderer,
                                       Material::MaterialLibrary& library) {
    const Material::ResolvedMaterial& resolved = library.Resolve(path);
    MaterialEntry& entry = m_materials[path];
    if (entry.uploaded && entry.version == resolved.version) return;

    MaterialGpuDesc desc;
    desc.blend = static_cast<GpuBlendMode>(resolved.settings.blendMode);
    desc.shading = static_cast<GpuShadingModel>(resolved.settings.shadingModel);
    desc.twoSided = resolved.settings.twoSided;
    desc.opacityMaskClip = resolved.settings.opacityMaskClip;
    entry.textures.clear();
    for (usize i = 0; i < Material::kMaterialParameterCount; ++i) {
        const auto param = static_cast<MaterialParameter>(i);
        const Material::MaterialInput& in = resolved.inputs[i];
        MaterialGpuDesc::Input& out = desc.inputs[i];
        out.constant = in.constant;
        out.channel = static_cast<GpuTextureChannel>(in.channel);
        out.tint = in.tint;
        out.tiling = in.uvTiling;
        out.offset = in.uvOffset;
        out.normalStrength = in.normalStrength;
        out.flipGreen = in.flipGreen;
        out.invert = in.invert;
        out.emissiveIntensity = in.emissiveIntensity;
        if (in.texture.empty()) continue;
        entry.textures.push_back(in.texture);
        const TextureEntry& tex = SyncTexture(in.texture, renderer, library.Paths());
        if (tex.handle == kInvalidHandle) continue;
        out.texture = tex.handle;
        out.view = static_cast<GpuTextureView>(Material::SelectTextureView(param, tex.srgb));
    }

    entry.handle = renderer.UpdateMaterial(MaterialKey(path), desc);
    entry.version = resolved.version;
    entry.uploaded = true;
}

const MaterialSceneBridge::TextureEntry& MaterialSceneBridge::SyncTexture(
    const std::string& path, SceneRenderer& renderer, const AssetPathResolver& paths) {
    auto [it, inserted] = m_textures.try_emplace(path);
    TextureEntry& entry = it->second;
    if (!inserted) return entry;

    const auto file = paths.ToFileAny(path, kImageExtensions);
    std::optional<LoadedImage> image;
    if (file) image = LoadImage(*file);
    if (!image) {
        entry.failed = true;
        LUMA_USER_WARN("Materials", "Texture '{}' could not be loaded; using the constant value",
                       path);
        return entry;
    }
    entry.srgb = IsTextureSRGB(*file, nullptr);
    image->desc.srgb = entry.srgb;
    image->desc.pixels = image->bytes.data();
    entry.handle = renderer.UploadTexture(TextureKey(path), image->desc);
    entry.failed = entry.handle == kInvalidHandle;
    if (entry.failed) {
        LUMA_USER_WARN("Materials",
                       "Texture '{}' ({}x{}) could not be uploaded to the GPU (out of memory or "
                       "texture slots); using the constant value",
                       path, image->desc.width, image->desc.height);
    }
    return entry;
}

void MaterialSceneBridge::SyncMesh(const std::string& path, SceneRenderer& renderer,
                                   const AssetPathResolver& paths) {
    auto [it, inserted] = m_meshes.try_emplace(path);
    if (!inserted) return;
    MeshEntry& entry = it->second;
    std::optional<LumaMeshData> source;
    if (const auto file = paths.ToFile(path, {"lmesh"})) source = LumaMeshIO::ReadMesh(*file);
    if (!source || !source->IsValid()) {
        entry.failed = true;
        LUMA_USER_WARN("Meshes", "Mesh '{}' could not be loaded; drawing its primitive instead", path);
        return;
    }
    for (u32 s = 0; s < source->submeshes.size(); ++s) {
        const std::string& name = source->submeshes[s].materialName;
        MeshSectionInfo info;
        info.slotName = SlotNameFor(name, s);
        if (!name.empty() && name.front() == '/') info.defaultMaterial = name;
        entry.sections.push_back(std::move(info));
    }
    const MeshData mesh = ConvertLumaMesh(*source);
    entry.handle = renderer.UploadMesh(MeshKey(path), mesh);
    entry.failed = entry.handle == kInvalidHandle;
    entry.sectionCount = static_cast<u32>(std::max<usize>(mesh.sections.size(), 1));
    if (!mesh.vertices.empty()) {
        entry.bounds.min = entry.bounds.max = mesh.vertices.front().position;
        for (const MeshVertex& v : mesh.vertices) {
            entry.bounds.min = {std::min(entry.bounds.min.x, v.position.x),
                                std::min(entry.bounds.min.y, v.position.y),
                                std::min(entry.bounds.min.z, v.position.z)};
            entry.bounds.max = {std::max(entry.bounds.max.x, v.position.x),
                                std::max(entry.bounds.max.y, v.position.y),
                                std::max(entry.bounds.max.z, v.position.z)};
        }
    }
}

void MaterialSceneBridge::BuildInstances(const Scene& scene, Entity selected,
                                         std::vector<SceneInstance>& out) const {
    out.clear();
    auto view = scene.Registry().view<const TransformComponent, const MeshRendererComponent>();
    for (Entity e : view) {
        const auto& mr = view.get<const MeshRendererComponent>(e);
        SceneInstance inst;
        inst.model = view.get<const TransformComponent>(e).Matrix();
        inst.primitive = mr.primitive;
        inst.selected = e == selected;
        inst.sectionCount = 1;
        const std::vector<MeshSectionInfo>* sections = nullptr;
        if (mr.UsesMeshAsset()) {
            if (auto it = m_meshes.find(mr.mesh); it != m_meshes.end() && !it->second.failed) {
                inst.mesh = it->second.handle;
                inst.sectionCount = it->second.sectionCount;
                sections = &it->second.sections;
            }
        }
        const u32 slots = std::min(inst.sectionCount, kMaxSectionMaterials);
        for (u32 s = 0; s < slots; ++s) {
            // Override > the mesh's own material > the Default Material.
            if (s < mr.materials.size() && !mr.materials[s].empty()) {
                inst.sectionMaterials[s] = HandleFor(mr.materials[s]);
            } else if (sections && s < sections->size()) {
                inst.sectionMaterials[s] = HandleFor((*sections)[s].defaultMaterial);
            } else {
                inst.sectionMaterials[s] = kDefaultMaterialHandle;
            }
        }
        out.push_back(inst);
    }
}

void MaterialSceneBridge::OnTextureFileChanged(std::string_view virtualPath) {
    m_staleTextures.emplace_back(virtualPath);
}

void MaterialSceneBridge::OnMeshFileChanged(std::string_view meshPath) {
    // Released (the renderer frees it once no frame draws it) and re-read on
    // the next Sync.
    m_staleMeshes.emplace_back(meshPath);
}

const MeshBounds* MaterialSceneBridge::Bounds(std::string_view meshPath) const {
    auto it = m_meshes.find(std::string(meshPath));
    return it != m_meshes.end() && !it->second.failed ? &it->second.bounds : nullptr;
}

const std::vector<MeshSectionInfo>* MaterialSceneBridge::Sections(std::string_view meshPath) const {
    auto it = m_meshes.find(std::string(meshPath));
    return it != m_meshes.end() && !it->second.failed ? &it->second.sections : nullptr;
}

void MaterialSceneBridge::EnsureMesh(std::string_view meshPath, SceneRenderer& renderer,
                                     const AssetPathResolver& paths) {
    if (!meshPath.empty()) SyncMesh(std::string(meshPath), renderer, paths);
}

u32 MaterialSceneBridge::SectionCount(const MeshRendererComponent& mesh) const {
    if (!mesh.UsesMeshAsset()) return 1;
    auto it = m_meshes.find(mesh.mesh);
    return it != m_meshes.end() && !it->second.failed ? it->second.sectionCount : 1u;
}

MaterialHandle MaterialSceneBridge::HandleFor(std::string_view materialPath) const {
    auto it = m_materials.find(SlotKey(materialPath));
    return it != m_materials.end() && it->second.uploaded ? it->second.handle
                                                          : kDefaultMaterialHandle;
}

void MaterialSceneBridge::Clear(SceneRenderer& renderer) {
    for (auto& [id, mesh] : m_meshes) {
        if (mesh.handle != kInvalidHandle) renderer.ReleaseMesh(mesh.handle);
    }
    for (auto& [path, tex] : m_textures) {
        if (tex.handle != kInvalidHandle) renderer.ReleaseTexture(tex.handle);
    }
    for (auto& [path, material] : m_materials) {
        if (material.uploaded && material.handle != kDefaultMaterialHandle) {
            renderer.ReleaseMaterial(material.handle);
        }
    }
    m_meshes.clear();
    m_textures.clear();
    m_materials.clear();
    m_staleTextures.clear();
    m_staleMeshes.clear();
}

}  // namespace Luma::Editor
