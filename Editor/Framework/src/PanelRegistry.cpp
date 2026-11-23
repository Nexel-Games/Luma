#include "Luma/Editor/PanelRegistry.h"

#include <algorithm>

#include "Luma/Core/Log.h"
#include "Luma/Editor/EditorIcons.h"
#include "Luma/Slate/Menu.h"

namespace Luma::Editor {

namespace {

bool Contains(const std::vector<std::string>& v, std::string_view id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

void EraseId(std::vector<std::string>& v, std::string_view id) {
    std::erase_if(v, [id](const std::string& s) { return s == id; });
}

void AddId(std::vector<std::string>& v, std::string_view id) {
    if (!Contains(v, id)) v.emplace_back(id);
}

}  // namespace

// --- PanelRegistry ----------------------------------------------------------------

PanelRegistry::PanelRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken PanelRegistry::Register(PanelDesc panel, std::string* error) {
    auto fail = [error](std::string message) {
        LUMA_LOG_ERROR("Editor", "{}", message);
        if (error) *error = std::move(message);
        return RegistrationToken{};
    };
    if (panel.id.empty()) return fail("editor panel rejected: the id is empty");
    const std::string who = "editor panel '" + panel.id + "'";
    if (panel.owner.empty()) return fail(who + " rejected: owner is empty");
    if (panel.title.empty()) return fail(who + " rejected: title is empty");
    if (!panel.draw) return fail(who + " rejected: missing draw callback");
    if (const PanelDesc* existing = Find(panel.id)) {
        return fail(who + " is already registered (by '" + existing->owner + "')");
    }
    const u64 handle = m_nextHandle++;
    m_entries.push_back({handle, std::make_unique<PanelDesc>(std::move(panel))});
    ++m_revision;
    return RegistrationToken(m_alive, [this, handle] {
        auto it = std::find_if(m_entries.begin(), m_entries.end(),
                               [handle](const Entry& e) { return e.handle == handle; });
        if (it == m_entries.end()) return;
        m_entries.erase(it);
        ++m_revision;
    });
}

const PanelDesc* PanelRegistry::Find(std::string_view id) const {
    for (const Entry& e : m_entries) {
        if (e.desc->id == id) return e.desc.get();
    }
    return nullptr;
}

std::vector<const PanelDesc*> PanelRegistry::All() const {
    std::vector<const PanelDesc*> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.push_back(e.desc.get());
    return out;
}

PanelRegistry& EditorPanels() {
    static PanelRegistry registry;
    return registry;
}

// --- EditorPanelHost --------------------------------------------------------------

EditorPanelHost::EditorPanelHost(PanelRegistry& registry, Slate::DockSpace& dock,
                                 ContextProvider context)
    : m_registry(registry), m_dock(dock), m_context(std::move(context)) {
    // Re-docking a panel or dragging a splitter persists the new layout.
    m_dock.onLayoutChanged = [this] { NotifyChanged(); };
}

bool EditorPanelHost::WantsOpen(const PanelDesc& panel) const {
    if (!panel.closable) return true;
    if (panel.openByDefault) return !Contains(m_closedByUser, panel.id);
    return Contains(m_openedByUser, panel.id);
}

void EditorPanelHost::SetOverride(const std::string& id, bool open, bool isDefault) {
    EraseId(m_closedByUser, id);
    EraseId(m_openedByUser, id);
    if (open == isDefault) return;  // matches the default: nothing to remember
    AddId(open ? m_openedByUser : m_closedByUser, id);
}

void EditorPanelHost::NotifyChanged() {
    if (onStateChanged) onStateChanged();
}

void EditorPanelHost::Dock(const PanelDesc& panel) {
    const std::string id = panel.id;
    PanelRegistry* registry = &m_registry;
    ContextProvider* context = &m_context;
    m_dock.AddPanel(
        id, panel.title,
        [registry, context, id](Slate::Context& ui, const Slate::Rect& rect) {
            // Look the panel up each frame: it may have been unregistered.
            const PanelDesc* desc = registry->Find(id);
            if (!desc) return;
            Panels::PanelContext ctx = (*context) ? (*context)() : Panels::PanelContext{};
            desc->draw(ui, rect, ctx);
        },
        EditorImages().Find(panel.icon));
    if (panel.closable) {
        m_dock.SetCloseHandler(id, [this, id] {
            // The tab's close button: the dock removes it at its next Draw.
            EraseId(m_open, id);
            AddId(m_closePending, id);
            if (const PanelDesc* desc = m_registry.Find(id)) {
                SetOverride(id, false, desc->openByDefault);
            }
            NotifyChanged();
            return true;
        });
    }

    if (m_open.empty()) {
        m_dock.DockRoot(id);
    } else if (!panel.dockNextTo.empty() && IsOpen(panel.dockNextTo) &&
               !Contains(m_deferredOpen, panel.dockNextTo)) {
        m_dock.DockWith(id, panel.dockNextTo, panel.dockDir, panel.dockRatio);
    } else {
        m_dock.DockWith(id, m_open.front(), Slate::DockDir::Center, 0.5f);
    }
    m_open.push_back(id);
}

void EditorPanelHost::BuildLayout() {
    m_known.clear();
    for (const PanelDesc* panel : m_registry.All()) {
        m_known.push_back(panel->id);
        if (WantsOpen(*panel) && !IsOpen(panel->id)) Dock(*panel);
    }
    // Restore the saved split/tab arrangement over the freshly docked defaults.
    if (m_hasSavedDock) m_dock.ApplyLayout(m_savedDock, m_open);
}

void EditorPanelHost::ResetLayout() {
    for (const std::string& id : m_open) m_dock.ClosePanel(id);
    m_dock = Slate::DockSpace{};
    m_open.clear();
    m_closePending.clear();
    m_deferredOpen.clear();
    m_closedByUser.clear();
    m_openedByUser.clear();
    m_savedDock = SerialValue{};  // drop the persisted layout: reset to defaults
    m_hasSavedDock = false;
    BuildLayout();
    NotifyChanged();
}

void EditorPanelHost::Sync() {
    // Closes requested last frame were applied by the dock's Draw.
    m_closePending.clear();
    std::vector<std::string> deferred;
    deferred.swap(m_deferredOpen);
    for (const std::string& id : deferred) {
        if (const PanelDesc* panel = m_registry.Find(id); panel && !IsOpen(id)) Dock(*panel);
    }

    // Unregistered panels leave the dock.
    std::vector<std::string> gone;
    for (const std::string& id : m_open) {
        if (!m_registry.Find(id)) gone.push_back(id);
    }
    for (const std::string& id : gone) {
        m_dock.ClosePanel(id);
        EraseId(m_open, id);
        AddId(m_closePending, id);
    }
    // Newly registered panels open per their default / saved state.
    std::vector<std::string> known;
    for (const PanelDesc* panel : m_registry.All()) {
        known.push_back(panel->id);
        if (!Contains(m_known, panel->id) && WantsOpen(*panel) && !IsOpen(panel->id)) {
            if (Contains(m_closePending, panel->id)) {
                AddId(m_deferredOpen, panel->id);
            } else {
                Dock(*panel);
            }
        }
    }
    m_known = std::move(known);
}

bool EditorPanelHost::IsOpen(std::string_view id) const {
    return Contains(m_open, id) || Contains(m_deferredOpen, id);
}

void EditorPanelHost::Open(std::string_view id) {
    const PanelDesc* panel = m_registry.Find(id);
    if (!panel) return;
    if (IsOpen(id)) {
        m_dock.FocusPanel(std::string(id));
        return;
    }
    if (Contains(m_closePending, id)) {
        AddId(m_deferredOpen, id);  // the dock still has to drop the old tab
    } else {
        Dock(*panel);
    }
    SetOverride(panel->id, true, panel->openByDefault);
    NotifyChanged();
}

void EditorPanelHost::Close(std::string_view id) {
    const PanelDesc* panel = m_registry.Find(id);
    if (!panel || !panel->closable) return;
    if (Contains(m_deferredOpen, id)) {
        EraseId(m_deferredOpen, id);
    } else if (Contains(m_open, id)) {
        m_dock.ClosePanel(std::string(id));
        EraseId(m_open, id);
        AddId(m_closePending, id);
    } else {
        return;
    }
    SetOverride(panel->id, false, panel->openByDefault);
    NotifyChanged();
}

void EditorPanelHost::Toggle(std::string_view id) {
    if (IsOpen(id)) {
        Close(id);
    } else {
        Open(id);
    }
}

std::vector<std::string> EditorPanelHost::ClosedPanels() const { return m_closedByUser; }

SerialValue EditorPanelHost::SaveState() const {
    SerialValue state = SerialValue::MakeObject();
    SerialValue closed = SerialValue::MakeArray();
    for (const std::string& id : m_closedByUser) closed.PushBack(SerialValue{id});
    SerialValue opened = SerialValue::MakeArray();
    for (const std::string& id : m_openedByUser) opened.PushBack(SerialValue{id});
    state["closed"] = std::move(closed);
    state["opened"] = std::move(opened);
    state["dock"] = m_dock.SaveLayout();
    return state;
}

void EditorPanelHost::LoadState(const SerialValue& state) {
    m_closedByUser.clear();
    m_openedByUser.clear();
    auto read = [&state](std::string_view key, std::vector<std::string>& out) {
        const SerialValue* list = state.Find(key);
        if (!list || !list->IsArray()) return;
        for (const SerialValue& v : list->Elements()) {
            if (v.IsString() && !v.AsString().empty()) AddId(out, v.AsString());
        }
    };
    read("closed", m_closedByUser);
    read("opened", m_openedByUser);
    if (const SerialValue* dock = state.Find("dock"); dock && dock->IsObject()) {
        m_savedDock = *dock;
        m_hasSavedDock = true;
    }
}

void EditorPanelHost::FillWindowMenu(Slate::MenuBuilder& builder) {
    for (const PanelDesc* panel : m_registry.All()) {
        if (!panel->showInWindowMenu) continue;
        Slate::MenuEntryOptions o;
        o.checked = IsOpen(panel->id);
        o.enabled = panel->closable || !o.checked;
        o.tooltip = panel->closable ? panel->tooltip
                                    : panel->title + " is always open.";
        const std::string id = panel->id;
        builder.AddEntry(panel->title, MenuIconByName(panel->icon), [this, id] { Toggle(id); },
                         o);
    }
}

}  // namespace Luma::Editor
