#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "PanelContext.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// World Outliner panel — lists every named entity in the Scene, drives
// selection (which feeds the Inspector + the gizmo via the shared
// PanelContext). Pure UI: owns no scene state. A \"+\" button in the panel
// header opens a Create GameObject menu (Empty / primitives / Light >
// directional, point, spot, tube / Environment) that spawns via the
// PanelContext::onCreateActor hook.

namespace Luma::Editor::Panels {

class WorldOutlinerPanel {
public:
    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    // Sets the Create-button texture drawn in the panel header. A zero handle
    // falls back to the procedural plus glyph.
    void SetCreateButtonIcon(TextureHandle icon) { m_createButtonIcon = icon; }

    // Sets the search-glass texture for the menu's search box (Unreal icon
    // pack). A zero handle falls back to the procedural search glyph.
    void SetSearchGlassIcon(TextureHandle icon) { m_texSearchGlass = icon; }

    // Clears the cached selection hint (called after the scene is reloaded
    // so the panel doesn't reference an entity that no longer exists).
    void ClearSelection() {
        m_pressedRow = 0;
        m_renaming = kNullEntity;
        m_renameFocusPending = false;
        m_collapsed.clear();
    }

    // Unreal "Place Actors" menu: one submenu per EntityTemplates() category;
    // picking an entry calls PanelContext::onCreateEntity with the template id.
    std::vector<Slate::MenuEntry> BuildPlaceActorsMenu(PanelContext& ctx);
    // Right-click menu for one entity row.
    std::vector<Slate::MenuEntry> BuildEntityMenu(Entity e, PanelContext& ctx);
    std::vector<Slate::MenuEntry> BuildEmptyMenu(PanelContext& ctx);

private:
    void FillPlaceActors(Slate::MenuBuilder& b,
                         const std::function<void(const std::string&)>& create);

    std::string m_entityFilter; // filters the outliner entity list by name
    u64 m_pressedRow = 0;       // row/button id currently mouse-held
    // Inline rename (F2 / context menu "Rename").
    std::unordered_set<Entity> m_collapsed;
    Entity m_renaming = kNullEntity;
    std::string m_renameBuf;
    bool m_renameFocusPending = false;
    f32 m_listScroll = 0.0f;             // entity-list vertical scroll offset
    TextureHandle m_createButtonIcon = 0;   // header create-button texture
    TextureHandle m_texSearchGlass = 0;     // search-box leading icon texture
};

}  // namespace Luma::Editor::Panels
