#include "Luma/Audio/AudioPreview.h"

#include "Luma/Core/Log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma::Audio {
namespace {

ma_result OpenSound(ma_engine* engine, const std::filesystem::path& file, ma_sound* sound) {
#if defined(_WIN32)
    return ma_sound_init_from_file_w(engine, file.wstring().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                     sound);
#else
    return ma_sound_init_from_file(engine, file.string().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound);
#endif
}

}  // namespace

struct AudioPreview::Impl {
    ma_engine engine{};
    ma_sound sound{};
    bool engineReady = false;
    bool soundReady = false;
    bool warned = false;
    std::filesystem::path current;
};

AudioPreview::AudioPreview() : m_impl(std::make_unique<Impl>()) {}

AudioPreview::~AudioPreview() {
    Shutdown();
}

bool AudioPreview::Init(std::string* err) {
    if (m_impl->engineReady) {
        if (err != nullptr) {
            *err = {};
        }
        return true;
    }
    const ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result != MA_SUCCESS) {
        const std::string message = "Audio preview is unavailable because no audio device could be opened";
        if (err != nullptr) {
            *err = message;
        }
        if (!m_impl->warned) {
            LUMA_USER_WARN("Audio", "{}", message);
            m_impl->warned = true;
        }
        return false;
    }
    m_impl->engineReady = true;
    if (err != nullptr) {
        *err = {};
    }
    return true;
}

void AudioPreview::Shutdown() {
    if (m_impl == nullptr) {
        return;
    }
    if (m_impl->soundReady) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundReady = false;
    }
    if (m_impl->engineReady) {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineReady = false;
    }
    m_impl->current.clear();
}

bool AudioPreview::Play(const std::filesystem::path& file, f32 volume, f32 pitch, bool loop) {
    if (!m_impl->engineReady) {
        return false;
    }
    if (m_impl->soundReady) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundReady = false;
    }
    const ma_result opened = OpenSound(&m_impl->engine, file, &m_impl->sound);
    if (opened != MA_SUCCESS) {
        LUMA_LOG_WARN("Audio", "Failed to play '{}': {}", file.string(), ma_result_description(opened));
        return false;
    }
    m_impl->soundReady = true;
    ma_sound_set_volume(&m_impl->sound, volume);
    ma_sound_set_pitch(&m_impl->sound, pitch);
    ma_sound_set_looping(&m_impl->sound, loop ? MA_TRUE : MA_FALSE);
    if (ma_sound_start(&m_impl->sound) != MA_SUCCESS) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundReady = false;
        return false;
    }
    m_impl->current = file;
    return true;
}

void AudioPreview::Stop() {
    if (m_impl->soundReady) {
        ma_sound_stop(&m_impl->sound);
    }
}

bool AudioPreview::IsPlaying() const {
    return m_impl->soundReady && ma_sound_is_playing(&m_impl->sound) == MA_TRUE;
}

const std::filesystem::path& AudioPreview::Current() const {
    return m_impl->current;
}

}  // namespace Luma::Audio
