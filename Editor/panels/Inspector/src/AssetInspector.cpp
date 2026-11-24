#include "Luma/Editor/Panels/AssetInspector.h"

#include <ctime>
#include <string>
#include <vector>

#include "Luma/Asset/AssetData.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Core/Log.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/SlotParameters.h"
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

namespace {

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
        default:
            break;
    }
    const f32 contentH = rows.y + m_assetScroll - contentTop + 12.0f;
    ui.PopClip();
    m_assetScroll = ui.VerticalScroll(Slate::Context::ID("insp.asset.scroll"), contentRegion, contentH, m_assetScroll);
}

}  // namespace Luma::Editor::Panels
