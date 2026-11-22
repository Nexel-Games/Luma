#include "SettingsWindow.h"

#include "Luma/Core/Events.h"
#include "Luma/Core/Log.h"
#include "Luma/RHI/VulkanRenderer.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Image.h"

using namespace Luma;

void SettingsWindow::Close() {
    if (m_renderer) m_renderer->WaitIdle();
    m_ui.reset();
    m_renderer.reset();
    m_window.reset();
}

void SettingsWindow::Tick(EditorScreen& editor, const Slate::Typography& fonts, bool validation,
                          const std::string& icon, const std::string& atlas, f32 dt) {
    if (!editor.SettingsOpen(m_kind)) {
        if (m_window) Close();
        return;
    }
    if (!m_window) {
        WindowProps props{m_kind == Kind::Project ? "Project Settings" : "Editor Preferences", 1000, 720};
        m_window = Window::Create(props);
        RendererConfig config;
        config.appName = props.title;
        config.presentMode = PresentMode::Mailbox;
        config.enableValidation = validation;
        config.enableSceneRendering = false;
        m_renderer = CreateVulkanRenderer(*m_window, config);
        m_ui = std::make_unique<Slate::Context>();
        if (!m_ui->Init(*m_renderer, fonts, m_window->ContentScale())) {
            LUMA_USER_ERROR("Settings", "Could not load fonts for {}", props.title);
            editor.CloseSettings(m_kind);
            Close();
            return;
        }
        Slate::LoadIconAtlas(*m_ui, *m_renderer, atlas);
        std::vector<u8> pixels;
        const auto image = Slate::LoadImage(*m_renderer, icon, &pixels);
        if (!pixels.empty()) {
            m_window->SetIcon(image.width, image.height, pixels.data());
            m_ui->SetBrandIcon(image);
        }
        m_ui->SetClipboardHandlers([this] { return m_window->GetClipboardText(); },
                                  [this](const std::string& s) { m_window->SetClipboardText(s); });
        m_running = true;
        m_window->SetEventCallback([this](Event& e) {
            EventDispatcher(e).Dispatch<WindowResizeEvent>([this](WindowResizeEvent& size) {
                m_renderer->OnResize(size.Width(), size.Height());
                return false;
            });
            m_feed(*m_ui, e, m_running, nullptr);
        });
    }
    if (!m_running || m_window->ShouldClose()) {
        editor.CloseSettings(m_kind);
        if (!editor.SettingsOpen(m_kind)) { Close(); return; }
        m_running = true;
        m_window->SetShouldClose(false);
    }
    if (m_window->IsMinimized() || !m_renderer->BeginFrame()) return;
    const f32 w = static_cast<f32>(m_window->Width()), h = static_cast<f32>(m_window->Height());
    m_ui->BeginFrame(w, h, dt);
    editor.DrawSettings(m_kind, *m_ui, w, h);
    m_renderer->DrawUI(m_ui->EndFrame());
    m_renderer->EndFrame();
    m_window->SetCursor(m_ui->RequestedCursor());
    if (auto pos = m_ui->TakePointerRestore()) {
        m_window->SetPointerLocked(false);
        m_window->SetCursorPosition(pos->x, pos->y);
        m_ui->WarpMouse(pos->x, pos->y);
    } else m_window->SetPointerLocked(m_ui->PointerLockRequested());
}
