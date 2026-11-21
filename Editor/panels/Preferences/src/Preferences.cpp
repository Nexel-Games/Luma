#include "Luma/Editor/Panels/Preferences.h"

#include <charconv>
#include <cmath>
#include <format>
#include <type_traits>
#include <windows.h>
#include <shlobj.h>

#include "Luma/Core/Log.h"

namespace Luma::Editor::Panels {

double PreferenceRow::Read() const {
    return std::visit([](auto* field) { return static_cast<double>(*field); }, value);
}

void PreferenceRow::Set(double number) const {
    if (!std::isfinite(number) || number < minimum || number > maximum) return;
    std::visit([number](auto* field) {
        using T = std::remove_reference_t<decltype(*field)>;
        if constexpr (std::is_integral_v<T>) {
            if (std::trunc(number) != number) return;
        }
        *field = static_cast<T>(number);
    }, value);
}

std::filesystem::path PreferencesPanel::UserFile() {
    PWSTR folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder);
    std::filesystem::path path;
    if (SUCCEEDED(result)) path = std::filesystem::path(folder) / L"LumaEngine" / L"EditorPreferences.ini";
    CoTaskMemFree(folder);
    return path;
}

PreferencesPanel::PreferencesPanel(ViewportSettings& vp, GizmoSnap& snap, std::filesystem::path file)
    : m_rows{{
          {L"Grid", "Show grid", PreferenceSection::Display, &vp.showGrid},
          {L"Stats", "Show frame rate and timing", PreferenceSection::Display, &vp.showStats},
          {L"Bloom", "Bloom", PreferenceSection::PostProcessing, &vp.showBloom},
          {L"EyeAdaptation", "Automatic exposure", PreferenceSection::PostProcessing, &vp.showEyeAdaptation},
          {L"CameraSpeed", "Camera speed", PreferenceSection::Camera, &vp.cameraSpeed, 1, 8},
          {L"FieldOfView", "Vertical field of view", PreferenceSection::Camera, &vp.fovYRadians,
              Math::Radians(30.0f), Math::Radians(120.0f), " deg", true},
          {L"SnapTranslation", "Enable translation snapping", PreferenceSection::Translation, &snap.translate},
          {L"TranslationStep", "Translation increment", PreferenceSection::Translation, &snap.translateStep, 0.001f, 1000, " m"},
          {L"SnapRotation", "Enable rotation snapping", PreferenceSection::Rotation, &snap.rotate},
          {L"RotationStep", "Rotation increment", PreferenceSection::Rotation, &snap.rotateStepDeg, 0.1f, 180, " deg"},
          {L"SnapScale", "Enable scale snapping", PreferenceSection::Scale, &snap.scale},
          {L"ScaleStep", "Scale increment", PreferenceSection::Scale, &snap.scaleStep, 0.001f, 10},
      }}, m_file(std::move(file)) {
    m_expanded.fill(true);
    for (usize i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows[i];
        wchar_t buffer[64]{};
        const DWORD length = m_file.empty() ? 0 : GetPrivateProfileStringW(
            L"Editor", row.key, L"", buffer, static_cast<DWORD>(std::size(buffer)), m_file.c_str());
        if (length > 0 && length < std::size(buffer) - 1) {
            char raw[64]{};
            for (DWORD n = 0; n < length; ++n) {
                raw[n] = buffer[n] <= 127 ? static_cast<char>(buffer[n]) : '?';
            }
            double number{};
            auto [end, error] = std::from_chars(raw, raw + length, number);
            if (error == std::errc{} && end == raw + length) row.Set(number);
        }
        m_saved[i] = m_attempted[i] = row.Read();
    }
}

void PreferencesPanel::SaveChanges(bool retry) {
    bool failed = false;
    for (usize i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows[i];
        const double current = row.Read();
        if (current == m_saved[i]) {
            m_attempted[i] = current;
            continue;
        }
        if (!retry && m_attempted[i] == current) {
            failed = true;
            continue;
        }
        m_attempted[i] = current;
        std::error_code ec;
        if (!m_file.empty()) std::filesystem::create_directories(m_file.parent_path(), ec);
        // Update only this key so other editor windows keep their preferences.
        if (m_file.empty() || ec || !WritePrivateProfileStringW(
                L"Editor", row.key, std::format(L"{}", current).c_str(), m_file.c_str())) {
            failed = true;
        } else {
            m_saved[i] = current;
        }
    }
    if (failed && !m_saveFailed) LUMA_USER_ERROR("Preferences", "Could not save editor preferences.");
    m_saveFailed = failed;
}

}  // namespace Luma::Editor::Panels
