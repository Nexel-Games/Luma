#pragma once

#include <map>
#include <string>

#include "Luma/MaterialEditors/EditorServices.h"
#include "Luma/MaterialEditors/InstanceParameters.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/DockSpace.h"
#include "Luma/MaterialEditors/ColorPopup.h"

namespace Luma::MaterialEditors {

// The Material Instance Editor, in Unreal's layout: a toolbar (Save,
// Browse, compile status), a large lit preview (mesh on a floor, orbit with
// the left mouse) taking ~70% of the width, and Details on the right:
//   General        Parent picker
//   Previewing     preview mesh, floor, sky
//   Parameter Groups  one collapsible section per group; each row has an
//                  override checkbox, the value and a reset-to-parent arrow.
// Edits are parameter-only (the bridge never recompiles for them).
class MaterialInstanceEditor {
public:
    MaterialInstanceEditor(std::string instancePath, Material::MaterialLibrary& library,
                           MaterialEditorServices services);

    void Draw(Slate::Context& ui, const Slate::Rect& rect);

    const std::string& AssetPath() const { return m_model.Path(); }
    std::string Title() const;  // asset name, "*" while dirty
    bool IsDirty() const { return m_model.IsDirty(); }
    bool Save(std::string* error) { return m_model.Save(error); }
    InstanceParameterModel& Model() { return m_model; }

private:
    void DrawToolbar(Slate::Context& ui, const Slate::Rect& bar);
    void DrawPreview(Slate::Context& ui, const Slate::Rect& area);
    void DrawDetails(Slate::Context& ui, const Slate::Rect& area);
    f32 DrawParameter(Slate::Context& ui, const InstanceParameter& p, f32 x, f32 y, f32 w);
    void SaveWithMessage();
    void ApplyParent(const std::string& parent);
    // The UE-style material slot: icon, name, path; arrow picks, double-click opens.
    void DrawParentSlot(Slate::Context& ui, f32 x, f32& y, f32 w);

    InstanceParameterModel m_model;
    MaterialEditorServices m_services;
    PreviewRequest m_preview;
    std::string m_previewMeshLabel = "Sphere";
    bool m_orbiting = false;
    f32 m_split = 0.70f;
    f32 m_scroll = 0.0f;
    bool m_generalOpen = true, m_previewingOpen = true, m_groupsOpen = true;
    std::map<std::string, bool> m_groupOpen;
    std::string m_parentError;
    // Viewport and Details are dock panels (drag tabs to re-dock).
    Slate::DockSpace m_dock;
    Detail::ColorPopup m_colorPopup;  // floating, rounded colour picker
    bool m_dockBuilt = false;
};

}  // namespace Luma::MaterialEditors
