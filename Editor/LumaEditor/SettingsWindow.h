#pragma once

#include "EditorScreen.h"
#include "Luma/Platform/Window.h"

// A modeless OS window. Each surface owns its Slate context and UI-only renderer.
// The settings model and its persistence remain owned by the main EditorScreen.
namespace Luma {
class SettingsWindow {
public:
    using Kind = EditorScreen::SettingsKind;
    using EventFeed = void (*)(Luma::Slate::Context&, Luma::Event&, bool&, EditorScreen*);
    SettingsWindow(Kind kind, EventFeed feed) : m_kind(kind), m_feed(feed) {}
    void Tick(EditorScreen& editor, const Luma::Slate::Typography& fonts, bool validation,
              const std::string& icon, const std::string& atlas, Luma::f32 dt);
    bool Focused() const { return m_window && m_window->IsFocused(); }

private:
    void Close();
    Kind m_kind;
    EventFeed m_feed;
    std::unique_ptr<Luma::Window> m_window;
    std::unique_ptr<Luma::Renderer> m_renderer;
    std::unique_ptr<Luma::Slate::Context> m_ui;
    bool m_running = false;
};
}  // namespace Luma
