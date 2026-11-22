#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <variant>

#include "Luma/Editor/Panels/Viewport.h"
#include "Luma/Gizmo/TransformGizmo.h"

namespace Luma::Editor::Panels {

enum class PreferenceSection { Display, PostProcessing, Camera, Translation, Rotation, Scale, Count };

struct PreferenceRow {
    const wchar_t* key;
    const char* label;
    PreferenceSection section;
    std::variant<bool*, i32*, f32*> value;
    f32 minimum = 0, maximum = 1;
    const char* suffix = "";
    // Stored values use the engine's native units. FOV is edited in degrees.
    bool angleInRadians = false;

    double Read() const;
    void Set(double number) const;
};

// Binds directly to the values used by the viewport and the transform gizmo.
// The editor owns those objects for the lifetime of this panel.
class PreferencesPanel {
public:
    PreferencesPanel(ViewportSettings& viewport, GizmoSnap& snap, std::filesystem::path file);
    static std::filesystem::path UserFile();
    bool Draw(Slate::Context& ui, f32 width, f32 height);
    void SaveChanges(bool retry = false);

private:
    std::array<PreferenceRow, 12> m_rows;
    std::array<double, 12> m_saved{}, m_attempted{};
    std::filesystem::path m_file;
    bool m_saveFailed = false;
    int m_category = 0;
    std::string m_search;
    std::array<bool, static_cast<usize>(PreferenceSection::Count)> m_expanded;
    f32 m_scroll = 0;
};

}  // namespace Luma::Editor::Panels
