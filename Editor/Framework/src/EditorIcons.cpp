#include "Luma/Editor/EditorIcons.h"

#include <utility>

namespace Luma::Editor {

Slate::Icon IconByName(std::string_view name) {
    using Slate::Icon;
    static constexpr std::pair<std::string_view, Icon> kIcons[] = {
        {"ChevronRight", Icon::ChevronRight}, {"ChevronDown", Icon::ChevronDown},
        {"ChevronUp", Icon::ChevronUp},       {"Search", Icon::Search},
        {"Gear", Icon::Gear},                 {"Folder", Icon::Folder},
        {"FolderOpen", Icon::FolderOpen},     {"Eye", Icon::Eye},
        {"EyeOff", Icon::EyeOff},             {"Lock", Icon::Lock},
        {"Plus", Icon::Plus},                 {"Close", Icon::Close},
        {"Check", Icon::Check},               {"Save", Icon::Save},
        {"Play", Icon::Play},                 {"Pause", Icon::Pause},
        {"Stop", Icon::Stop},                 {"Grip", Icon::Grip},
        {"Dot", Icon::Dot},                   {"Trash", Icon::Trash},
        {"Camera", Icon::Camera},             {"Light", Icon::Light},
        {"Cube", Icon::Cube},                 {"Sphere", Icon::Sphere},
        {"Plane", Icon::Plane},               {"Cylinder", Icon::Cylinder},
        {"Refresh", Icon::Refresh},           {"ArrowLeft", Icon::ArrowLeft},
        {"Reset", Icon::Reset},               {"Image", Icon::Image},
    };
    for (const auto& [n, icon] : kIcons) {
        if (n == name) return icon;
    }
    return Icon::None;
}

}  // namespace Luma::Editor
