#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Serialization/SerialValue.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/DockSpace.h"
#include "PanelContext.h"

// Editor panels (dock tabs) as a registry. Each registered panel has a stable id,
// a title and icon, a draw callback and a default dock placement. EditorPanelHost
// turns the registry into DockSpace tabs, keeps them in sync while panels are
// registered / unregistered (plugins), opens and closes them from the Window
// menu, and persists which panels are open, keyed by panel id.
//
// Main-thread only.

namespace Luma::Editor {

struct PanelDesc {
    std::string id;       // stable ("outliner"); also the persistence key
    std::string title;    // tab title
    std::string icon;     // icon name (EditorImages / Slate icon), optional
    std::string tooltip;  // Window-menu tooltip
    std::string owner;    // "Engine" or the plugin id
    std::function<void(Slate::Context&, const Slate::Rect&, Panels::PanelContext&)> draw;

    // Default placement: next to the panel `dockNextTo` (empty = the dock root),
    // on side `dockDir` taking `dockRatio` of that region. When `dockNextTo` is
    // not open, the panel is tabbed with the first open panel.
    std::string dockNextTo;
    Slate::DockDir dockDir = Slate::DockDir::Center;
    f32 dockRatio = 0.25f;

    bool openByDefault = true;
    bool closable = true;          // tab close button + Window-menu toggle
    bool showInWindowMenu = true;
};

class PanelRegistry {
public:
    PanelRegistry();
    PanelRegistry(const PanelRegistry&) = delete;
    PanelRegistry& operator=(const PanelRegistry&) = delete;

    RegistrationToken Register(PanelDesc panel, std::string* error = nullptr);
    const PanelDesc* Find(std::string_view id) const;
    std::vector<const PanelDesc*> All() const;  // registration order
    u64 Revision() const { return m_revision; }

private:
    struct Entry {
        u64 handle = 0;
        std::unique_ptr<PanelDesc> desc;
    };
    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
    u64 m_revision = 0;
};

// The process-wide editor panel registry.
PanelRegistry& EditorPanels();

// Hosts the registered panels in a DockSpace.
class EditorPanelHost {
public:
    using ContextProvider = std::function<Panels::PanelContext()>;

    EditorPanelHost(PanelRegistry& registry, Slate::DockSpace& dock, ContextProvider context);

    // Builds the layout: every registered panel that should be open (its default,
    // overridden by the loaded state) is docked at its default placement, in
    // registration order.
    void BuildLayout();
    // Forgets the open/closed state and rebuilds the default layout. The dock is
    // cleared first.
    void ResetLayout();
    // Follows registry changes: newly registered panels open per their default /
    // saved state; unregistered panels are closed. Call once per frame, after
    // DockSpace::Draw (the dock applies tab closes during its Draw).
    void Sync();

    bool IsOpen(std::string_view id) const;
    void Open(std::string_view id);   // docks (or focuses) the panel
    void Close(std::string_view id);  // no-op for non-closable panels
    void Toggle(std::string_view id);

    // Panel ids the user closed (persisted) and opened beyond their defaults.
    std::vector<std::string> ClosedPanels() const;

    // Persisted state: { "closed": [ids], "opened": [ids] } — panels whose open
    // state differs from their default. Unknown ids are kept (a plugin panel
    // stays closed after the plugin returns).
    SerialValue SaveState() const;
    void LoadState(const SerialValue& state);

    // Called when the user changes which panels are open (to persist it).
    std::function<void()> onStateChanged;

    // Window-menu section filling one toggle per registered panel.
    void FillWindowMenu(Slate::MenuBuilder& builder);

private:
    void Dock(const PanelDesc& panel);
    bool WantsOpen(const PanelDesc& panel) const;
    void SetOverride(const std::string& id, bool open, bool isDefault);
    void NotifyChanged();

    PanelRegistry& m_registry;
    Slate::DockSpace& m_dock;
    ContextProvider m_context;
    std::vector<std::string> m_open;          // docked panel ids, in dock order
    std::vector<std::string> m_closedByUser;  // overrides: default open, user closed
    std::vector<std::string> m_openedByUser;  // overrides: default closed, user opened
    std::vector<std::string> m_known;         // panels seen by the last Sync/BuildLayout
    std::vector<std::string> m_closePending;  // closed; the dock drops them at its next Draw
    std::vector<std::string> m_deferredOpen;  // reopened before that happened
};

}  // namespace Luma::Editor
