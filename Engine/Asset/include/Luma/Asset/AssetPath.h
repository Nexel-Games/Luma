#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

// Unreal-style virtual asset paths, the form every asset reference is stored
// in on disk (materials -> textures, instances -> parents, scene slots ->
// materials):
//
//   /Game/Materials/M_Wood          -> <project>/Assets/Materials/M_Wood.lmat
//   /Engine/Materials/DefaultMaterial -> <engine>/Content/Engine/Materials/...
//
// Virtual paths carry no extension and survive moving or renaming the project
// folder (raw AssetIds are hashes of absolute paths and do not).

namespace Luma {

class AssetPathResolver {
public:
    void SetGameRoot(const std::filesystem::path& root);
    void SetEngineRoot(const std::filesystem::path& root);
    const std::filesystem::path& GameRoot() const { return m_game; }
    const std::filesystem::path& EngineRoot() const { return m_engine; }

    // Resolves a virtual path to the asset file. The extension is found by
    // looking for exactly one file with that stem in the target folder.
    // nullopt when the prefix is unknown, the root is unset, or no file
    // exists.
    std::optional<std::filesystem::path> ToFile(std::string_view virtualPath) const;
    // Typed lookup: the first existing file among `extensions` (no dot, in
    // preference order). Disambiguates assets sharing a stem (a material
    // and a texture both named "Wood").
    std::optional<std::filesystem::path> ToFile(
        std::string_view virtualPath,
        std::initializer_list<std::string_view> extensions) const;
    template <typename Range>
    std::optional<std::filesystem::path> ToFileAny(std::string_view virtualPath,
                                                   const Range& extensions) const {
        for (std::string_view ext : extensions) {
            if (ext.empty()) continue;
            if (auto f = MakeFilePath(virtualPath, ext); f && std::filesystem::is_regular_file(*f)) {
                return f;
            }
        }
        return std::nullopt;
    }
    // The file a virtual path maps to with `extension`, whether or not it
    // exists yet (target for saving a new asset). nullopt for unknown roots.
    std::optional<std::filesystem::path> MakeFilePath(std::string_view virtualPath,
                                                      std::string_view extension) const;

    // Converts a file under the game or engine root to its virtual path
    // (no extension, '/' separators). nullopt for files outside both roots.
    std::optional<std::string> ToVirtual(const std::filesystem::path& file) const;

    static constexpr std::string_view kGamePrefix = "/Game/";
    static constexpr std::string_view kEnginePrefix = "/Engine/";

private:
    std::filesystem::path m_game;
    std::filesystem::path m_engine;
};

}  // namespace Luma
