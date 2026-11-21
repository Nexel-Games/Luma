#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <functional>
#include <vector>
#include "PanelContext.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"
#include "Luma/RHI/Renderer.h"

namespace Luma {
class AssetRegistry;
}

namespace Luma::Editor::Panels {

class FileSystemTreePanel {
public:
    void SetFolderTexture(TextureHandle tex);
    void SetOpenFolderTexture(TextureHandle tex);
    void SetExpandTexture(TextureHandle tex);
    void SetRetractTexture(TextureHandle tex);
    void SetSelectedFolder(const std::filesystem::path& folder);
    void SetRegistry(Luma::AssetRegistry* registry);
    
    void Draw(Slate::Context& ui, const Slate::Rect& body);

    // Folder rows from the last Draw, in screen space. Used by the Content
    // Browser to rename a row and to accept an asset dropped on it.
    struct FolderRow {
        std::filesystem::path path;
        Slate::Rect row;
        Slate::Rect label;
        bool rightClicked = false;
    };
    const std::vector<FolderRow>& Rows() const noexcept { return m_rows; }
    
    std::filesystem::path SelectedFolder() const;
    
    // Callback when a folder is selected
    std::function<void(const std::filesystem::path&)> OnFolderSelected;

private:
    TextureHandle m_folderTex = 0;
    TextureHandle m_openFolderTex = 0;
    TextureHandle m_expandTex = 0;
    TextureHandle m_retractTex = 0;
    std::filesystem::path m_currentFolder;
    Luma::AssetRegistry* m_registry = nullptr;
    
    // Expansion state tracking
    std::unordered_set<std::string> m_expanded;
    std::vector<FolderRow> m_rows;
    // Vertical scroll offset of the folder list (px).
    f32 m_scroll = 0.0f;
    
    static constexpr f32 kRowH = 24.0f;
    
    void EnsureRevealed(const std::filesystem::path& folder);
    bool HasChildFolders(const std::filesystem::path& path) const;
    bool IsExpanded(const std::filesystem::path& path) const;
    void DrawFolderBranch(Slate::Context& ui, const Slate::Rect& panel,
                          const char* label, const std::filesystem::path& path,
                          f32& y, int depth);
    void DrawFolderRow(Slate::Context& ui, const Slate::Rect& panel,
                       const char* label, const std::filesystem::path& path,
                       f32& y, int depth, bool selected, bool hasChildren,
                       bool expanded);
};

}  // namespace Luma::Editor::Panels
