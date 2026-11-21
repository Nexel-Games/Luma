#include "Luma/Editor/Panels/ImportDialog.h"

#include "Luma/Serialization/Json.h"
#include "Luma/Slate/IconKind.h"
#include "Luma/Slate/Icons.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Luma::Editor::Panels {
namespace {

constexpr f32 kHeaderH = 32.0f;
constexpr f32 kRowH = 22.0f;
constexpr f32 kCheck = 14.0f;
constexpr f32 kIndent = 16.0f;
constexpr f32 kCatH = 24.0f;
constexpr f32 kFieldH = 26.0f;
constexpr f32 kLabelW = 160.0f;
constexpr f32 kButtonW = 84.0f;
constexpr f32 kButtonH = 26.0f;

std::string LowerExt(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

const char* KindTitle(Import::NodeKind kind) {
    switch (kind) {
        case Import::NodeKind::Mesh: return "Meshes";
        case Import::NodeKind::Material: return "Materials";
        case Import::NodeKind::Texture: return "Textures";
        case Import::NodeKind::Sound: return "Sounds";
        case Import::NodeKind::File: return "File";
    }
    return "File";
}

// Slate has no input clipping: a widget drawn outside its scroll area is
// invisible but still takes clicks (e.g. a drop-down under the Import
// button). Rows only exist while fully inside their panel.
bool FullyInside(const Slate::Rect& row, const Slate::Rect& panel) {
    return row.y >= panel.y - 0.5f && row.Bottom() <= panel.Bottom() + 0.5f;
}

Slate::Icon KindIcon(Import::NodeKind kind) {
    switch (kind) {
        case Import::NodeKind::Mesh: return Slate::Icon::StaticMesh;
        case Import::NodeKind::Material: return Slate::Icon::Sphere;
        case Import::NodeKind::Texture: return Slate::Icon::Image;
        case Import::NodeKind::Sound: return Slate::Icon::Play;
        case Import::NodeKind::File: return Slate::Icon::Folder;
    }
    return Slate::Icon::None;
}

Slate::SpinBoxOptions FieldSpin(const Import::SettingField& field) {
    Slate::SpinBoxOptions options;
    if (field.max > field.min) {
        options.min = field.min;
        options.max = field.max;
    }
    if (field.step > 0.0) options.delta = field.step;
    options.speed = 0.02;
    return options;
}

}  // namespace

void ImportDialog::Open(Import::ImportSession* session, std::filesystem::path destFolder,
                        std::string destVirtualPath, bool reimport) {
    m_session = session;
    m_dest = std::move(destFolder);
    m_virtual = std::move(destVirtualPath);
    m_reimport = reimport;
    m_open = true;
    m_naming = false;
    m_presetIndex = 0;
    m_presetName.clear();
    m_selected = Import::kNone;
    m_scrollCategory.clear();
    m_treeScroll = 0.0f;
    m_settingsScroll = 0.0f;
    m_expanded.clear();
    m_categoryOpen.clear();
}

void ImportDialog::Close() { m_open = false; }

bool ImportDialog::Expanded(const std::string& key) const {
    const auto it = m_expanded.find(key);
    return it == m_expanded.end() || it->second;
}

void ImportDialog::SetExpanded(const std::string& key, bool open) { m_expanded[key] = open; }

Slate::CheckState ImportDialog::Aggregate(const Import::ImportScene& scene,
                                          const std::vector<Import::NodeId>& ids) {
    int on = 0;
    int off = 0;
    for (Import::NodeId id : ids) {
        if (id >= scene.nodes.size()) continue;
        const Import::ImportNode& node = scene.nodes[id];
        if (!node.error.empty()) continue;
        if (node.enabled) ++on;
        else ++off;
    }
    if (on == 0) return Slate::CheckState::Unchecked;
    if (off == 0) return Slate::CheckState::Checked;
    return Slate::CheckState::Undetermined;
}

void ImportDialog::Apply(Import::ImportScene& scene, const std::vector<Import::NodeId>& ids, bool enabled) {
    for (Import::NodeId id : ids) {
        if (id >= scene.nodes.size()) continue;
        Import::ImportNode& node = scene.nodes[id];
        if (!node.error.empty()) {
            node.enabled = false;
            continue;
        }
        node.enabled = enabled;
    }
}

std::string ImportDialog::CategoryFor(Import::NodeKind kind) {
    switch (kind) {
        case Import::NodeKind::Mesh: return "Meshes";
        case Import::NodeKind::Material: return "Materials";
        case Import::NodeKind::Texture: return "Textures";
        case Import::NodeKind::Sound: return "Audio";
        case Import::NodeKind::File: return "Common";
    }
    return "Common";
}

bool ImportDialog::ShowCategory(const std::string& category, int meshes, int textures, int sounds) {
    if (category == "Meshes" || category == "Materials") return meshes > 0;
    if (category == "Textures") return textures > 0;
    if (category == "Audio") return sounds > 0;
    return true;
}

std::vector<ImportDialog::Row> ImportDialog::BuildRows() const {
    std::vector<Row> rows;
    if (m_session == nullptr) return rows;
    const Import::ImportScene& scene = m_session->Scene();
    const Import::NodeKind kinds[] = {Import::NodeKind::Mesh, Import::NodeKind::Material,
                                      Import::NodeKind::Texture, Import::NodeKind::Sound};
    for (Import::NodeId id = 0; id < scene.nodes.size(); ++id) {
        const Import::ImportNode& node = scene.nodes[id];
        if (node.kind != Import::NodeKind::File) continue;
        Row file;
        file.kind = Row::Kind::File;
        file.node = id;
        file.depth = 0;
        file.label = node.name.empty() ? node.sourceFile.filename().string() : node.name;
        file.expandKey = "file:" + std::to_string(id);
        const std::vector<Import::NodeId> children = scene.Children(id);
        for (Import::NodeKind kind : kinds) {
            for (Import::NodeId child : children) {
                if (scene.nodes[child].kind == kind) file.members.push_back(child);
            }
        }
        rows.push_back(file);
        if (!Expanded(file.expandKey)) continue;
        for (Import::NodeKind kind : kinds) {
            std::vector<Import::NodeId> members;
            for (Import::NodeId child : children) {
                if (scene.nodes[child].kind == kind) members.push_back(child);
            }
            if (members.empty()) continue;
            Row group;
            group.kind = Row::Kind::Group;
            group.group = kind;
            group.depth = 1;
            group.label = std::string(KindTitle(kind)) + " (" + std::to_string(members.size()) + ")";
            group.expandKey = "group:" + std::to_string(id) + ":" + KindTitle(kind);
            group.members = members;
            rows.push_back(group);
            if (!Expanded(group.expandKey)) continue;
            for (Import::NodeId child : members) {
                Row item;
                item.kind = Row::Kind::Item;
                item.node = child;
                item.group = kind;
                item.depth = 2;
                item.label = scene.nodes[child].name;
                item.members.push_back(child);
                rows.push_back(std::move(item));
            }
        }
    }
    return rows;
}

bool ImportDialog::ReadPresets(SerialValue& root) const {
    const std::filesystem::path path = m_configDir / "ImportPresets.json";
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    const auto parsed = ParseJson(text.str());
    if (!parsed || !parsed->IsObject()) return false;
    root = *parsed;
    return true;
}

void ImportDialog::WritePresets(const SerialValue& root) const {
    if (m_configDir.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(m_configDir, error);
    std::ofstream out(m_configDir / "ImportPresets.json", std::ios::binary | std::ios::trunc);
    if (!out) return;
    const std::string json = WriteJson(root, true);
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
}

void ImportDialog::RememberLast() {
    // A reimport runs with the asset's own recorded settings (and Overwrite);
    // those are not the user's choice for the next import.
    if (m_reimport) return;
    if (m_session == nullptr || m_configDir.empty()) return;
    SerialValue root = SerialValue::MakeObject();
    ReadPresets(root);
    if (!root.IsObject()) root = SerialValue::MakeObject();
    root["__last"] = m_session->Settings().ToJson();
    WritePresets(root);
}

void ImportDialog::LoadPresets(const std::filesystem::path& projectConfigDir) {
    m_configDir = projectConfigDir;
    m_presetNames.clear();
    SerialValue root;
    if (!ReadPresets(root)) return;
    if (const SerialValue* presets = root.Find("presets"); presets != nullptr && presets->IsObject()) {
        for (const auto& member : presets->Members()) m_presetNames.push_back(member.first);
    }
    if (const SerialValue* last = root.Find("__last"); last != nullptr && m_session != nullptr) {
        m_session->Settings().FromJson(*last);
    }
}

void ImportDialog::SavePreset(const std::string& name) {
    if (m_session == nullptr || m_configDir.empty() || name.empty() || name == "Default" ||
        name == "Save Preset…") {
        return;
    }
    SerialValue root = SerialValue::MakeObject();
    ReadPresets(root);
    if (!root.IsObject()) root = SerialValue::MakeObject();
    const SerialValue stack = m_session->Settings().ToJson();
    root["presets"][name] = stack;
    root["__last"] = stack;
    WritePresets(root);
    m_presetNames.clear();
    if (const SerialValue* presets = root.Find("presets"); presets != nullptr && presets->IsObject()) {
        for (const auto& member : presets->Members()) m_presetNames.push_back(member.first);
    }
    const auto it = std::find(m_presetNames.begin(), m_presetNames.end(), name);
    m_presetIndex = it == m_presetNames.end() ? 0 : static_cast<int>(it - m_presetNames.begin()) + 1;
}

ImportDialog::Action ImportDialog::Draw(Slate::Context& ui, f32 screenW, f32 screenH) {
    if (!m_open || m_session == nullptr) return Action::None;

    std::string title = "Import Content";
    if (m_reimport) {
        std::string name = "Asset";
        for (const Import::ImportNode& node : m_session->Scene().nodes) {
            if (node.kind == Import::NodeKind::File && !node.name.empty()) {
                name = node.name;
                break;
            }
        }
        title = "Reimport " + name;
    }
    const f32 modalW = std::min(960.0f, screenW * 0.9f);
    const f32 modalH = std::min(620.0f, screenH * 0.9f);
    const Slate::Rect content = ui.BeginModal(Slate::Context::ID("import.dialog"), title, {modalW, modalH});

    if (!ui.textFieldFocused()) {
        if (ui.keyEscape()) {
            ui.EndModal();
            Close();
            return Action::Cancel;
        }
        if (ui.enterPressed()) {
            RememberLast();
            ui.EndModal();
            Close();
            return Action::Import;
        }
    }

    const Slate::Theme& theme = ui.theme();
    ui.Label({content.x, content.y + (kHeaderH - 16.0f) * 0.5f}, "Destination", theme.textDim);
    ui.Label({content.x + 96.0f, content.y + (kHeaderH - 16.0f) * 0.5f}, m_virtual, theme.text);

    std::vector<std::string> presetItems;
    presetItems.emplace_back("Default");
    presetItems.insert(presetItems.end(), m_presetNames.begin(), m_presetNames.end());
    presetItems.emplace_back("Save Preset…");
    const Slate::Rect presetRect{content.Right() - 220.0f, content.y + 4.0f, 220.0f, 24.0f};
    if (m_naming) {
        const Slate::TextResult typed = ui.EditableText(Slate::Context::ID("import.preset.name"), presetRect, m_presetName);
        if (typed.commit == Slate::TextCommit::OnEnter && !m_presetName.empty()) {
            SavePreset(m_presetName);
            m_naming = false;
        } else if (typed.commit == Slate::TextCommit::OnCleared) {
            m_naming = false;
        }
    } else {
        const int picked = ui.Combo(Slate::Context::ID("import.preset"), presetRect, presetItems, m_presetIndex);
        if (picked != m_presetIndex) {
            if (picked == static_cast<int>(presetItems.size()) - 1) {
                m_naming = true;
                m_presetName.clear();
            } else if (picked == 0) {
                for (Import::SettingsBlock* block : m_session->Settings().Blocks()) block->ResetToDefaults();
                m_presetIndex = 0;
            } else if (picked > 0) {
                SerialValue root;
                if (ReadPresets(root)) {
                    if (const SerialValue* presets = root.Find("presets")) {
                        if (const SerialValue* preset = presets->Find(presetItems[static_cast<size_t>(picked)])) {
                            m_session->Settings().FromJson(*preset);
                        }
                    }
                }
                m_presetIndex = picked;
            }
        }
    }

    int meshes = 0;
    int textures = 0;
    int sounds = 0;
    for (const Import::ImportNode& node : m_session->Scene().nodes) {
        if (node.kind == Import::NodeKind::Mesh) ++meshes;
        else if (node.kind == Import::NodeKind::Texture) ++textures;
        else if (node.kind == Import::NodeKind::Sound) ++sounds;
    }

    f32 bodyY = content.y + kHeaderH;
    f32 bodyH = content.h - kHeaderH;
    const Import::ImportPlan plan = m_session->Plan(m_dest);
    int conflicts = 0;
    for (const Import::AssetRequest& request : plan.requests) {
        if (request.exists) ++conflicts;
    }
    if (conflicts > 0) {
        const Slate::Rect strip{content.x, bodyY, content.w, 28.0f};
        const std::string label = std::to_string(conflicts) + (conflicts == 1 ? " asset already exists" : " assets already exist");
        ui.Label({strip.x, strip.y + 6.0f}, label, theme.text);
        Import::SettingsBlock& common = m_session->Settings().Common();
        const std::string current = common.GetString("conflict");
        std::vector<std::string> policies = {"Overwrite", "Skip", "Keep Both"};
        int index = 2;
        for (int i = 0; i < static_cast<int>(policies.size()); ++i) {
            if (policies[static_cast<size_t>(i)] == current) index = i;
        }
        const Slate::Rect combo{strip.Right() - 180.0f, strip.y + 2.0f, 180.0f, 24.0f};
        const int picked = ui.Combo(Slate::Context::ID("import.conflict"), combo, policies, index);
        if (picked >= 0 && picked < static_cast<int>(policies.size())) {
            common.Set("conflict", policies[static_cast<size_t>(picked)]);
        }
        bodyY += 28.0f;
        bodyH -= 28.0f;
    }

    const f32 leftW = content.w * 0.38f;
    const Slate::Rect tree{content.x, bodyY, leftW, std::max(0.0f, bodyH)};
    const Slate::Rect settings{tree.Right() + 8.0f, bodyY, std::max(0.0f, content.Right() - tree.Right() - 8.0f), tree.h};

    const std::vector<Row> rows = BuildRows();
    const f32 treeContent = static_cast<f32>(rows.size()) * kRowH;
    m_treeScroll = ui.VerticalScroll(Slate::Context::ID("import.tree.scroll"), tree, treeContent, m_treeScroll);
    ui.drawList().PushClip(tree);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const f32 y = tree.y - m_treeScroll + static_cast<f32>(i) * kRowH;
        const Slate::Rect rowRect{tree.x, y, tree.w, kRowH};
        if (!FullyInside(rowRect, tree)) continue;
        const f32 checkX = rowRect.x + static_cast<f32>(row.depth) * kIndent;
        const Slate::Rect box{checkX, y + (kRowH - kCheck) * 0.5f, kCheck, kCheck};
        const bool errored = row.kind == Row::Kind::Item && row.node < m_session->Scene().nodes.size() &&
                             !m_session->Scene().nodes[row.node].error.empty();
        Slate::CheckState state = Aggregate(m_session->Scene(), row.members);
        if (errored) state = Slate::CheckState::Unchecked;
        if (ui.Checkbox(Slate::Context::ID("import.check." + std::to_string(i) + "." + row.label), box, "", state)) {
            Apply(m_session->Scene(), row.members, state == Slate::CheckState::Checked);
            if (errored && row.node < m_session->Scene().nodes.size()) m_session->Scene().nodes[row.node].enabled = false;
        }
        if (errored) {
            ui.Tooltip(box, m_session->Scene().nodes[row.node].error);
            ui.Label({box.Right() + 8.0f, y + (kRowH - 16.0f) * 0.5f}, row.label, theme.error);
            continue;
        }
        const Slate::Rect nodeRect{box.Right() + 4.0f, y, std::max(0.0f, rowRect.Right() - box.Right() - 4.0f), kRowH};
        if (row.kind == Row::Kind::Item) {
            // Leaves have nothing to expand: same layout as the group rows
            // (icon and label columns line up), without the disclosure arrow.
            const bool selected = m_selected == row.node;
            const bool clicked = ui.Selectable(Slate::Context::ID("import.node." + std::to_string(row.node)), nodeRect,
                                               "", selected);
            const f32 indent = theme.space.md;
            const f32 icon = theme.size.iconMd;
            Slate::DrawIcon(ui, {nodeRect.x + indent + theme.space.sm, y + (kRowH - icon) * 0.5f, icon, icon},
                            KindIcon(row.group), selected ? theme.selectionText : theme.text);
            const f32 textX = nodeRect.x + indent + icon + theme.space.sm + icon + theme.space.sm;
            ui.Label({textX, y + (kRowH - 16.0f) * 0.5f}, row.label, selected ? theme.selectionText : theme.text);
            if (clicked) {
                m_selected = row.node;
                m_scrollCategory = CategoryFor(row.group);
                m_settingsScroll = -1.0f;
            }
        } else {
            bool open = Expanded(row.expandKey);
            ui.TreeNode(Slate::Context::ID("import.expand." + row.expandKey), nodeRect, row.label,
                        row.kind == Row::Kind::File ? Slate::Icon::Folder : KindIcon(row.group), open, 0, false);
            SetExpanded(row.expandKey, open);
        }
    }
    ui.drawList().PopClip();

    f32 contentY = 0.0f;
    std::vector<Import::SettingsBlock*> blocks = m_session->Settings().Blocks();
    auto visibleFields = [](Import::SettingsBlock& block) {
        std::vector<const Import::SettingField*> fields;
        for (const Import::SettingField& field : block.Fields()) {
            if (block.IsVisible(field)) fields.push_back(&field);
        }
        return fields;
    };
    std::vector<std::pair<Import::SettingsBlock*, std::vector<const Import::SettingField*>>> shown;
    for (Import::SettingsBlock* block : blocks) {
        if (block->Fields().empty()) continue;
        const std::string category = block->Fields().front().category;
        if (!ShowCategory(category, meshes, textures, sounds)) continue;
        auto fields = visibleFields(*block);
        contentY += kCatH + static_cast<f32>(fields.size()) * kFieldH;
        shown.push_back({block, std::move(fields)});
    }
    const f32 settingsContent = contentY;
    if (m_settingsScroll < 0.0f && !m_scrollCategory.empty()) {
        f32 y = 0.0f;
        for (const auto& entry : shown) {
            if (entry.first->Fields().front().category == m_scrollCategory) {
                m_settingsScroll = y;
                break;
            }
            y += kCatH + static_cast<f32>(entry.second.size()) * kFieldH;
        }
        if (m_settingsScroll < 0.0f) m_settingsScroll = 0.0f;
    }
    m_settingsScroll = ui.VerticalScroll(Slate::Context::ID("import.settings.scroll"), settings, settingsContent, m_settingsScroll);

    ui.drawList().PushClip(settings);
    f32 y = settings.y - m_settingsScroll;
    for (auto& entry : shown) {
        Import::SettingsBlock& block = *entry.first;
        const std::string category = block.Fields().front().category;
        auto openIt = m_categoryOpen.find(category);
        if (openIt == m_categoryOpen.end()) openIt = m_categoryOpen.emplace(category, true).first;
        const Slate::Rect header{settings.x, y, settings.w, kCatH};
        if (FullyInside(header, settings)) {
            ui.CollapsingHeader(Slate::Context::ID("import.cat." + category), header, category, openIt->second);
        }
        y += kCatH;
        if (!openIt->second) continue;
        for (const Import::SettingField* fieldPtr : entry.second) {
            const Import::SettingField& field = *fieldPtr;
            const Slate::Rect row{settings.x, y, settings.w, kFieldH};
            if (!FullyInside(row, settings)) {
                y += kFieldH;
                continue;
            }
            const Slate::Rect fieldRect = ui.PropertyRow(row, field.label, kLabelW);
            const std::string idKey = category + "/" + field.key;
            const u64 id = Slate::Context::ID(idKey);
            if (!field.tooltip.empty()) ui.Tooltip(id, row, field.tooltip);
            switch (field.type) {
                case Import::FieldType::Bool: {
                    bool value = block.GetBool(field.key);
                    if (ui.Checkbox(id, {fieldRect.x, fieldRect.y, kCheck, kCheck}, "", value)) block.Set(field.key, value);
                    break;
                }
                case Import::FieldType::Float: {
                    f32 value = static_cast<f32>(block.GetFloat(field.key));
                    if (ui.SpinBox(id, fieldRect, value, FieldSpin(field)).changed) block.Set(field.key, static_cast<f64>(value));
                    break;
                }
                case Import::FieldType::Int: {
                    i32 value = static_cast<i32>(block.GetInt(field.key));
                    if (ui.SpinBoxInt(id, fieldRect, value, FieldSpin(field)).changed) block.Set(field.key, static_cast<i64>(value));
                    break;
                }
                case Import::FieldType::Enum: {
                    std::vector<std::string> items = field.enumValues;
                    const std::string current = block.GetString(field.key);
                    int index = 0;
                    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                        if (items[static_cast<size_t>(i)] == current) index = i;
                    }
                    const int picked = ui.Combo(id, fieldRect, items, index);
                    if (picked >= 0 && picked < static_cast<int>(items.size())) block.Set(field.key, items[static_cast<size_t>(picked)]);
                    break;
                }
                case Import::FieldType::Vec3: {
                    const Math::Vec3 value = block.GetVec3(field.key);
                    f32 xyz[3] = {value.x, value.y, value.z};
                    if (ui.VectorField(id, fieldRect, xyz, 3, FieldSpin(field)).changed) {
                        SerialValue array = SerialValue::MakeArray();
                        array.PushBack(xyz[0]);
                        array.PushBack(xyz[1]);
                        array.PushBack(xyz[2]);
                        block.Set(field.key, std::move(array));
                    }
                    break;
                }
                case Import::FieldType::String: {
                    std::string value = block.GetString(field.key);
                    if (ui.EditableText(id, fieldRect, value).changed) block.Set(field.key, value);
                    break;
                }
            }
            y += kFieldH;
        }
    }
    ui.drawList().PopClip();

    const f32 footerY = content.Bottom() + theme.space.lg;
    if (ui.Button(Slate::Context::ID("import.reset"), {content.x, footerY, 150.0f, kButtonH}, "Reset to Defaults")) {
        for (Import::SettingsBlock* block : m_session->Settings().Blocks()) block->ResetToDefaults();
        m_presetIndex = 0;
    }

    std::unordered_map<std::string, int> extensions;
    bool importAll = false;
    for (const Import::ImportNode& node : m_session->Scene().nodes) {
        if (node.kind != Import::NodeKind::File) continue;
        const int count = ++extensions[LowerExt(node.sourceFile)];
        if (count > 1) importAll = true;
    }
    f32 x = content.Right() - kButtonW;
    if (ui.Button(Slate::Context::ID("import.cancel"), {x, footerY, kButtonW, kButtonH}, "Cancel")) {
        ui.EndModal();
        Close();
        return Action::Cancel;
    }
    x -= theme.space.md + kButtonW;
    if (importAll) {
        if (ui.Button(Slate::Context::ID("import.all"), {x, footerY, kButtonW, kButtonH}, "Import All")) {
            RememberLast();
            ui.EndModal();
            Close();
            return Action::ImportAll;
        }
        x -= theme.space.md + kButtonW;
    }
    if (ui.Button(Slate::Context::ID("import.go"), {x, footerY, kButtonW, kButtonH}, "Import")) {
        RememberLast();
        ui.EndModal();
        Close();
        return Action::Import;
    }
    return Action::None;
}

}  // namespace Luma::Editor::Panels
