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
    // falls back to the pack plus glyph.
    void SetCreateButtonIcon(TextureHandle icon) { m_createButtonIcon = icon; }

    // Sets the search-glass texture for the menu's search box (Unreal icon
    // pack). A zero handle falls back to the pack search glyph.
    void SetSearchGlassIcon(TextureHandle icon) { m_texSearchGlass = icon; }

    // Sets the category-row textures (Geometry / Lights / Environment —
    // Unreal-style silhouettes, drawn gray). Zero handles fall back to the
    // pack glyphs.
    void SetCategoryIcons(TextureHandle geometry, TextureHandle light,
                          TextureHandle environment) {
        m_texCatGeometry = geometry;
        m_texCatLight = light;
        m_texCatEnvironment = environment;
    }

    // Sets the primitive-actor textures for the Geometry submenu rows
    // (Unreal primitive thumbnails, drawn gray). Zero handles fall back to
    // the pack glyphs.
    void SetPrimitiveIcons(TextureHandle cube, TextureHandle plane,
                           TextureHandle sphere, TextureHandle cylinder) {
        m_texActorCube = cube;
        m_texActorPlane = plane;
        m_texActorSphere = sphere;
        m_texActorCylinder = cylinder;
    }

    // Sets the outliner actor icons (light types + a generic mesh) shown
    // beside entity names in the list. Zero handles fall back to no icon.
    void SetOutlinerActorIcons(TextureHandle dirLight, TextureHandle pointLight,
                               TextureHandle spotLight, TextureHandle mesh) {
        m_texActorDirLight = dirLight;
        m_texActorPointLight = pointLight;
        m_texActorSpotLight = spotLight;
        m_texActorMesh = mesh;
    }

    // Clears the cached selection hint (called after the scene is reloaded
    // so the panel doesn't reference an entity that no longer exists).
    void ClearSelection() {}

private:
    // Unreal "Place Actors" menu (categories as submenus; searchable).
    std::vector<Slate::MenuEntry> BuildPlaceActorsMenu(PanelContext& ctx);
    void FillPlaceActors(Slate::MenuBuilder& b,
                         const std::function<void(CreateActorKind)>& create);
    // Right-click menu for one entity row.
    std::vector<Slate::MenuEntry> BuildEntityMenu(Entity e, PanelContext& ctx);
    std::vector<Slate::MenuEntry> BuildEmptyMenu(PanelContext& ctx);

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
    TextureHandle m_texCatGeometry = 0;     // Geometry category-row icon
    TextureHandle m_texCatLight = 0;        // Lights category-row icon
    TextureHandle m_texCatEnvironment = 0;  // Environment category-row icon
    TextureHandle m_texActorCube = 0;       // Cube primitive row icon
    TextureHandle m_texActorPlane = 0;      // Plane primitive row icon
    TextureHandle m_texActorSphere = 0;     // Sphere primitive row icon
    TextureHandle m_texActorCylinder = 0;   // Cylinder primitive row icon
    TextureHandle m_texActorDirLight = 0;   // Directional-light row icon
    TextureHandle m_texActorPointLight = 0; // Point-light row icon
    TextureHandle m_texActorSpotLight = 0;  // Spot-light row icon
    TextureHandle m_texActorMesh = 0;       // Static mesh actor row icon (stacked blocks)
};

}  // namespace Luma::Editor::Panels
