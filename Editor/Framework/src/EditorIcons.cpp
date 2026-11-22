#include "Luma/Editor/EditorIcons.h"

#include <algorithm>
#include <utility>

#include "Luma/Core/Log.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor {

Slate::Icon IconByName(std::string_view name) {
    using Slate::Icon;
    // Generated from the icon set (IconSources.inc), so every packed icon
    // is reachable by its role name without a second list to maintain.
    static constexpr std::pair<std::string_view, Icon> kIcons[] = {
#define LUMA_ICON(role, source) {#role, Icon::role},
#include "Luma/Slate/IconSources.inc"
#undef LUMA_ICON
    };
    for (const auto& [n, icon] : kIcons) {
        if (n == name) return icon;
    }
    return Icon::None;
}

EditorImageRegistry::EditorImageRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken EditorImageRegistry::Register(std::string name, TextureHandle texture,
                                                Slate::Icon fallback, std::string owner,
                                                std::string* error) {
    auto fail = [error](std::string message) {
        LUMA_LOG_ERROR("Editor", "{}", message);
        if (error) *error = std::move(message);
        return RegistrationToken{};
    };
    if (name.empty()) return fail("editor image rejected: the name is empty");
    if (owner.empty()) return fail("editor image '" + name + "' rejected: owner is empty");
    if (const Entry* existing = FindEntry(name)) {
        return fail("editor image '" + name + "' is already registered (by '" +
                    existing->owner + "')");
    }
    const u64 handle = m_nextHandle++;
    m_entries.push_back({handle, std::move(name), texture, fallback, std::move(owner)});
    return RegistrationToken(m_alive, [this, handle] {
        std::erase_if(m_entries, [handle](const Entry& e) { return e.handle == handle; });
    });
}

const EditorImageRegistry::Entry* EditorImageRegistry::FindEntry(std::string_view name) const {
    for (const Entry& e : m_entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

TextureHandle EditorImageRegistry::Find(std::string_view name) const {
    const Entry* e = FindEntry(name);
    return e ? e->texture : 0;
}

bool EditorImageRegistry::Contains(std::string_view name) const { return FindEntry(name); }

Slate::Icon EditorImageRegistry::Fallback(std::string_view name) const {
    const Entry* e = FindEntry(name);
    return e ? e->fallback : Slate::Icon::None;
}

EditorImageRegistry& EditorImages() {
    static EditorImageRegistry registry;
    return registry;
}

Slate::MenuIcon MenuIconByName(std::string_view name) {
    const EditorImageRegistry& images = EditorImages();
    if (images.Contains(name)) {
        return Slate::MenuIcon::Texture(images.Find(name), images.Fallback(name));
    }
    return Slate::MenuIcon(IconByName(name));
}

bool DrawNamedIcon(Slate::Context& ui, const Slate::Rect& box, std::string_view name,
                   Slate::Color color) {
    const Slate::MenuIcon icon = MenuIconByName(name);
    if (icon.texture) {
        ui.Image(icon.texture, box, color);
        return true;
    }
    if (icon.glyph != Slate::Icon::None) {
        Slate::DrawIcon(ui, box, icon.glyph, color);
        return true;
    }
    return false;
}

}  // namespace Luma::Editor
