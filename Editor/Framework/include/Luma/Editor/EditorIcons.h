#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/IconKind.h"
#include "Luma/Slate/Menu.h"
#include "Luma/Slate/Types.h"

// Registries describe icons by name (ComponentDesc::icon / outlinerIcon, entity
// templates, menu entries, panels) so engine modules and plugins don't depend on
// the Slate icon enum or on textures the editor loaded.
//
// A name resolves to a registered editor image (a texture the editor or a plugin
// loaded, with a procedural glyph to use when the texture is missing) or else to
// the procedural Slate icon of that name.
//
// Main-thread only.

namespace Luma::Slate {
class Context;
}

namespace Luma::Editor {

// The Slate procedural icon called `name` ("Cube", "Light", ...); Icon::None when
// the name is empty or unknown.
Slate::Icon IconByName(std::string_view name);

class EditorImageRegistry {
public:
    EditorImageRegistry();
    EditorImageRegistry(const EditorImageRegistry&) = delete;
    EditorImageRegistry& operator=(const EditorImageRegistry&) = delete;

    // `texture` may be 0 (the image failed to load): `fallback` is drawn then.
    RegistrationToken Register(std::string name, TextureHandle texture, Slate::Icon fallback,
                               std::string owner, std::string* error = nullptr);
    // The texture registered as `name` (0 when none / not loaded).
    TextureHandle Find(std::string_view name) const;
    // True when `name` is registered (even with a missing texture).
    bool Contains(std::string_view name) const;
    Slate::Icon Fallback(std::string_view name) const;

private:
    struct Entry {
        u64 handle = 0;
        std::string name;
        TextureHandle texture = 0;
        Slate::Icon fallback = Slate::Icon::None;
        std::string owner;
    };
    const Entry* FindEntry(std::string_view name) const;

    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
};

// The process-wide editor image registry.
EditorImageRegistry& EditorImages();

// Menu icon for `name`: the registered image (with its glyph fallback), else the
// procedural icon of that name; empty when neither exists.
Slate::MenuIcon MenuIconByName(std::string_view name);

// Draws the icon called `name` in `box` (texture tinted by `color`, else its
// glyph). Returns false (draws nothing) when the name resolves to nothing.
bool DrawNamedIcon(Slate::Context& ui, const Slate::Rect& box, std::string_view name,
                   Slate::Color color);

}  // namespace Luma::Editor
