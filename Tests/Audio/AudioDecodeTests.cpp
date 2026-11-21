#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/Audio/AudioDecode.h"

using Catch::Approx;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

}  // namespace

TEST_CASE("WAV and MP3 probe and a full-scale sine has peaks in every column", "[import][audio]") {
    std::string error;
    const auto wav = Luma::Audio::ProbeSound(Fixture("tone_mono.wav"), &error);
    REQUIRE(wav);
    REQUIRE(error.empty());
    REQUIRE(wav->channels == 1);
    REQUIRE(wav->sampleRate == 44100);
    REQUIRE(wav->seconds == Approx(1.0).margin(0.01));

    const auto mp3 = Luma::Audio::ProbeSound(Fixture("tone_stereo.mp3"), &error);
    REQUIRE(mp3);
    REQUIRE(mp3->channels == 2);
    REQUIRE(mp3->sampleRate == 44100);
    REQUIRE(mp3->seconds == Approx(1.0).margin(0.06));

    const auto peaks = Luma::Audio::ComputePeaks(Fixture("tone_mono.wav"), 64, &error);
    REQUIRE(peaks);
    REQUIRE(peaks->min.size() == 64);
    REQUIRE(peaks->max.size() == 64);
    // ffmpeg's sine source writes s16 peaks of ±4095, not full scale.
    const float kSinePeak = 4095.0f / 32768.0f;
    for (Luma::u32 column = 0; column < 64; ++column) {
        REQUIRE(peaks->max[column] == Approx(kSinePeak).margin(0.02f));
        REQUIRE(peaks->min[column] == Approx(-kSinePeak).margin(0.02f));
    }

    const auto temp = std::filesystem::temp_directory_path() / "luma-import-corrupt.wav";
    {
        std::ofstream out(temp, std::ios::binary);
        const char junk[16] = {'R', 'I', 'F', 'F', 8, 0, 0, 0, 'x', 'x', 'x', 'x', 1, 2, 3, 4};
        out.write(junk, 16);
    }
    const auto corrupt = Luma::Audio::ProbeSound(temp, &error);
    REQUIRE_FALSE(corrupt);
    REQUIRE_FALSE(error.empty());
    std::filesystem::remove(temp);
}
