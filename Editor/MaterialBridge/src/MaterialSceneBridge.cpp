#include "Luma/Editor/MaterialSceneBridge.h"

#include <atomic>
#include <cstdio>

#include "Luma/Jobs/JobSystem.h"
#include "Luma/Mesh/MeshLodCache.h"
#include "Luma/Mesh/MeshSimplify.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

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
#include "Luma/Editor/MaterialGpuCompile.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/Mesh/MeshConversion.h"
#include "Luma/ShaderCompiler/ShaderCompiler.h"

namespace Luma::Editor {
namespace {


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

struct MaterialSceneBridge::LodBuild {
    std::string path;          // mesh virtual path
    MeshHandle handle = kInvalidHandle;
    std::atomic<bool> done{false};
    std::vector<MeshLodData> lods;
    bool fromCache = false;
};

void MaterialSceneBridge::EnableMeshLods(Jobs::JobSystem* jobs, std::filesystem::path cacheDir) {
    m_lodJobs = jobs;
    m_lodCacheDir = std::move(cacheDir);
}

void MaterialSceneBridge::PollLodBuilds(SceneRenderer& renderer) {
    std::erase_if(m_lodBuilds, [&](const std::shared_ptr<LodBuild>& build) {
        if (!build->done.load(std::memory_order_acquire)) return false;
        // Applies only if the mesh is still the one the build started from
        // (a reload / file change uploads a new handle and a new build).
        const auto it = m_meshes.find(build->path);
        if (it != m_meshes.end() && it->second.handle == build->handle && !build->lods.empty() &&
            renderer.SetMeshLods(build->handle, build->lods)) {
            LUMA_LOG_INFO("Meshes", "'{}': {} LOD(s) {}", build->path, build->lods.size(),
                          build->fromCache ? "loaded from cache" : "built");
        }
        return true;
    });
}

void MaterialSceneBridge::Sync(const Scene& scene, SceneRenderer& renderer,
                               Material::MaterialLibrary& library) {
    PollLodBuilds(renderer);
    ProcessCompiles(renderer, library);
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
    if (m_preview) {
        SyncMesh(m_preview->first, renderer, library.Paths());
        if (const auto* sections = Sections(m_preview->first)) {
            for (const MeshSectionInfo& s : *sections) used.push_back(SlotKey(s.defaultMaterial));
        }
    }
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
    // Post-process volumes' material stacks.
    auto volumes = scene.Registry().view<const PostProcessComponent>();
    for (Entity e : volumes) {
        for (const PostProcessMaterial& m : volumes.get<const PostProcessComponent>(e).materials) {
            if (!m.material.empty()) used.push_back(SlotKey(m.material));
        }
    }
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    for (const std::string& path : used) SyncMaterial(path, renderer, library);
}

MaterialSceneBridge::MaterialSceneBridge() : m_shaderInclude(LUMA_SHADER_SOURCE_DIR) {}
MaterialSceneBridge::~MaterialSceneBridge() = default;

void MaterialSceneBridge::SetShaderPaths(std::filesystem::path includeDir, std::filesystem::path cacheDir) {
    m_shaderInclude = std::move(includeDir);
    m_shaderCache = std::move(cacheDir);
    m_shaders.reset();
}

ShaderCompiler::Compiler& MaterialSceneBridge::Shaders(const Material::MaterialLibrary& library) {
    if (!m_shaders) {
        std::filesystem::path cache = m_shaderCache;
        if (cache.empty()) {
            const std::filesystem::path game = library.Paths().GameRoot();
            cache = game.empty() ? std::filesystem::temp_directory_path() / "LumaShaderCache"
                                 : game.parent_path() / "Saved" / "ShaderCache";
        }
        m_shaders = std::make_unique<ShaderCompiler::Compiler>(m_shaderInclude, cache, 2);
    }
    return *m_shaders;
}

namespace {

// Combined versions of every Material Function a graph calls (recursively),
// so editing a function recompiles exactly the materials that use it.
u64 FunctionVersions(const MaterialGraph::Graph& graph, Material::MaterialLibrary& library, u32 depth) {
    u64 v = 0;
    if (depth > 16) return v;
    for (const MaterialGraph::Node& n : graph.Nodes()) {
        if (n.type != "MaterialFunctionCall") continue;
        const auto prop = n.properties.find("function");
        if (prop == n.properties.end()) continue;
        const std::string path = prop->second.AsString();
        v = v * 1099511628211ull + library.GraphVersion(path) + 1;
        if (const Material::MaterialFunction* f = library.GetFunction(path)) {
            v = v * 1099511628211ull + FunctionVersions(f->graph, library, depth + 1);
            v = v * 1099511628211ull + library.GraphVersion(path) + 1;  // loaded now
        }
    }
    return v;
}

// The graph material behind a slot path plus the overrides of every instance
// on the way (child over parent). False when nothing loadable is there.
bool ResolveGraph(const std::string& path, Material::MaterialLibrary& library, Material::GraphMaterial& material,
                  std::vector<Material::ParameterOverride>& overrides, std::optional<f32>& clip,
                  std::optional<bool>& twoSided, u64& version) {
    version = 0;
    if (path.empty()) {
        material = Material::GraphMaterial::MakeDefault();
        return true;
    }
    std::string current = path;
    for (u32 depth = 0; depth < 16; ++depth) {
        if (const Material::GraphMaterialInstance* inst = library.GetGraphInstance(current)) {
            version = version * 1099511628211ull + library.GraphVersion(current) + 1;
            for (const Material::ParameterOverride& o : inst->overrides) {
                const bool known = std::any_of(overrides.begin(), overrides.end(),
                                               [&](const Material::ParameterOverride& e) { return e.name == o.name; });
                if (!known) overrides.push_back(o);
            }
            if (!clip && inst->opacityMaskClip) clip = inst->opacityMaskClip;
            if (!twoSided && inst->twoSided) twoSided = inst->twoSided;
            current = inst->parent;
            continue;
        }
        // The Default Material is the engine's graph (its BaseColor / Roughness
        // are parameters instances override); the v3 file is Phase-1 legacy.
        if (current.empty() || current == Material::kDefaultMaterialPath) {
            material = Material::GraphMaterial::MakeDefault();
            return true;
        }
        if (const Material::GraphMaterial* m = library.GetGraphMaterial(current)) {
            version = version * 1099511628211ull + library.GraphVersion(current) + 1;
            material = *m;
            version = version * 1099511628211ull + FunctionVersions(material.graph, library, 0);
            return true;
        }
        return false;
    }
    return false;
}

}  // namespace

void MaterialSceneBridge::ReportErrors(MaterialEntry& entry, const std::string& errors) {
    if (errors == entry.reportedErrors) return;  // once per distinct error set
    entry.reportedErrors = errors;
    LUMA_USER_ERROR("Materials", "{}: {}", entry.label, errors);
}

bool MaterialSceneBridge::BindTextures(MaterialEntry& entry, GpuMaterialBuild& build, SceneRenderer& renderer,
                                       Material::MaterialLibrary& library) {
    entry.textures.clear();
    for (usize i = 0; i < build.gpu.textures.size(); ++i) {
        const std::string& texture = build.texturePaths[i];
        if (texture.empty()) continue;
        entry.textures.push_back(texture);
        build.gpu.textures[i].texture = SyncTexture(texture, renderer, library.Paths()).handle;
    }
    const auto& a = build.gpu.textures;
    const auto& b = entry.gpu.textures;
    if (a.size() != b.size()) return true;
    for (usize i = 0; i < a.size(); ++i) {
        if (a[i].slot != b[i].slot || a[i].texture != b[i].texture || a[i].srgb != b[i].srgb ||
            a[i].sampler != b[i].sampler || a[i].cube != b[i].cube) {
            return true;
        }
    }
    return false;
}

void MaterialSceneBridge::SyncMaterial(const std::string& path, SceneRenderer& renderer,
                                       Material::MaterialLibrary& library) {
    MaterialEntry& entry = m_materials[path];
    entry.label = path.empty() ? std::string(Material::kDefaultMaterialPath) : path;
    Material::GraphMaterial material;
    std::vector<Material::ParameterOverride> overrides;
    std::optional<f32> clip;
    std::optional<bool> twoSided;
    u64 version = 0;
    if (!ResolveGraph(path, library, material, overrides, clip, twoSided, version)) {
        if (!entry.uploaded || entry.version != ~0ull) {
            LUMA_USER_WARN("Materials", "'{}' is not a loadable material; using the Default Material", path);
        }
        entry.handle = kDefaultMaterialHandle;
        entry.version = ~0ull;
        entry.uploaded = true;
        entry.pending.reset();
        return;
    }
    if (entry.uploaded && entry.version == version) {
        // Same graph. Collections it reads may have new values (re-evaluate
        // the block, no recompile) or be gone (recompile: reports the error
        // and keeps the last good shaders).
        bool missing = false, changed = false;
        for (auto& [collection, revision] : entry.collectionRevisions) {
            const Material::ParameterCollection* pc = library.GetCollection(collection);
            if (!pc) {
                missing = true;
            } else if (pc->revision != revision) {
                revision = pc->revision;
                changed = true;
            }
        }
        if (!missing) {
            if (changed && !entry.fingerprint.empty()) {
                MaterialParameterBlock block;
                block.values = MaterialCompiler::EvaluatePreshader(entry.preshader, entry.layout, entry.overrides,
                                                                   MaterialCompiler::CollectionValues(library), nullptr);
                renderer.UpdateMaterialParameters(entry.handle, block);
            }
            return;
        }
    }
    entry.version = version;
    entry.uploaded = true;

    GpuMaterialBuild build = PrepareGpuMaterial(material, &library, overrides, clip, twoSided);
    if (!build.ok) {
        ReportErrors(entry, build.errors);
        entry.pending.reset();
        return;  // the previous shaders (or the Default Material) stay on screen
    }
    entry.overrides = overrides;

    if (!entry.fingerprint.empty() && build.fingerprint == entry.fingerprint) {
        // Same shaders (an instance's parameters, textures or settings that
        // need no pipelines): new block, textures and state only.
        entry.reportedErrors.clear();
        entry.pending.reset();
        const bool texturesChanged = BindTextures(entry, build, renderer, library);
        entry.preshader = std::move(build.preshader);
        entry.layout = std::move(build.layout);
        const CompiledMaterialGpu& g = build.gpu;
        if (texturesChanged || g.opacityMaskClip != entry.gpu.opacityMaskClip || g.twoSided != entry.gpu.twoSided) {
            build.gpu.passes = entry.gpu.passes;
            build.gpu.shaderKey = entry.gpu.shaderKey;
            entry.gpu = build.gpu;
            entry.handle = renderer.UpdateMaterial(MaterialKey(path), entry.gpu, build.block);
        } else {
            renderer.UpdateMaterialParameters(entry.handle, build.block);
        }
        return;
    }

    // New shaders: compile the set (or join one already compiling / done).
    const std::string fingerprint = build.fingerprint;
    auto [it, created] = m_sets.try_emplace(fingerprint);
    if (created) {
        ShaderSet& set = it->second;
        set.sources = build.sources;
        set.spirv.resize(set.sources.size());
        set.keys.resize(set.sources.size());
        set.remaining = static_cast<u32>(set.sources.size());
        ShaderCompiler::Compiler& shaders = Shaders(library);
        for (u32 s = 0; s < set.sources.size(); ++s) {
            ShaderCompiler::CompileRequest request;
            request.source = set.sources[s].source;
            request.stage = set.sources[s].vertex ? ShaderCompiler::ShaderStage::Vertex
                                                  : ShaderCompiler::ShaderStage::Fragment;
            request.name = entry.label + (set.sources[s].vertex ? ".vert" : ".frag");
            m_tickets[shaders.Submit(std::move(request))] = {fingerprint, s};
        }
        ++m_setsCompiled;
        if (entry.fingerprint.empty()) LUMA_USER_INFO("Materials", "Compiling {}...", entry.label);
    }
    entry.pending = std::move(build);
    if (it->second.remaining == 0) {
        if (it->second.failed) {
            ReportErrors(entry, it->second.errors);
            entry.pending.reset();
        } else {
            Finalize(path, entry, it->second, renderer, library);
        }
    }
}

void MaterialSceneBridge::Finalize(const std::string& path, MaterialEntry& entry, const ShaderSet& set,
                                   SceneRenderer& renderer, Material::MaterialLibrary& library) {
    GpuMaterialBuild build = std::move(*entry.pending);
    entry.pending.reset();
    u64 key = kShaderKeySeed;
    for (usize s = 0; s < set.sources.size(); ++s) {
        auto& pass = build.gpu.passes[static_cast<usize>(set.sources[s].pass)];
        if (!pass) pass.emplace();
        (set.sources[s].vertex ? pass->vertex : pass->fragment) = set.spirv[s];
        key = FoldShaderKey(key, set.keys[s]);
        pass->key = key;
    }
    build.gpu.shaderKey = key;
    const bool first = entry.fingerprint.empty();
    BindTextures(entry, build, renderer, library);
    entry.gpu = build.gpu;
    entry.fingerprint = build.fingerprint;
    entry.reportedErrors.clear();
    entry.preshader = std::move(build.preshader);
    entry.layout = std::move(build.layout);
    entry.collectionRevisions.clear();
    for (const auto& [collection, parameter] : entry.preshader.collectionValues) {
        if (const Material::ParameterCollection* pc = library.GetCollection(collection)) {
            entry.collectionRevisions[collection] = pc->revision;
        }
    }
    entry.handle = renderer.UpdateMaterial(MaterialKey(path), entry.gpu, build.block);
    if (first) LUMA_USER_INFO("Materials", "Compiled {} ({} instructions)", entry.label, set.instructions);
}

void MaterialSceneBridge::ProcessCompiles(SceneRenderer& renderer, Material::MaterialLibrary& library) {
    if (!m_shaders) return;
    for (auto& [ticket, result] : m_shaders->Poll()) {
        const auto t = m_tickets.find(ticket);
        if (t == m_tickets.end()) continue;
        const auto [fingerprint, index] = t->second;
        m_tickets.erase(t);
        const auto s = m_sets.find(fingerprint);
        if (s == m_sets.end()) continue;
        ShaderSet& set = s->second;
        if (result.ok) {
            set.instructions += ShaderCompiler::Compiler::Stats(result.spirv).instructions;
            set.spirv[index] = std::move(result.spirv);
            set.keys[index] = result.key;
        } else if (!set.failed) {
            set.failed = true;
            set.errors = "shader compile failed: " +
                         (result.errors.empty() ? result.log : result.errors.front().second);
        }
        if (set.remaining > 0) --set.remaining;
    }
    for (auto& [path, entry] : m_materials) {
        if (!entry.pending) continue;
        const auto s = m_sets.find(entry.pending->fingerprint);
        if (s == m_sets.end() || s->second.remaining > 0) continue;
        if (s->second.failed) {
            ReportErrors(entry, s->second.errors);
            entry.pending.reset();
        } else {
            Finalize(path, entry, s->second, renderer, library);
        }
    }
}

void MaterialSceneBridge::FinishCompiles(SceneRenderer& renderer, Material::MaterialLibrary& library) {
    while (PendingCompiles() > 0) {
        ProcessCompiles(renderer, library);
        if (PendingCompiles() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

u32 MaterialSceneBridge::PendingCompiles() const {
    u32 n = 0;
    for (const auto& [path, entry] : m_materials) n += entry.pending ? 1 : 0;
    return n;
}

std::string_view MaterialSceneBridge::Errors(std::string_view materialPath) const {
    const auto it = m_materials.find(SlotKey(materialPath));
    return it == m_materials.end() ? std::string_view{} : std::string_view(it->second.reportedErrors);
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
        LUMA_USER_WARN("Materials", "Texture '{}' could not be loaded; sampling white instead", path);
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
                       "texture slots); sampling white instead",
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
    const auto file = paths.ToFile(path, {"lmesh"});
    if (file) source = LumaMeshIO::ReadMesh(*file);
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
    if (!entry.failed && m_lodJobs != nullptr && file && !m_lodCacheDir.empty()) {
        auto build = std::make_shared<LodBuild>();
        build->path = path;
        build->handle = entry.handle;
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx.lods", static_cast<unsigned long long>(MeshKey(path)));
        auto lodSource = std::make_shared<MeshData>(mesh);
        Jobs::JobDesc desc;
        desc.name = "Mesh LODs: " + path;
        desc.priority = Jobs::Priority::Low;
        m_lodJobs->Schedule(std::move(desc), [build, lodSource, meshFile = *file, cacheFile = m_lodCacheDir / name] {
            const auto key = Mesh::MakeLodCacheKey(meshFile, *lodSource);
            if (key) {
                if (auto cached = Mesh::ReadLodCache(cacheFile, *key)) {
                    build->lods = std::move(*cached);
                    build->fromCache = true;
                }
            }
            if (!build->fromCache) {
                build->lods = Mesh::BuildMeshLods(*lodSource);
                if (key && !Mesh::WriteLodCache(cacheFile, *key, build->lods)) {
                    LUMA_LOG_WARN("Meshes", "Could not write the LOD cache {}", cacheFile.string());
                }
            }
            build->done.store(true, std::memory_order_release);
        });
        m_lodBuilds.push_back(std::move(build));
    }
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
        if (scene.Registry().all_of<EditorHiddenComponent>(e)) continue;
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
    if (m_preview) {
        auto it = m_meshes.find(m_preview->first);
        if (it != m_meshes.end() && !it->second.failed) {
            SceneInstance inst;
            inst.model = m_preview->second;
            inst.mesh = it->second.handle;
            inst.selected = true;  // outlined, like Unreal's drop preview
            inst.sectionCount = it->second.sectionCount;
            const u32 slots = std::min(inst.sectionCount, kMaxSectionMaterials);
            for (u32 s = 0; s < slots; ++s) {
                inst.sectionMaterials[s] = s < it->second.sections.size()
                                               ? HandleFor(it->second.sections[s].defaultMaterial)
                                               : kDefaultMaterialHandle;
            }
            out.push_back(inst);
        }
    }
}

void MaterialSceneBridge::SetPlacementPreview(std::string meshPath, const Math::Mat4& model) {
    m_preview.emplace(std::move(meshPath), model);
}

void MaterialSceneBridge::ClearPlacementPreview() { m_preview.reset(); }

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
