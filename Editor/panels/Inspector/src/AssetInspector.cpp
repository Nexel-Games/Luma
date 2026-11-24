#include "Luma/Editor/Panels/AssetInspector.h"

#include <ctime>
#include <fstream>
#include <functional>
#include <utility>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Luma/Asset/AssetData.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Asset/ImageDecode.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/AssetImport/Pipelines/ImportPlan.h"
#include "Luma/Audio/AudioDecode.h"
#include "Luma/Audio/AudioPreview.h"
#include "Luma/Serialization/Json.h"
#include "Luma/Core/Log.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/PrefabAssetEditor.h"
#include "Luma/Editor/Panels/SlotParameters.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialEditors/InstanceParameters.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

std::string FormatAssetSize(u64 bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    const char* units[] = {"KB", "MB", "GB", "TB"};
    f64 value = static_cast<f64>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

std::string FormatAssetTime(i64 filetime) {
    if (filetime <= 0) return "Unknown";
    // FILETIME: 100 ns ticks since 1601-01-01 (UTC).
    const std::time_t seconds = static_cast<std::time_t>(filetime / 10000000ll - 11644473600ll);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &seconds) != 0) return "Unknown";
#else
    if (!localtime_r(&seconds, &local)) return "Unknown";
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
    return buf;
}

bool SetMeshSectionMaterial(const std::filesystem::path& meshFile, u32 section, const std::string& material,
                            std::string* error) {
    auto mesh = LumaMeshIO::ReadMesh(meshFile);
    if (!mesh) {
        if (error) *error = "could not read " + meshFile.generic_string();
        return false;
    }
    if (section >= mesh->submeshes.size()) {
        if (error) *error = "the mesh has no section " + std::to_string(section);
        return false;
    }
    mesh->submeshes[section].materialName = material;
    if (!LumaMeshIO::WriteMesh(meshFile, *mesh)) {
        if (error) *error = "could not write " + meshFile.generic_string();
        return false;
    }
    return true;
}

bool SetAssetPlacement(const std::filesystem::path& assetFile, const AssetId& id, AssetType type,
                       const Import::PlacementTransform& placement, std::string* error) {
    Import::ImportMetadata meta;
    if (auto existing = Import::ReadMetadata(assetFile)) {
        meta = std::move(*existing);
    } else {
        meta.guid = id;
        meta.type = type;
        meta.settings = SerialValue::MakeObject();
    }
    meta.placement = placement;
    return Import::WriteMetadata(assetFile, meta, error);
}

namespace {

// Per-asset facts read from disk once per (asset, modification time).
template <typename T>
struct FileCache {
    AssetId id{};
    i64 mtime = -1;
    std::optional<T> value;
    template <typename Load>
    const std::optional<T>& Get(const AssetData& a, Load load) {
        if (a.id != id || a.mtime != mtime) {
            id = a.id;
            mtime = a.mtime;
            value = load(a.packagePath);
        }
        return value;
    }
};

std::string FormatDuration(f64 seconds) {
    char buf[32];
    const int minutes = static_cast<int>(seconds / 60.0);
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", minutes, seconds - minutes * 60.0);
    return buf;
}

std::string FormatVec3(const Math::Vec3& v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3g x %.3g x %.3g", v.x, v.y, v.z);
    return buf;
}

// The import settings block of an asset kind, drawn from the importer's own
// field descriptions (the same ones the Import window uses). True when a
// value changed (the caller saves the .meta).
bool DrawSettingsBlock(InspectorRows& rows, Import::SettingsBlock& block, const std::string& prefix) {
    bool changed = false;
    for (const Import::SettingField& f : block.Fields()) {
        if (!block.IsVisible(f)) continue;
        const std::string key = prefix + "." + f.key;
        Slate::SpinBoxOptions o;
        if (f.max > f.min) {
            o.sliderMin = static_cast<f32>(f.min);
            o.sliderMax = static_cast<f32>(f.max);
        }
        if (f.step > 0.0) o.speed = static_cast<f32>(f.step);
        switch (f.type) {
            case Import::FieldType::Bool: {
                bool v = block.GetBool(f.key);
                if (rows.Check(f.label, key, v)) {
                    block.Set(f.key, SerialValue(v));
                    changed = true;
                }
                break;
            }
            case Import::FieldType::Float: {
                f32 v = static_cast<f32>(block.GetFloat(f.key));
                if (rows.Float(f.label, key, v, o)) {
                    block.Set(f.key, SerialValue(static_cast<f64>(v)));
                    changed = true;
                }
                break;
            }
            case Import::FieldType::Int: {
                i32 v = static_cast<i32>(block.GetInt(f.key));
                if (rows.Int(f.label, key, v, o)) {
                    block.Set(f.key, SerialValue(static_cast<i64>(v)));
                    changed = true;
                }
                break;
            }
            case Import::FieldType::Enum: {
                const std::string current = block.GetString(f.key);
                int index = 0;
                for (usize i = 0; i < f.enumValues.size(); ++i) {
                    if (f.enumValues[i] == current) index = static_cast<int>(i);
                }
                if (rows.Combo(f.label, key, f.enumValues, index)) {
                    block.Set(f.key, SerialValue(f.enumValues[static_cast<usize>(index)]));
                    changed = true;
                }
                break;
            }
            case Import::FieldType::Vec3: {
                Math::Vec3 v = block.GetVec3(f.key);
                if (rows.Vec3(f.label, key, v)) {
                    SerialValue a = SerialValue::MakeArray();
                    a.PushBack(static_cast<f64>(v.x));
                    a.PushBack(static_cast<f64>(v.y));
                    a.PushBack(static_cast<f64>(v.z));
                    block.Set(f.key, std::move(a));
                    changed = true;
                }
                break;
            }
            case Import::FieldType::String: {
                std::string v = block.GetString(f.key);
                if (rows.Text(f.label, key, v)) {
                    block.Set(f.key, SerialValue(v));
                    changed = true;
                }
                break;
            }
        }
    }
    return changed;
}

// "Import Settings" for an imported asset: its .meta settings, edited in
// place and saved; Reimport applies them.
void DrawImportSettings(InspectorRows& rows, const AssetData& asset) {
    auto meta = Import::ReadMetadata(asset.packagePath);
    if (!meta) return;  // not imported (no source): nothing to re-run
    Import::PipelineStack stack;
    stack.FromJson(meta->settings);
    Import::SettingsBlock* block = asset.type == AssetType::Mesh      ? &stack.Meshes()
                                   : asset.type == AssetType::Texture ? &stack.Textures()
                                   : asset.type == AssetType::Sound   ? &stack.Audio()
                                                                      : nullptr;
    if (!block) return;
    rows.SubHeading("Import Settings");
    if (!meta->sourceFile.empty()) rows.ReadOnly("Source File", meta->sourceFile);
    if (DrawSettingsBlock(rows, *block, "asset.import")) {
        meta->settings = stack.ToJson();
        std::string error;
        if (!Import::WriteMetadata(asset.packagePath, *meta, &error)) {
            LUMA_USER_ERROR("Assets", "Could not save the import settings of '{}': {}", asset.assetName, error);
        }
    }
    rows.Note("Changes are saved now and applied by Reimport.", rows.ui.theme().textDim);
}

// The placement transform of a placeable asset (meshes, prefabs).
void DrawPlacement(InspectorRows& rows, const AssetData& asset) {
    const auto meta = Import::ReadMetadata(asset.packagePath);
    Import::PlacementTransform p = meta ? meta->placement : Import::PlacementTransform{};
    rows.SubHeading("Transform");
    bool changed = rows.Vec3("Location", "asset.place.pos", p.position);
    changed = rows.Vec3("Rotation", "asset.place.rot", p.rotation) || changed;
    changed = rows.Vec3("Scale", "asset.place.scale", p.scale) || changed;
    if (changed) {
        std::string error;
        if (!SetAssetPlacement(asset.packagePath, asset.id, asset.type, p, &error)) {
            LUMA_USER_ERROR("Assets", "Could not save the transform of '{}': {}", asset.assetName, error);
        }
    }
    rows.Note("Applied when the asset is placed in a level (location is an offset from the drop point).",
              rows.ui.theme().textDim);
}

const std::vector<std::string> kDomains{"Surface", "Post Process"};
const std::vector<std::string> kBlendModes{"Opaque", "Masked", "Translucent", "Additive", "Modulate"};
const std::vector<std::string> kShadingModels{"Default Lit", "Unlit", "Subsurface", "Clear Coat",
                                              "Two-Sided Foliage"};

bool Importable(AssetType type) {
    return type == AssetType::Mesh || type == AssetType::Texture || type == AssetType::Sound;
}

}  // namespace

void InspectorPanel::DrawMaterialAsset(InspectorRows& rows, const std::string& path) {
    Material::MaterialLibrary* lib = rows.panel.materials;
    const Material::GraphMaterial* material = lib ? lib->GetGraphMaterial(path) : nullptr;
    if (!material) {
        rows.Note("This material could not be loaded (see the Console).", rows.ui.theme().error);
        return;
    }
    // Settings: a change recompiles the material (new shaders).
    Material::MaterialSettings s = material->settings;
    bool changed = false;
    int domain = static_cast<int>(s.domain), blend = static_cast<int>(s.blendMode),
        shading = static_cast<int>(s.shadingModel);
    rows.SubHeading("Material");
    if (rows.Combo("Material Domain", "asset.mat.domain", kDomains, domain)) {
        s.domain = static_cast<Material::MaterialDomain>(domain);
        changed = true;
    }
    if (rows.Combo("Blend Mode", "asset.mat.blend", kBlendModes, blend)) {
        s.blendMode = static_cast<Material::BlendMode>(blend);
        changed = true;
    }
    if (rows.Combo("Shading Model", "asset.mat.shading", kShadingModels, shading)) {
        s.shadingModel = static_cast<Material::ShadingModel>(shading);
        changed = true;
    }
    if (rows.Check("Two Sided", "asset.mat.twosided", s.twoSided)) changed = true;
    if (s.blendMode == Material::BlendMode::Masked) {
        Slate::SpinBoxOptions o;
        o.speed = 0.01f;
        o.sliderMin = 0.0f;
        o.sliderMax = 1.0f;
        if (rows.Float("Opacity Mask Clip", "asset.mat.clip", s.opacityMaskClip, o)) changed = true;
    }
    if (changed) {
        Material::GraphMaterial edited = *material;
        edited.settings = s;
        lib->SetGraphMaterial(path, std::move(edited));
    }
    rows.SubHeading("Parameters");
    rows.Note("Defaults used by every object and instance that doesn't override them.", rows.ui.theme().textDim);
    DrawMaterialParameters(rows, path, "asset.mat.p");
}

void InspectorPanel::DrawInstanceAsset(InspectorRows& rows, const std::string& path) {
    Material::MaterialLibrary* lib = rows.panel.materials;
    if (!lib || !lib->GetGraphInstance(path)) {
        rows.Note("This material instance could not be loaded (see the Console).", rows.ui.theme().error);
        return;
    }
    MaterialEditors::InstanceParameterModel model(path, *lib);
    rows.SubHeading("Material Instance");
    std::vector<std::string> parents{""};
    if (rows.panel.listMaterials) {
        for (std::string& m : rows.panel.listMaterials()) {
            if (m != path) parents.push_back(std::move(m));
        }
    }
    const std::string parent = model.Parent();
    int current = 0;
    for (usize i = 0; i < parents.size(); ++i) {
        if (parents[i] == parent) current = static_cast<int>(i);
    }
    if (current == 0 && !parent.empty()) {
        parents.push_back(parent);
        current = static_cast<int>(parents.size() - 1);
    }
    std::vector<std::string> labels;
    for (const std::string& p : parents) {
        labels.push_back(p.empty() ? std::string("None") : p.substr(p.find_last_of('/') + 1));
    }
    if (rows.Combo("Parent", "asset.mi.parent", labels, current) && current > 0) {
        std::string error;
        if (!model.SetParent(parents[static_cast<usize>(current)], &error)) {
            LUMA_USER_ERROR("Materials", "Can't set the parent of '{}': {}", path, error);
        }
    }
    rows.SubHeading("Parameters");
    rows.Note("Changes here affect every object using this instance. The arrow resets an override.",
              rows.ui.theme().textDim);
    DrawMaterialParameters(rows, path, "asset.mi.p");
}

void InspectorPanel::DrawMeshAsset(InspectorRows& rows, const AssetData& asset) {
    static FileCache<LumaMeshData> cache;
    const auto& mesh = cache.Get(asset, [](const std::filesystem::path& f) { return LumaMeshIO::ReadMesh(f); });
    DrawPlacement(rows, asset);
    rows.SubHeading("Mesh");
    if (!mesh) {
        rows.Note("This mesh file could not be read.", rows.ui.theme().error);
        return;
    }
    rows.ReadOnly("Vertices", std::to_string(mesh->vertices.size()));
    rows.ReadOnly("Triangles", std::to_string(mesh->indices.size() / 3));
    rows.ReadOnly("Sections", std::to_string(mesh->submeshes.size()));
    LumaBounds bounds;
    if (!mesh->vertices.empty()) bounds.ComputeFromVertices(mesh->vertices.data(), mesh->vertices.size());
    rows.ReadOnly("Size (m)", FormatVec3(bounds.max - bounds.min));
    rows.ReadOnly("LODs", "Automatic (built in the background when first used)");

    rows.SubHeading("Material Slots");
    std::vector<std::string> materials{""};
    if (rows.panel.listMaterials) {
        for (std::string& m : rows.panel.listMaterials()) materials.push_back(std::move(m));
    }
    for (u32 s = 0; s < mesh->submeshes.size(); ++s) {
        const std::string& current = mesh->submeshes[s].materialName;
        const bool isAsset = !current.empty() && current.front() == '/';
        std::vector<std::string> paths = materials;
        int index = 0;
        for (usize i = 0; i < paths.size(); ++i) {
            if (isAsset && paths[i] == current) index = static_cast<int>(i);
        }
        if (isAsset && index == 0) {
            paths.push_back(current);
            index = static_cast<int>(paths.size() - 1);
        }
        std::vector<std::string> labels;
        for (const std::string& p : paths) {
            labels.push_back(p.empty() ? std::string("Default Material") : p.substr(p.find_last_of('/') + 1));
        }
        const std::string label = "Element " + std::to_string(s) +
                                  (!isAsset && !current.empty() ? " (" + current + ")" : std::string());
        if (rows.Combo(label, "asset.mesh.slot." + std::to_string(s), labels, index)) {
            std::string error;
            if (!SetMeshSectionMaterial(asset.packagePath, s, paths[static_cast<usize>(index)], &error)) {
                LUMA_USER_ERROR("Assets", "Could not set the material of '{}': {}", asset.assetName, error);
            } else {
                cache.mtime = -1;  // re-read the new file
            }
        }
    }
    DrawImportSettings(rows, asset);
}

void InspectorPanel::DrawTextureAsset(InspectorRows& rows, const AssetData& asset) {
    static FileCache<Import::DecodedImage> cache;
    const auto& image = cache.Get(asset, [](const std::filesystem::path& f) {
        std::string error;
        return Import::DecodeImageFile(f, &error);
    });
    rows.SubHeading("Texture");
    if (!image) {
        rows.Note("This image could not be decoded.", rows.ui.theme().error);
    } else {
        rows.ReadOnly("Resolution", std::to_string(image->width) + " x " + std::to_string(image->height));
        static const char* kChannels[] = {"", "Grayscale", "Grayscale + Alpha", "RGB", "RGBA"};
        rows.ReadOnly("Format", std::string(kChannels[std::min(image->channels, 4u)]) +
                                    (image->isFloat ? ", 32-bit float (HDR)" : ", 8-bit"));
        u32 mips = 1;
        for (u32 s = std::max(image->width, image->height); s > 1; s /= 2) ++mips;
        rows.ReadOnly("Mip Levels", std::to_string(mips) + " (full chain on the GPU)");
        const u64 bytes = static_cast<u64>(image->width) * image->height * 4 * (image->isFloat ? 2 : 1);
        rows.ReadOnly("GPU Memory", "~" + FormatAssetSize(bytes + bytes / 3));
    }
    DrawImportSettings(rows, asset);
}

void InspectorPanel::DrawSoundAsset(InspectorRows& rows, const AssetData& asset) {
    static FileCache<Audio::SoundInfo> cache;
    const auto& info = cache.Get(asset, [](const std::filesystem::path& f) {
        std::string error;
        return Audio::ProbeSound(f, &error);
    });
    rows.SubHeading("Sound");
    if (!info) {
        rows.Note("This sound could not be read.", rows.ui.theme().error);
    } else {
        rows.ReadOnly("Duration", FormatDuration(info->seconds));
        rows.ReadOnly("Channels", info->channels == 1 ? "Mono" : info->channels == 2 ? "Stereo"
                                                               : std::to_string(info->channels));
        rows.ReadOnly("Sample Rate", std::to_string(info->sampleRate) + " Hz");
    }
    if (Audio::AudioPreview* preview = rows.panel.audioPreview) {
        const bool playing = preview->IsPlaying();
        const Slate::Rect r{rows.x + rows.NameColumn(), rows.y, 90.0f, 22.0f};
        if (rows.ui.Button(Slate::Context::ID("asset.sound.play"), r, playing ? "Stop" : "Play")) {
            if (playing) {
                preview->Stop();
            } else {
                preview->Play(asset.packagePath);
            }
        }
        rows.y += 28.0f;
    }
    DrawImportSettings(rows, asset);
}

void InspectorPanel::DrawSceneAsset(InspectorRows& rows, const AssetData& asset) {
    static FileCache<usize> cache;
    const auto& count = cache.Get(asset, [](const std::filesystem::path& f) -> std::optional<usize> {
        std::ifstream in(f, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const auto json = ParseJson(text.str());
        const SerialValue* entities = json ? json->Find("entities") : nullptr;
        if (!entities || !entities->IsArray()) return std::nullopt;
        return entities->Size();
    });
    rows.SubHeading("Level");
    rows.ReadOnly("Objects", count ? std::to_string(*count) : std::string("Unreadable level file"));
    rows.Note("Open (above) loads this level in the editor.", rows.ui.theme().textDim);
}

void InspectorPanel::DrawPrefabAsset(InspectorRows& rows, PanelContext& ctx, const AssetData& asset,
                                     const std::string& path) {
    Slate::Context& ui = rows.ui;
    const Slate::Theme& t = ui.theme();
    if (!m_prefabEdit) m_prefabEdit = std::make_unique<PrefabAssetEditor>();
    PrefabAssetEditor& edit = *m_prefabEdit;
    const bool readable = edit.Open(asset.packagePath, path);
    if (edit.TakeReloaded()) {
        ClosePopups();  // pickers pointed into the previous edit scene
        m_prefabSaveError.clear();
    }
    if (!readable) {
        rows.SubHeading("Prefab");
        rows.Note("This prefab can't be read: " + edit.Error(), t.warning);
        return;
    }
    Scene& scene = edit.EditScene();
    auto& reg = scene.Registry();
    const Entity root = edit.Root();

    // Placement: the root's rotation and scale. Copies land where they are
    // dropped; their own transform is theirs to change afterwards.
    rows.SubHeading("Placement");
    auto& rootXf = reg.get<TransformComponent>(root);
    rows.Vec3("Rotation", "prefab.rotation", rootXf.rotationEuler);
    rows.Vec3("Scale", "prefab.scale", rootXf.scale);
    rows.Note("New copies placed in the level get this rotation and scale.", t.textDim);

    // The prefab's objects; the picked one's components follow.
    rows.SubHeading("Objects");
    Entity picked = edit.Picked();
    const f32 left = rows.panelLeft + 10.0f;
    const f32 width = rows.panelRight - rows.panelLeft - 20.0f;
    std::function<void(Entity, int)> objectRow = [&](Entity e, int depth) {
        const f32 indent = 14.0f * static_cast<f32>(depth);
        const Slate::Rect r{left + indent, rows.y, width - indent, 22.0f};
        const NameComponent* name = reg.try_get<NameComponent>(e);
        if (ui.Selectable(rows.Id("prefab.object." + std::to_string(static_cast<u32>(e))), r,
                          name ? name->name : std::string("Object"), e == picked,
                          e == root ? Slate::Icon::Cube : Slate::Icon::Dot)) {
            edit.Pick(e);
        }
        rows.y += 24.0f;
        for (Entity child : ChildrenOf(scene, e)) objectRow(child, depth + 1);
    };
    objectRow(root, 0);
    picked = edit.Picked();
    {
        f32 bx = left;
        const f32 by = rows.y + 2.0f;
        auto button = [&](const char* label, const char* tip, f32 bw) {
            const Slate::Rect r{bx, by, bw, 22.0f};
            bx += bw + 6.0f;
            const bool clicked = ui.Button(rows.Id(std::string("prefab.button.") + label), r, label);
            ui.Tooltip(rows.Id(std::string("prefab.button.tip.") + label), r, tip);
            return std::pair{clicked, r};
        };
        if (button("Add Object", "Add an empty object under the selected one.", 92.0f).first) {
            picked = edit.AddObject(picked);
        }
        if (picked != root &&
            button("Remove", "Remove the selected object and its children from the prefab.", 70.0f).first) {
            edit.RemoveObject(picked);
            picked = edit.Picked();
        }
        const u64 addId = rows.Id("prefab.addcomponent");
        const auto [addClicked, addRect] =
            button("Add Component", "Attach a new component to the selected object.", 116.0f);
        if (addClicked) {
            Slate::MenuOptions opts;
            opts.searchable = true;
            opts.searchHint = "Search components";
            opts.minWidth = 240.0f;
            ui.OpenMenuBelow(addId, addRect, BuildAddComponentMenu(&scene, picked), opts);
        }
        rows.y = by + 32.0f;
    }

    // The picked object.
    if (NameComponent* name = reg.try_get<NameComponent>(picked)) {
        rows.SubHeading(picked == root ? "Root Object" : "Object");
        rows.Text("Name", "prefab.name." + std::to_string(static_cast<u32>(picked)), name->name);
    }
    for (const ComponentDesc* desc : Components().All()) {
        if (!scene.IsValid(picked) || !desc->has(scene, picked)) continue;
        const InspectorDrawer* drawer = InspectorDrawers().Find(desc->id);
        if (drawer && drawer->hidden) continue;
        if (picked == root && desc->id == "Transform") continue;  // Placement, above
        Slate::Rect band{rows.panelLeft, 0.0f, rows.panelRight - rows.panelLeft, 0.0f};
        f32 y = rows.y + 4.0f;
        DrawSectionHeader(ui, ctx, band, y, rows.x, rows.w, desc->displayName,
                          rows.Id("prefab.section." + desc->id) ^ static_cast<u64>(static_cast<u32>(picked)),
                          BuildComponentMenu(&scene, picked, *desc), t.accent);
        InspectorRows section(ui, ctx, scene, picked, rows.x, y, rows.w, m_pickers);
        section.panelLeft = rows.panelLeft;
        section.panelRight = rows.panelRight;
        if (drawer) {
            drawer->draw(section);
        } else {
            DrawReflectedComponent(section, *desc);
        }
        rows.y = section.y + 8.0f;
    }

    // Save once an edit is finished (not on every frame of a drag).
    if (!ui.isMouseDown(0) && !ui.textFieldFocused() && edit.Dirty()) {
        std::string err;
        if (edit.Commit(&err)) {
            m_prefabSaveError.clear();
            if (ctx.onPrefabSaved) ctx.onPrefabSaved(path);
        } else if (!err.empty() && err != m_prefabSaveError) {
            m_prefabSaveError = err;
            LUMA_USER_ERROR("Prefabs", "Could not save '{}': {}", path, err);
        }
    }
    if (!m_prefabSaveError.empty()) rows.Note("Not saved: " + m_prefabSaveError, t.warning);
}

void InspectorPanel::DrawAsset(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx,
                               const AssetData& asset) {
    const Slate::Theme& t = ui.theme();
    if (asset.id != m_lastAsset) {
        ClosePopups();
        m_lastAsset = asset.id;
        m_assetScroll = 0.0f;
    }
    const std::string path = ctx.virtualPathOf ? ctx.virtualPathOf(asset.packagePath) : std::string{};
    const auto typeDesc = AssetTypes().FindBuiltin(asset.type);
    const std::string typeName = typeDesc ? typeDesc->displayName : std::string("Asset");
    const f32 x = body.x + 12.0f;
    const f32 w = body.w - 24.0f;

    // Pinned header: thumbnail, name, type; actions.
    constexpr f32 kThumb = 64.0f;
    const Slate::Rect thumb{x, body.y + 10.0f, kThumb, kThumb};
    ui.PanelRounded(thumb, t.surface3, 4.0f);
    if (const TextureHandle tex = ctx.assetThumbnail && !path.empty() ? ctx.assetThumbnail(path) : 0) {
        ui.Image(tex, thumb.Inset(2.0f, 2.0f));
    }
    const f32 tx = thumb.Right() + 10.0f;
    ui.Heading({tx, thumb.y, x + w - tx, 24.0f}, asset.assetName, t.text);
    ui.LabelIn({tx, thumb.y + 24.0f, x + w - tx, 18.0f}, typeName, t.accent);
    f32 bx = tx;
    const f32 by = thumb.y + 44.0f;
    auto action = [&](const char* label, const char* tip, f32 bw) {
        const Slate::Rect r{bx, by, bw, 22.0f};
        bx += bw + 6.0f;
        const bool clicked = ui.Button(Slate::Context::ID(std::string("asset.action.") + label), r, label);
        ui.Tooltip(Slate::Context::ID(std::string("asset.action.tip.") + label), r, tip);
        return clicked;
    };
    if (ctx.onOpenAsset && !path.empty() && action("Open", "Open the asset in its editor.", 60.0f)) {
        ctx.onOpenAsset(path);
    }
    if (ctx.onBrowseToAsset && !path.empty() && action("Browse", "Show the asset in the Content Browser.", 70.0f)) {
        ctx.onBrowseToAsset(path);
    }
    if (Importable(asset.type) && ctx.onReimport &&
        action("Reimport", "Re-import the asset from its source file with its current import settings.", 80.0f)) {
        ctx.onReimport(asset.packagePath);
    }

    // Scrolling content: common info, then the type's section.
    const f32 contentTop = thumb.Bottom() + 12.0f;
    const Slate::Rect contentRegion{body.x, contentTop, body.w, body.Bottom() - contentTop};
    ui.Panel({body.x, contentTop - 1.0f, body.w, 1.0f}, t.separator);
    ui.PushClip(contentRegion);
    InspectorRows rows(ui, ctx, *ctx.scene, kNullEntity, x, contentTop + 8.0f - m_assetScroll, w, m_pickers);
    rows.panelLeft = body.x;
    rows.panelRight = body.Right();
    rows.SubHeading("Info");
    rows.ReadOnly("Path", path.empty() ? asset.packagePath.generic_string() : path);
    rows.ReadOnly("Type", typeName);
    rows.ReadOnly("Size on Disk", FormatAssetSize(asset.size));
    rows.ReadOnly("Modified", FormatAssetTime(asset.mtime));
    rows.ReadOnly("Asset ID", ToString(asset.id));
    switch (asset.type) {
        case AssetType::Material:
            DrawMaterialAsset(rows, path);
            break;
        case AssetType::MaterialInstance:
            DrawInstanceAsset(rows, path);
            break;
        case AssetType::Mesh:
            DrawMeshAsset(rows, asset);
            break;
        case AssetType::Texture:
            DrawTextureAsset(rows, asset);
            break;
        case AssetType::Sound:
            DrawSoundAsset(rows, asset);
            break;
        case AssetType::Scene:
            DrawSceneAsset(rows, asset);
            break;
        case AssetType::Prefab:
            DrawPrefabAsset(rows, ctx, asset, path);
            break;
        default:
            break;
    }
    const f32 contentH = rows.y + m_assetScroll - contentTop + 12.0f;
    ui.PopClip();
    m_assetScroll = ui.VerticalScroll(Slate::Context::ID("insp.asset.scroll"), contentRegion, contentH, m_assetScroll);
}

}  // namespace Luma::Editor::Panels
