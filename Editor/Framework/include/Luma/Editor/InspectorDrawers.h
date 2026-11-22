#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Math/Math.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Slate/ColorPickerPopup.h"
#include "Luma/Slate/Context.h"
#include "PanelContext.h"

// Inspector (Details panel) drawers: how the Inspector draws one component type.
//
// Every registered component (ComponentRegistry) is drawn by the Inspector. A
// component with a drawer registered here is drawn by that drawer; any other
// component gets the default reflection-driven drawer (one row per reflected
// property, grouped by category, edits written back through the component's
// read/write callbacks). Built-in components register their custom drawers
// through this same registry, exactly like plugins.
//
// Main-thread only.

namespace Luma::Editor {

// Floating color pickers used by Inspector rows, keyed by row id. The Inspector
// draws their panels after the dock (so they are not clipped to its column).
class InspectorColorPickers {
public:
    // The picker for `rowId`, created on first use and marked used this frame.
    Slate::ColorPickerPopup& Use(u64 rowId);
    // Starts a frame of Draw: pickers not used during it are closed afterwards.
    void BeginFrame();
    // Draws the open picker panels used by the last Draw.
    void DrawPanels(Slate::Context& ui);
    void CloseAll();

private:
    std::map<u64, std::unique_ptr<Slate::ColorPickerPopup>> m_pickers;
    std::vector<u64> m_used;
};

// Row layout helper handed to drawers: a vertical cursor plus Unreal-style
// labelled property rows. Every row id mixes in the entity, so typed text never
// carries over to another actor.
class InspectorRows {
public:
    InspectorRows(Slate::Context& ui, Panels::PanelContext& panel, Scene& scene, Entity entity,
                  f32 x, f32 y, f32 w, InspectorColorPickers& pickers);

    Slate::Context& ui;
    Panels::PanelContext& panel;
    Scene& scene;
    Entity entity;
    f32 x;
    f32 y;  // cursor: the top of the next row
    f32 w;
    // Mesh Renderer material slot rows laid out this frame (drop targets).
    std::vector<Slate::Rect>* materialSlotRects = nullptr;

    static constexpr f32 kLabelWidth = 118.0f;
    static constexpr f32 kRowHeight = 22.0f;
    static constexpr f32 kRowStep = 28.0f;

    // Widget id for `key` on this row's entity.
    u64 Id(std::string_view key) const { return RowId(key, entity); }
    static u64 RowId(std::string_view key, Entity entity);

    void SubHeading(std::string_view text);
    void Space(f32 px) { y += px; }
    // Each editor returns true when the value changed this frame.
    bool Float(std::string_view label, std::string_view key, f32& value,
               const Slate::SpinBoxOptions& options = {});
    bool Int(std::string_view label, std::string_view key, i32& value,
             const Slate::SpinBoxOptions& options = {});
    bool Vec3(std::string_view label, std::string_view key, Math::Vec3& value);
    bool Check(std::string_view label, std::string_view key, bool& value, f32 indent = 0.0f);
    bool Combo(std::string_view label, std::string_view key,
               const std::vector<std::string>& items, int& current);
    bool Text(std::string_view label, std::string_view key, std::string& value);
    // A swatch that opens a floating color picker. `set` is called with each
    // change (later in the frame, when the picker panel draws), so it must look
    // the component up again rather than hold a reference to it.
    void Color(std::string_view label, std::string_view key, const Math::Vec3& current,
               std::function<void(const Math::Vec3&)> set);
    void ReadOnly(std::string_view label, std::string_view value);
    void Note(std::string_view text, Slate::Color color);

private:
    InspectorColorPickers& m_pickers;
};

struct InspectorDrawer {
    std::string componentId;  // ComponentDesc::id this drawer is for
    std::string owner;        // "Engine" or the plugin id
    // Hidden components get no Inspector section (e.g. Name: the header shows it).
    bool hidden = false;
    std::function<void(InspectorRows&)> draw;  // required unless hidden
};

class InspectorDrawerRegistry {
public:
    InspectorDrawerRegistry();
    InspectorDrawerRegistry(const InspectorDrawerRegistry&) = delete;
    InspectorDrawerRegistry& operator=(const InspectorDrawerRegistry&) = delete;

    // One drawer per component id; a duplicate or invalid drawer is rejected.
    RegistrationToken Register(InspectorDrawer drawer, std::string* error = nullptr);
    const InspectorDrawer* Find(std::string_view componentId) const;
    std::vector<const InspectorDrawer*> All() const;
    u64 Revision() const { return m_revision; }

private:
    struct Entry {
        u64 handle = 0;
        std::unique_ptr<InspectorDrawer> drawer;
    };
    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
    u64 m_revision = 0;
};

// The process-wide drawer registry.
InspectorDrawerRegistry& InspectorDrawers();

// --- Default (reflection) drawer --------------------------------------------------

struct ReflectedRow {
    enum class Kind { Bool, Int, Float, String, Vec3, Color, Enum, StringList, Other };
    usize property = 0;     // index into the component's TypeInfoBase
    std::string name;       // serialized property name
    std::string label;      // display label
    std::string category;   // display category ("" = none)
    std::string tooltip;
    Kind kind = Kind::Other;
    bool readOnly = false;
};

// The rows the default drawer shows for `desc`, given the component's current
// serialized payload (the payload decides the value kind; metadata refines it).
std::vector<ReflectedRow> BuildReflectedRows(const ComponentDesc& desc, const SerialValue& payload);

// Draws `desc` on rows.entity from its reflection, writing edits back through
// desc.read. Components without reflection show a read-only note.
void DrawReflectedComponent(InspectorRows& rows, const ComponentDesc& desc);

// "RigidBody (plugin Luma.Physics.PhysX not loaded) — data kept"
std::string MissingComponentLabel(const UnresolvedComponent& component);

}  // namespace Luma::Editor
