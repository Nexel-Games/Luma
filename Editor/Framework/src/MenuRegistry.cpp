#include "Luma/Editor/MenuRegistry.h"

#include <algorithm>

#include "Luma/Core/Log.h"
#include "Luma/Editor/EditorIcons.h"

namespace Luma::Editor {

namespace {

RegistrationToken Fail(std::string* error, std::string message) {
    LUMA_LOG_ERROR("Editor", "{}", message);
    if (error) *error = std::move(message);
    return {};
}

}  // namespace

MenuRegistry::MenuRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken MenuRegistry::MakeToken(u64 handle) {
    return RegistrationToken(m_alive, [this, handle] {
        std::erase_if(m_menus, [handle](const MenuItem& m) { return m.handle == handle; });
        std::erase_if(m_items, [handle](const Item& i) { return i.handle == handle; });
    });
}

RegistrationToken MenuRegistry::RegisterMenu(MenuDesc menu, std::string* error) {
    if (menu.id.empty()) return Fail(error, "menu rejected: the id is empty");
    if (menu.owner.empty()) return Fail(error, "menu '" + menu.id + "' rejected: owner is empty");
    if (const MenuDesc* existing = FindMenu(menu.id)) {
        return Fail(error, "menu '" + menu.id + "' is already registered (by '" +
                               existing->owner + "')");
    }
    if (menu.label.empty()) menu.label = menu.id;
    const u64 handle = m_next++;
    m_menus.push_back({handle, handle, std::move(menu)});
    return MakeToken(handle);
}

RegistrationToken MenuRegistry::RegisterEntry(MenuEntryDesc entry, std::string* error) {
    if (entry.id.empty()) return Fail(error, "menu entry rejected: the id is empty");
    const std::string who = "menu entry '" + entry.id + "'";
    if (entry.owner.empty()) return Fail(error, who + " rejected: owner is empty");
    if (entry.label.empty()) return Fail(error, who + " rejected: label is empty");
    if (!entry.execute) return Fail(error, who + " rejected: missing execute callback");
    if (!FindMenu(entry.menu)) {
        return Fail(error, who + " rejected: menu '" + entry.menu + "' is not registered");
    }
    for (const Item& i : m_items) {
        const std::string& id = i.isSection ? i.dynamic.id : i.entry.id;
        if (id == entry.id) return Fail(error, who + " is already registered");
    }
    const u64 handle = m_next++;
    Item item;
    item.handle = handle;
    item.sequence = handle;
    item.entry = std::move(entry);
    m_items.push_back(std::move(item));
    return MakeToken(handle);
}

RegistrationToken MenuRegistry::RegisterSection(MenuSectionDesc section, std::string* error) {
    if (section.id.empty()) return Fail(error, "menu section rejected: the id is empty");
    const std::string who = "menu section '" + section.id + "'";
    if (section.owner.empty()) return Fail(error, who + " rejected: owner is empty");
    if (!section.fill) return Fail(error, who + " rejected: missing fill callback");
    if (!FindMenu(section.menu)) {
        return Fail(error, who + " rejected: menu '" + section.menu + "' is not registered");
    }
    for (const Item& i : m_items) {
        const std::string& id = i.isSection ? i.dynamic.id : i.entry.id;
        if (id == section.id) return Fail(error, who + " is already registered");
    }
    const u64 handle = m_next++;
    Item item;
    item.handle = handle;
    item.sequence = handle;
    item.isSection = true;
    item.dynamic = std::move(section);
    m_items.push_back(std::move(item));
    return MakeToken(handle);
}

std::vector<const MenuDesc*> MenuRegistry::Menus() const {
    std::vector<const MenuItem*> sorted;
    for (const MenuItem& m : m_menus) sorted.push_back(&m);
    std::stable_sort(sorted.begin(), sorted.end(), [](const MenuItem* a, const MenuItem* b) {
        return a->desc.order < b->desc.order;
    });
    std::vector<const MenuDesc*> out;
    for (const MenuItem* m : sorted) out.push_back(&m->desc);
    return out;
}

const MenuDesc* MenuRegistry::FindMenu(std::string_view id) const {
    for (const MenuItem& m : m_menus) {
        if (m.desc.id == id) return &m.desc;
    }
    return nullptr;
}

bool MenuRegistry::HasEntries(std::string_view menuId) const {
    for (const Item& i : m_items) {
        if ((i.isSection ? i.dynamic.menu : i.entry.menu) == menuId) return true;
    }
    return false;
}

std::vector<Slate::MenuEntry> MenuRegistry::Build(std::string_view menuId) const {
    std::vector<const Item*> items;
    for (const Item& i : m_items) {
        if ((i.isSection ? i.dynamic.menu : i.entry.menu) == menuId) items.push_back(&i);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item* a, const Item* b) {
        const i32 oa = a->isSection ? a->dynamic.order : a->entry.order;
        const i32 ob = b->isSection ? b->dynamic.order : b->entry.order;
        return oa < ob;
    });

    Slate::MenuBuilder b;
    bool started = false;
    std::string section;
    for (const Item* i : items) {
        const std::string& s = i->isSection ? i->dynamic.section : i->entry.section;
        if (!started || s != section) {
            b.BeginSection(s);
            section = s;
            started = true;
        }
        if (i->isSection) {
            i->dynamic.fill(b);
            continue;
        }
        const MenuEntryDesc& e = i->entry;
        Slate::MenuEntryOptions o;
        o.tooltip = e.tooltip;
        o.shortcut = e.shortcut;
        o.enabled = e.isEnabled ? e.isEnabled() : true;
        o.checked = e.isChecked ? e.isChecked() : false;
        b.AddEntry(e.label, MenuIconByName(e.icon), e.execute, o);
    }
    return b.Build();
}

MenuRegistry& Menus() {
    static MenuRegistry registry;
    return registry;
}

}  // namespace Luma::Editor
