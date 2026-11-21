#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>

#include "Luma/AssetImport/Translators/AudioTranslator.h"

using Catch::Approx;
using namespace Luma::Import;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

const ImportNode* FindKind(const ImportScene& scene, NodeKind kind) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == kind) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Audio translator reports one sound node", "[import][audio]") {
    AudioTranslator translator;
    REQUIRE(translator.Name() == "Luma.Import.Audio");
    REQUIRE(translator.CanTranslate(Fixture("tone_mono.wav")));
    REQUIRE(translator.CanTranslate(Fixture("tone_stereo.mp3")));
    REQUIRE_FALSE(translator.CanTranslate(Fixture("img_rgba.png")));

    ImportScene wavScene;
    std::string error;
    REQUIRE(translator.Translate(Fixture("tone_mono.wav"), wavScene, &error));
    REQUIRE(error.empty());
    REQUIRE(wavScene.OfKind(NodeKind::File).size() == 1);
    REQUIRE(wavScene.OfKind(NodeKind::Sound).size() == 1);
    const ImportNode* wav = FindKind(wavScene, NodeKind::Sound);
    REQUIRE(wav);
    REQUIRE(wav->name == "tone_mono");
    REQUIRE(wav->soundChannels == 1);
    REQUIRE(wav->soundSampleRate == 44100);
    REQUIRE(wav->soundSeconds == Approx(1.0).margin(0.01));

    ImportScene mp3Scene;
    REQUIRE(translator.Translate(Fixture("tone_stereo.mp3"), mp3Scene, &error));
    const ImportNode* mp3 = FindKind(mp3Scene, NodeKind::Sound);
    REQUIRE(mp3);
    REQUIRE(mp3->name == "tone_stereo");
    REQUIRE(mp3->soundChannels == 2);
    REQUIRE(mp3->soundSampleRate == 44100);
    REQUIRE(mp3->soundSeconds == Approx(1.0).margin(0.06));
}
