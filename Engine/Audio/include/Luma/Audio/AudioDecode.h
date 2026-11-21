#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Luma/Core/Types.h"

namespace Luma::Audio {

struct SoundInfo {
    u32 channels = 0;
    u32 sampleRate = 0;
    u64 frames = 0;
    f64 seconds = 0.0;
};

std::optional<SoundInfo> ProbeSound(const std::filesystem::path& file, std::string* err);

// Min and max of every sample in each column, normalised to [-1, 1].
struct WaveformPeaks {
    std::vector<f32> min;
    std::vector<f32> max;
};

std::optional<WaveformPeaks> ComputePeaks(const std::filesystem::path& file, u32 columns, std::string* err);

}  // namespace Luma::Audio
