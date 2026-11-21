#include "Luma/Audio/AudioDecode.h"

#include "Luma/Core/Log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace Luma::Audio {
namespace {

void SetError(std::string* err, const std::string& message) {
    if (err != nullptr) {
        *err = message;
    }
}

ma_result OpenDecoder(const std::filesystem::path& file, ma_decoder* decoder) {
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
#if defined(_WIN32)
    return ma_decoder_init_file_w(file.wstring().c_str(), &config, decoder);
#else
    return ma_decoder_init_file(file.string().c_str(), &config, decoder);
#endif
}

std::string DecodeFailure(const std::filesystem::path& file, ma_result result) {
    const char* reason = ma_result_description(result);
    return "Failed to decode '" + file.filename().string() + "': " +
           (reason != nullptr ? reason : "unknown error");
}

template <typename Fn>
bool WithDecoder(const std::filesystem::path& file, std::string* err, Fn&& fn) {
    ma_decoder decoder{};
    const ma_result opened = OpenDecoder(file, &decoder);
    if (opened != MA_SUCCESS) {
        const std::string message = DecodeFailure(file, opened);
        SetError(err, message);
        LUMA_LOG_WARN("Audio", "{}", message);
        return false;
    }
    const bool ok = fn(decoder);
    ma_decoder_uninit(&decoder);
    return ok;
}

}  // namespace

std::optional<SoundInfo> ProbeSound(const std::filesystem::path& file, std::string* err) {
    if (err != nullptr) {
        *err = {};
    }
    std::optional<SoundInfo> info;
    const bool ok = WithDecoder(file, err, [&](ma_decoder& decoder) {
        ma_uint64 frames = 0;
        const ma_result length = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        if (length != MA_SUCCESS) {
            SetError(err, DecodeFailure(file, length));
            return false;
        }
        SoundInfo probed;
        probed.channels = decoder.outputChannels;
        probed.sampleRate = decoder.outputSampleRate;
        probed.frames = frames;
        probed.seconds = probed.sampleRate > 0 ? static_cast<f64>(frames) / static_cast<f64>(probed.sampleRate) : 0.0;
        info = probed;
        return true;
    });
    return ok ? info : std::nullopt;
}

std::optional<WaveformPeaks> ComputePeaks(const std::filesystem::path& file, u32 columns, std::string* err) {
    if (err != nullptr) {
        *err = {};
    }
    if (columns == 0) {
        SetError(err, "Waveform column count must be greater than zero");
        return std::nullopt;
    }
    std::optional<WaveformPeaks> peaks;
    const bool ok = WithDecoder(file, err, [&](ma_decoder& decoder) {
        ma_uint64 frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS) {
            SetError(err, "Failed to read the length of '" + file.filename().string() + "'");
            return false;
        }
        WaveformPeaks result;
        result.min.assign(columns, 0.0f);
        result.max.assign(columns, 0.0f);
        const u64 perColumn = (frames + columns - 1) / columns;
        const u32 channelCount = std::max(decoder.outputChannels, 1u);
        if (perColumn == 0) {
            peaks = std::move(result);
            return true;
        }
        std::vector<f32> samples(static_cast<usize>(perColumn) * channelCount);
        for (u32 column = 0; column < columns; ++column) {
            ma_uint64 framesRead = 0;
            const ma_result read = ma_decoder_read_pcm_frames(&decoder, samples.data(), perColumn, &framesRead);
            if (read != MA_SUCCESS && read != MA_AT_END) {
                SetError(err, DecodeFailure(file, read));
                return false;
            }
            if (framesRead == 0) {
                break;
            }
            f32 lo = std::numeric_limits<f32>::infinity();
            f32 hi = -std::numeric_limits<f32>::infinity();
            const usize count = static_cast<usize>(framesRead) * channelCount;
            for (usize i = 0; i < count; ++i) {
                lo = std::min(lo, samples[i]);
                hi = std::max(hi, samples[i]);
            }
            result.min[column] = lo;
            result.max[column] = hi;
        }
        peaks = std::move(result);
        return true;
    });
    return ok ? peaks : std::nullopt;
}

}  // namespace Luma::Audio
