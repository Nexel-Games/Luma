#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Slate/Menu.h"

// The editor's main menu bar as a registry: menus by id ("File", "Edit",
// "Assets", "Tools", "Window", plugin menus) in `order`, each holding entries and
// dynamic sections grouped by section heading. The bar shows every registered
// menu that has entries. Built-in commands register exactly like plugin ones.
//
// Main-thread only.

namespace Luma::Editor {

struct MenuDesc {
    std::string id;     // "File"
    std::string label;  // shown in the bar
    i32 order = 0;      // bar position (ascending)
    std::string owner;  // "Engine" or the plugin id
};

struct MenuEntryDesc {
    std::string id;       // unique ("File.SaveScene")
    std::string menu;     // MenuDesc::id it belongs to (must be registered)
    std::string section;  // section heading inside the menu
    std::string label;
    std::string icon;     // icon name (see EditorIcons.h)
    std::string tooltip;
    std::string shortcut; // key hint only; the shortcut itself is handled elsewhere
    std::string owner;
    i32 order = 0;        // position in the menu (ascending); sections follow their entries
    std::function<void()> execute;
    std::function<bool()> isEnabled;  // optional; default enabled
    std::function<bool()> isChecked;  // optional; default unchecked
};

// A section whose entries are produced when the menu opens (e.g. one toggle per
// registered panel).
struct MenuSectionDesc {
    std::string id;
    std::string menu;
    std::string section;
    std::string owner;
    i32 order = 0;
    std::function<void(Slate::MenuBuilder&)> fill;
};

class MenuRegistry {
public:
    MenuRegistry();
    MenuRegistry(const MenuRegistry&) = delete;
    MenuRegistry& operator=(const MenuRegistry&) = delete;

    RegistrationToken RegisterMenu(MenuDesc menu, std::string* error = nullptr);
    RegistrationToken RegisterEntry(MenuEntryDesc entry, std::string* error = nullptr);
    RegistrationToken RegisterSection(MenuSectionDesc section, std::string* error = nullptr);

    // Registered menus sorted by order (ties: registration order).
    std::vector<const MenuDesc*> Menus() const;
    const MenuDesc* FindMenu(std::string_view id) const;
    // True when the menu has at least one entry or dynamic section.
    bool HasEntries(std::string_view menuId) const;
    // The menu's entries: entries and dynamic sections sorted by order (ties:
    // registration order), each new section heading starting a new section.
    std::vector<Slate::MenuEntry> Build(std::string_view menuId) const;

private:
    struct Item {
        u64 handle = 0;
        u64 sequence = 0;
        bool isSection = false;
        MenuEntryDesc entry;
        MenuSectionDesc dynamic;
    };
    struct MenuItem {
        u64 handle = 0;
        u64 sequence = 0;
        MenuDesc desc;
    };
    RegistrationToken MakeToken(u64 handle);

    std::vector<MenuItem> m_menus;
    std::vector<Item> m_items;
    std::shared_ptr<void> m_alive;
    u64 m_next = 1;
};

// The process-wide editor menu registry.
MenuRegistry& Menus();

}  // namespace Luma::Editor
