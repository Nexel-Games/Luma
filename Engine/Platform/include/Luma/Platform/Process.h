#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Process utilities used to connect the project browser to the editor: the
// browser relaunches this same executable in editor mode (Godot's model) and
// then exits.

namespace Luma {

// Absolute path to the currently running executable.
std::filesystem::path ExecutablePath();

// Launches `exe` with `args` as a detached process (does not wait). Arguments
// are quoted; UTF-8 is converted to the platform's wide encoding. Returns true
// if the process was started.
bool LaunchDetached(const std::filesystem::path& exe,
                    const std::vector<std::string>& args);

// Opens a file, folder, or URL with the OS shell (https://..., a path, ...).
bool OpenExternal(const std::string& target);

}  // namespace Luma
