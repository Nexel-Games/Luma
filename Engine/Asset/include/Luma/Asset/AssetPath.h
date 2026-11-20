#pragma once

#include <filesystem>
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
