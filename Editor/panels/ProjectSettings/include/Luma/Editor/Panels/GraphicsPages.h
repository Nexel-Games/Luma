#pragma once

#include <functional>
#include <string>

#include "Luma/Core/Registration.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Slate/Context.h"

// Project Settings pages for the graphics settings (Luma::Graphics), both
// stored in Config/Graphics.json:
//   Engine > Display & Frame Rate  (Engine.Display)   VSync, present mode,
//                                                       frame-rate limits,
//                                                       realtime viewport
//   Engine > Rendering Quality     (Engine.Rendering) tier, render scale, AA,
//                                                       bloom, auto exposure,
//                                                       shadows, culling,
//                                                       texture filtering
// The pages edit the settings objects in place, so the editor applies changes
// on the next frame (texture filtering on the next start).

namespace Luma::Editor::Panels {

// `activePresentMode` (optional) reports the swapchain's actual mode for the
// page's status line.
RegistrationToken RegisterDisplayPage(SettingsPageRegistry& pages, Graphics::DisplaySettings& settings,
                                      std::function<std::string()> activePresentMode = {});
RegistrationToken RegisterRenderingPage(SettingsPageRegistry& pages, Graphics::RenderingSettings& settings);

// Page bodies (exposed for tests); return true when a value changed.
bool DrawDisplayPage(Slate::Context& ui, const Slate::Rect& area, Graphics::DisplaySettings& settings,
                     const std::string& activePresentMode);
bool DrawRenderingPage(Slate::Context& ui, const Slate::Rect& area, Graphics::RenderingSettings& settings);

}  // namespace Luma::Editor::Panels
