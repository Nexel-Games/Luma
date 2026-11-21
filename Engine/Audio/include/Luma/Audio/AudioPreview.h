#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Luma/Core/Types.h"

namespace Luma::Audio {

// Plays one sound at a time. Init failing (no audio device) leaves preview
// unavailable and does not throw.
class AudioPreview {
public:
    AudioPreview();
    ~AudioPreview();
    AudioPreview(const AudioPreview&) = delete;
    AudioPreview& operator=(const AudioPreview&) = delete;

    bool Init(std::string* err);
    void Shutdown();
    bool Play(const std::filesystem::path& file, f32 volume = 1.0f, f32 pitch = 1.0f, bool loop = false);
    void Stop();
    bool IsPlaying() const;
    const std::filesystem::path& Current() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace Luma::Audio
