#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Luma/MaterialEditors/EditorIcons.h"
#include "Luma/Slate/Icons.h"

using namespace Luma;

TEST_CASE("Luma icon pack slots draw in full colour, monochrome glyphs stay tintable", "[slate][icons]") {
    // Entries pointing at Content/Editor/Luma_Icons are full colour.
    CHECK(Slate::IconIsFullColor(Slate::Icon::Play));
    CHECK(Slate::IconIsFullColor(Slate::Icon::Save));
    // Content Browser: folders use the Luma pack, asset types keep their classic art.
    CHECK(Slate::IconIsFullColor(Slate::Icon::Folder));
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::MaterialInstance));
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::Image));
    CHECK(Slate::IconIsFullColor(Slate::Icon::Search));
    // UI glyphs with no Luma counterpart keep their tint.
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::Close));
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::ChevronDown));
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::None));
    CHECK_FALSE(Slate::IconIsFullColor(Slate::Icon::Count));
}

TEST_CASE("Full-colour texture registry marks and unmarks handles", "[slate][icons]") {
    constexpr TextureHandle kTexture = 0x5EED'0001ull;
    CHECK_FALSE(Slate::IsFullColorTexture(kTexture));
    Slate::MarkFullColorTexture(kTexture);
    CHECK(Slate::IsFullColorTexture(kTexture));
    Slate::UnmarkFullColorTexture(kTexture);
    CHECK_FALSE(Slate::IsFullColorTexture(kTexture));
    Slate::MarkFullColorTexture(0);  // the invalid handle is never marked
    CHECK_FALSE(Slate::IsFullColorTexture(0));
}

TEST_CASE("Luma icon paths are recognised with either separator", "[slate][icons]") {
    CHECK(Slate::IsLumaIconPath("Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/001_play.png"));
    CHECK(Slate::IsLumaIconPath("D:/Luma/Content/Editor/Luma_Icons/PNG/16x16/x.png"));
    CHECK(Slate::IsLumaIconPath("C:\\Luma\\Content\\Editor\\Luma_Icons\\PNG\\x.png"));
    CHECK_FALSE(Slate::IsLumaIconPath("Icons/ue/icon_tab_Viewports_16x.png"));
}

TEST_CASE("Material editor icons resolve to files under Content/Editor", "[editor][icons]") {
    const std::filesystem::path editorRoot = std::filesystem::path(LUMA_SOURCE_DIR) / "Content" / "Editor";
    for (int i = 0; i < static_cast<int>(MaterialEditors::EditorIcon::Count); ++i) {
        const auto icon = static_cast<MaterialEditors::EditorIcon>(i);
        const std::string file = MaterialEditors::EditorIconFile(icon);
        INFO(file);
        REQUIRE_FALSE(file.empty());
        CHECK(std::filesystem::is_regular_file(editorRoot / file));
        // Luma pack artwork is never re-themed; the material editor's own glyphs are.
        CHECK(MaterialEditors::EditorIconKeepsColor(icon) == Slate::IsLumaIconPath(file));
    }
}
