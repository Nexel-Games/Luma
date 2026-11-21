#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/AssetImport/ImportSession.h"
#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"

namespace Luma::Editor::Panels {

// Modal import window. The session is owned by the caller and must outlive Close.
class ImportDialog {
public:
    void Open(Import::ImportSession* session, std::filesystem::path destFolder,
              std::string destVirtualPath, bool reimport);
    bool IsOpen() const { return m_open; }

    enum class Action { None, Import, ImportAll, Cancel };

    Action Draw(Slate::Context& ui, f32 screenW, f32 screenH);
    void LoadPresets(const std::filesystem::path& projectConfigDir);
    void SavePreset(const std::string& name);

private:
    struct Row {
        enum class Kind { File, Group, Item };
        Kind kind = Kind::File;
        Import::NodeId node = Import::kNone;
        Import::NodeKind group = Import::NodeKind::Mesh;
        int depth = 0;
        std::string label;
        std::string expandKey;
        std::vector<Import::NodeId> members;
    };

    void Close();
    void RememberLast();
    bool ReadPresets(Luma::SerialValue& root) const;
    void WritePresets(const Luma::SerialValue& root) const;
    std::vector<Row> BuildRows() const;
    bool Expanded(const std::string& key) const;
    void SetExpanded(const std::string& key, bool open);
    static Slate::CheckState Aggregate(const Import::ImportScene& scene,
                                       const std::vector<Import::NodeId>& ids);
    static void Apply(Import::ImportScene& scene, const std::vector<Import::NodeId>& ids, bool enabled);
    static std::string CategoryFor(Import::NodeKind kind);
    static bool ShowCategory(const std::string& category, int meshes, int textures, int sounds);

    Import::ImportSession* m_session = nullptr;
    std::filesystem::path m_dest;
    std::filesystem::path m_configDir;
    std::string m_virtual;
    bool m_reimport = false;
    bool m_open = false;
    bool m_naming = false;
    int m_presetIndex = 0;
    std::string m_presetName;
    std::vector<std::string> m_presetNames;
    std::unordered_map<std::string, bool> m_expanded;
    std::unordered_map<std::string, bool> m_categoryOpen;
    Import::NodeId m_selected = Import::kNone;
    std::string m_scrollCategory;
    f32 m_treeScroll = 0.0f;
    f32 m_settingsScroll = 0.0f;
};

}  // namespace Luma::Editor::Panels
