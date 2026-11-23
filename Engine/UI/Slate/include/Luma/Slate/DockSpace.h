#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Serialization/SerialValue.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Luma::Slate::DockSpace - a dockable panel layout. Panels are registered with a
// draw callback and arranged in a tree of split nodes and tabbed leaves. Tabs
// can be dragged to re-dock (tabify onto another panel, or split a region on any
// side). Its own Slate feature, in its own files.

namespace Luma::Slate {

enum class DockDir { Center, Left, Right, Up, Down };

class DockSpace {
public:
    using PanelDrawer = std::function<void(Context&, const Rect&)>;

    // Registers a panel and its content drawer. `icon` (optional) is drawn
    // before the title in the panel's tab, Unreal-style.
    void AddPanel(const std::string& id, const std::string& title,
                  PanelDrawer drawer, TextureHandle icon = 0);

    // --- Layout builder (also used by drag-docking) ------------------------
    void DockRoot(const std::string& id);
    // Splits the region containing `targetId`, placing `id` on `dir`; `ratio` is
    // the fraction given to the side `id` lands on.
    void DockWith(const std::string& id, const std::string& targetId,
                  DockDir dir, f32 ratio);

    // --- Runtime panels (e.g. one Material Editor tab per asset) ------------
    bool HasPanel(const std::string& id) const { return m_panels.count(id) != 0; }
    void SetTitle(const std::string& id, const std::string& title);
    // Makes `id` the active tab of its leaf.
    void FocusPanel(const std::string& id);
    // Makes `id` closable (middle click on its tab; tabs show no close button). `onClose` decides: return
    // true to close now, false to keep the tab (e.g. while a Save / Discard /
    // Cancel prompt is open; the owner calls ClosePanel later).
    void SetCloseHandler(const std::string& id, std::function<bool()> onClose);
    // Removes the panel and its tab (applied at the start of the next Draw,
    // so it is safe to call from inside the panel's own drawer).
    void ClosePanel(const std::string& id);

    // Lays out + draws the whole dock space (tab bars, splitters, panels) and
    // handles tab drag-docking.
    void Draw(Context& ctx, const Rect& area);

    // --- Layout persistence ------------------------------------------------
    // Serializes the split/tab tree (structure, ratios, active tabs) so the
    // exact arrangement can be restored. Empty when nothing is docked.
    SerialValue SaveLayout() const;
    // Rebuilds the tree from `layout`, keeping only tabs whose id is in
    // `allowed` (currently-open panels); any allowed panel the layout does not
    // place is appended as a tab so no open panel disappears.
    void ApplyLayout(const SerialValue& layout, const std::vector<std::string>& allowed);
    // Fired after the user re-docks a panel or finishes dragging a splitter, so
    // the owner can persist the new layout.
    std::function<void()> onLayoutChanged;

private:
    struct Node {
        bool isLeaf = true;
        // Leaf:
        std::vector<std::string> tabs;
        int active = 0;
        // Split:
        bool horizontal = true;  // true: side-by-side (vertical divider)
        f32 ratio = 0.5f;
        std::unique_ptr<Node> a, b;
        // Computed each frame:
        Rect rect{};
    };

    struct PanelInfo {
        std::string title;
        PanelDrawer drawer;
        TextureHandle icon = 0;
        std::function<bool()> onClose;  // set = closable tab
    };

    struct PendingDock {
        bool active = false;
        std::string panel;
        Node* target = nullptr;
        DockDir dir = DockDir::Center;
        bool edge = false;  // dock against the whole workspace edge (split root)
    };

    Node* FindLeafWithPanel(Node* n, const std::string& id) const;
    Node* LeafUnderPoint(Node* n, Vec2 p) const;
    std::unique_ptr<Node>* FindSlot(std::unique_ptr<Node>& slot, Node* target);
    void RemovePanel(const std::string& id);
    static bool Cleanup(std::unique_ptr<Node>& slot);
    static void SplitSlot(std::unique_ptr<Node>& slot, DockDir dir,
                          const std::string& panelId, f32 ratio);

    void DrawNode(Context& ctx, Node* n);
    void DrawLeaf(Context& ctx, Node* n);
    void ApplyPending();
    static SerialValue SaveNode(const Node* n);
    static std::unique_ptr<Node> LoadNode(const SerialValue& v,
                                          const std::unordered_set<std::string>& allowed);

    std::unordered_map<std::string, PanelInfo> m_panels;
    std::unique_ptr<Node> m_root;

    // Tab drag state.
    std::string m_dragPanel;
    bool m_dragging = false;
    Vec2 m_dragStart;
    PendingDock m_pending;
    std::vector<std::string> m_pendingClose;

    // Layout-change tracking (drives onLayoutChanged once per change).
    bool m_layoutDirty = false;
    bool m_splitterDragging = false;
    bool m_splitterWasDragging = false;
};

}  // namespace Luma::Slate
