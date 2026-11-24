#include "Luma/Editor/Panels/SlotParameters.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Editor/InspectorDrawers.h"
#include "Luma/MaterialEditors/InstanceParameters.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Icons.h"
#include "PanelContext.h"

namespace Luma::Editor::Panels {
namespace {

using Material::ParameterOverride;
using MaterialCompiler::ParameterInfo;
using MaterialEditors::InstanceParameter;
using MaterialEditors::InstanceParameterModel;

std::string AssetName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// One model per (library, material): it caches the compiled parameter list
// until the root material changes.
InstanceParameterModel& ModelFor(Material::MaterialLibrary& library, const std::string& path) {
    static std::map<std::pair<const Material::MaterialLibrary*, std::string>, std::unique_ptr<InstanceParameterModel>>
        models;
    auto& slot = models[{&library, path}];
    if (!slot) slot = std::make_unique<InstanceParameterModel>(path, library);
    return *slot;
}

std::vector<std::string> TexturePaths(const PanelContext& ctx) {
    std::vector<std::string> out;
    if (!ctx.assetRegistry || !ctx.virtualPathOf) return out;
    for (const AssetData* a : ctx.assetRegistry->FilterByType(AssetType::Texture)) {
        std::string v = ctx.virtualPathOf(a->packagePath);
        if (!v.empty()) out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string ValueText(const InstanceParameter& p) {
    char buf[96];
    switch (p.info.kind) {
        case ParameterInfo::Kind::Scalar:
            std::snprintf(buf, sizeof(buf), "%.3g", p.value.scalar);
            return buf;
        case ParameterInfo::Kind::Vector:
            std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f, %.2f", p.value.vector.x, p.value.vector.y,
                          p.value.vector.z, p.value.vector.w);
            return buf;
        case ParameterInfo::Kind::Texture: return p.value.texture.empty() ? "None" : AssetName(p.value.texture);
        case ParameterInfo::Kind::StaticSwitch: return p.value.staticSwitch ? "On" : "Off";
    }
    return {};
}

// One editable row; returns the edited value through `out` when it changed.
// Vector edits arrive later in the frame (the colour picker), so they write
// through the library directly.
bool EditRow(InspectorRows& rows, Material::MaterialLibrary& library, const std::string& path,
             const InstanceParameter& p, const std::string& key, ParameterOverride& out) {
    out = p.value;
    out.name = p.info.name;
    out.kind = static_cast<ParameterOverride::Kind>(p.info.kind);
    switch (p.info.kind) {
        case ParameterInfo::Kind::Scalar: {
            Slate::SpinBoxOptions o;
            o.speed = 0.01f;
            if (p.info.sliderMax > p.info.sliderMin) {
                o.sliderMin = p.info.sliderMin;
                o.sliderMax = p.info.sliderMax;
            }
            return rows.Float(p.info.name, key, out.scalar, o);
        }
        case ParameterInfo::Kind::Vector: {
            const Math::Vec4 current = p.value.vector;
            Material::MaterialLibrary* lib = &library;
            const std::string name = p.info.name;
            rows.Color(p.info.name, key, {current.x, current.y, current.z},
                       [lib, path, name, alpha = current.w](const Math::Vec3& c) {
                           ParameterOverride o;
                           o.name = name;
                           o.kind = ParameterOverride::Kind::Vector;
                           o.vector = {c.x, c.y, c.z, alpha};
                           InstanceParameterModel(path, *lib).SetValue(o);
                       });
            return false;
        }
        case ParameterInfo::Kind::Texture: {
            std::vector<std::string> paths{""};
            for (std::string& t : TexturePaths(rows.panel)) paths.push_back(std::move(t));
            int current = 0;
            for (usize i = 0; i < paths.size(); ++i) {
                if (paths[i] == p.value.texture) current = static_cast<int>(i);
            }
            if (current == 0 && !p.value.texture.empty()) {
                paths.push_back(p.value.texture);  // a texture the registry doesn't list (yet)
                current = static_cast<int>(paths.size() - 1);
            }
            std::vector<std::string> labels;
            for (const std::string& t : paths) labels.push_back(t.empty() ? std::string("None") : AssetName(t));
            if (!rows.Combo(p.info.name, key, labels, current)) return false;
            out.texture = paths[static_cast<usize>(current)];
            return true;
        }
        case ParameterInfo::Kind::StaticSwitch:
            return rows.Check(p.info.name, key, out.staticSwitch);
    }
    return false;
}

}  // namespace

SlotParameterMode SlotParameterModeFor(const std::string& materialPath, Material::MaterialLibrary& library) {
    if (materialPath.empty()) return SlotParameterMode::None;
    const bool engine = materialPath.rfind("/Engine/", 0) == 0;
    if (library.GetGraphInstance(materialPath)) {
        return engine ? SlotParameterMode::ReadOnly : SlotParameterMode::Instance;
    }
    if (library.GetGraphMaterial(materialPath)) {
        return engine ? SlotParameterMode::ReadOnly : SlotParameterMode::SharedMaterial;
    }
    return SlotParameterMode::None;
}

void DrawSlotParameters(InspectorRows& rows, const std::string& materialPath, u32 slot) {
    Material::MaterialLibrary* library = rows.panel.materials;
    if (!library) return;
    const SlotParameterMode mode = SlotParameterModeFor(materialPath, *library);
    if (mode == SlotParameterMode::None) return;
    InstanceParameterModel& model = ModelFor(*library, materialPath);
    const std::vector<MaterialEditors::ParameterGroup> groups = model.Groups();
    usize count = 0;
    for (const auto& g : groups) count += g.parameters.size();
    if (count == 0) return;

    Slate::Context& ui = rows.ui;
    const Slate::Theme& t = ui.theme();
    static std::map<u64, bool> open;  // per entity + slot; parameters start expanded
    const u64 headerId = rows.Id("insp.mat.params." + std::to_string(slot));
    auto [it, added] = open.try_emplace(headerId, true);
    const Slate::Rect header{rows.x + 12.0f, rows.y, rows.w - 12.0f, 20.0f};
    ui.CollapsingHeader(headerId, header, "Parameters (" + std::to_string(count) + ")", it->second);
    ui.Tooltip(Slate::Rect{header.x, header.y, header.w, header.h},
               mode == SlotParameterMode::Instance
                   ? "Parameters of '" + AssetName(materialPath) + "'\nChanges this instance (every object using it)."
                   : "Parameters of '" + AssetName(materialPath) + "'");
    rows.y += 24.0f;
    if (!it->second) return;

    if (mode == SlotParameterMode::SharedMaterial) {
        rows.Note("Shared material: these are '" + AssetName(materialPath) +
                      "' defaults, used by every object with it. Create Instance (above) gives this object its own.",
                  t.textDim);
    } else if (mode == SlotParameterMode::ReadOnly) {
        rows.Note("Engine material (read-only). Use Create Instance (above) to customize it for this object.",
                  t.warning);
    }
    DrawMaterialParameters(rows, materialPath, "insp.mat.p." + std::to_string(slot));
}

void DrawMaterialParameters(InspectorRows& rows, const std::string& materialPath, const std::string& keyPrefix) {
    Material::MaterialLibrary* library = rows.panel.materials;
    if (!library) return;
    const SlotParameterMode mode = SlotParameterModeFor(materialPath, *library);
    if (mode == SlotParameterMode::None) return;
    InstanceParameterModel& model = ModelFor(*library, materialPath);
    const std::vector<MaterialEditors::ParameterGroup> groups = model.Groups();
    Slate::Context& ui = rows.ui;
    const Slate::Theme& t = ui.theme();
    for (const auto& group : groups) {
        if (group.name != "None") rows.SubHeading(group.name);
        for (const InstanceParameter& p : group.parameters) {
            const std::string key = keyPrefix + "." + p.info.name;
            if (mode == SlotParameterMode::ReadOnly) {
                rows.ReadOnly(p.info.name, ValueText(p));
                continue;
            }
            const f32 rowTop = rows.y;
            ParameterOverride edited;
            if (EditRow(rows, *library, materialPath, p, key, edited)) model.SetValue(edited);
            // An instance's override: reset to the parent's value (Unreal's arrow).
            if (mode == SlotParameterMode::Instance && p.overridden) {
                const Slate::Rect reset{rows.x + rows.NameColumn() - 24.0f, rowTop + 3.0f, 16.0f, 16.0f};
                if (ui.Button(rows.Id(key + ".reset"), reset, {})) model.Reset(p.info.name);
                Slate::DrawIcon(ui, reset.Inset(1.0f, 1.0f), Slate::Icon::Reset, t.warning);
                ui.Tooltip(rows.Id(key + ".reset.tip"), reset, "Reset to the parent's value");
            }
        }
    }
    rows.Space(4.0f);
}

}  // namespace Luma::Editor::Panels
