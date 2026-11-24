#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Editor/EditorIcons.h"
#include "Luma/Editor/Panels/ContentBrowser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <sstream>

#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Audio/AudioPreview.h"
#include "Luma/Core/Log.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Icon;
using Slate::Rect;
using Slate::Vec2;

std::string SoundCaption(const std::filesystem::path& file) {
    const auto meta = Import::ReadMetadata(file);
    if (!meta) return "Sound";
    const SerialValue* seconds = meta->settings.Find("seconds");
    if (seconds == nullptr || !seconds->IsNumber()) return "Sound";
    int total = static_cast<int>(std::lround(seconds->AsFloat()));
    if (total < 0) total = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

static f64 SteadyMs() {
    using namespace std::chrono;
    return duration<f64, std::milli>(steady_clock::now().time_since_epoch()).count();
}

ContentBrowserPanel::ContentBrowserPanel() = default;

// Last frame's deferred count drives a Background Tasks entry, so a folder of
// fresh imports shows progress instead of looking idle.
void ContentBrowserPanel::UpdateThumbnailTask(PanelContext& ctx) {
    const u32 waiting = m_thumbBudget.DeferredThisFrame();
    if (waiting > 0 && !m_thumbTask.IsValid() && ctx.tasks != nullptr) {
        Tasks::TaskDesc desc;
        desc.title = "Generating thumbnails";
        desc.category = "Content Browser";
        m_thumbTask = ctx.tasks->Begin(std::move(desc));
    }
    if (!m_thumbTask.IsValid()) return;
    if (waiting == 0) {
        m_thumbTask.Complete();
        m_thumbTask = Tasks::TaskToken{};
    } else {
        m_thumbTask.SetStatus(std::to_string(waiting) + " waiting");
    }
}

void ContentBrowserPanel::SetIcons(Luma::TextureHandle sortUp,
                                   Luma::TextureHandle sortDown,
                                   Luma::TextureHandle searchGlass,
                                   Luma::TextureHandle folder,
                                   Luma::TextureHandle reload,
                                   Luma::TextureHandle importTex,
                                   Luma::TextureHandle openFolder,
                                   Luma::TextureHandle expandArrow,
                                   Luma::TextureHandle retractArrow) {
    m_texSortUp = sortUp;
    m_texSortDown = sortDown;
    m_texSearchGlass = searchGlass;
    m_texFolder = folder;
    m_texReload = reload;
    m_texImport = importTex;
    m_texOpenFolder = openFolder;
    m_texExpandArrow = expandArrow;
    m_texRetractArrow = retractArrow;
    m_tree.SetFolderTexture(folder);
    m_tree.SetOpenFolderTexture(openFolder);
    m_tree.SetExpandTexture(expandArrow);
    m_tree.SetRetractTexture(retractArrow);
}

void ContentBrowserPanel::SetRenderer(Luma::Renderer* renderer) {
    m_renderer = renderer;
}

void ContentBrowserPanel::SetShowPluginContent(bool show) {
    m_showPluginContent = show;
    if (!show && m_registry && m_registry->IsPluginPath(m_currentFolder)) {
        ResetNavigation();
    }
}

void ContentBrowserPanel::ResetNavigation() {
    m_currentFolder.clear();
    m_tree.SetSelectedFolder({});
    m_selected = AssetId{};
}

void ContentBrowserPanel::BrowseTo(const std::filesystem::path& file) {
    NavigateTo(file.parent_path());
    if (m_registry) {
        if (const AssetData* data = m_registry->LookupByPath(file)) m_selected = data->id;
    }
}

void ContentBrowserPanel::NavigateTo(const std::filesystem::path& folder) {
    m_currentFolder = folder.lexically_normal();
    m_tree.SetSelectedFolder(m_currentFolder);
}

void ContentBrowserPanel::NavigateUp() {
    if (m_currentFolder.empty()) return;
    auto parent = m_currentFolder.parent_path();
    if (parent == m_currentFolder) return;
    m_currentFolder = parent;
}

std::vector<std::filesystem::path> ContentBrowserPanel::BreadcrumbSegments()
    const {
    std::vector<std::filesystem::path> segs;
    std::filesystem::path acc;
    for (auto part : m_currentFolder) {
        acc /= part;
        segs.push_back(acc);
    }
    return segs;
}

static AssetTypeRegistry::Description Describe(const AssetData& asset) {
    if (asset.type != AssetType::Unknown) return AssetTypes().FindBuiltin(asset.type);
    if (auto desc = AssetTypes().FindByExtension(asset.extension)) return desc;
    return AssetTypes().FindBuiltin(AssetType::Unknown);
}

std::vector<const AssetData*> ContentBrowserPanel::CurrentEntries() const {
    if (!m_registry) return {};
    std::optional<std::filesystem::path> dir;
    if (!m_currentFolder.empty()) {
        dir = m_currentFolder;
    } else if (!m_registry->Roots().empty()) {
        // When at root, filter by the root directory to show only direct children
        dir = m_registry->Roots()[0];
    }
    auto filter = AssetTypes().Find(m_typeFilter);
    auto entries = m_registry->Filter(std::nullopt, dir, m_nameFilter);
    if (!m_showPluginContent) {
        std::erase_if(entries, [&](const AssetData* asset) {
            return m_registry->IsPluginPath(asset->packagePath);
        });
    }
    if (filter) std::erase_if(entries, [&](const AssetData* asset) { return Describe(*asset)->id != filter->id; });
    return entries;
}

void ContentBrowserPanel::DrawToolbar(Slate::Context& ui,
                                      const Slate::Rect& rect,
                                      PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    ui.GradientRect(rect, t.surface2, t.surface1);
    ui.Panel({rect.x, rect.Bottom() - 1.0f, rect.w, 1.0f}, t.separator);

    // Shared chrome height for all toolbar-row controls (reload, import,
    // search bar).
    constexpr f32 kBarH = 24.0f;

    // Reload button (re-scans the registry). Rendered as a bare icon —
    // no button chrome: draw the PNG directly, 20px inside a 24px hit
    // rect, tinted brighter on hover. Back/forward navigation now lives
    // in the breadcrumb.
    Rect reloadR{rect.x + 8.0f, rect.y + 6.0f, 24.0f, 24.0f};
    {
        bool reloadHover = reloadR.Contains(ui.mouse());
        if (reloadHover) ui.RequestCursor(Luma::CursorShape::Hand);
        if (reloadHover && ui.mousePressed(0)) m_reloadPressed = true;
        bool clicked = false;
        if (ui.mouseReleased(0)) {
            if (m_reloadPressed && reloadHover) clicked = true;
            m_reloadPressed = false;
        }
        constexpr f32 kReloadIcon = 20.0f;
        Rect iconR{reloadR.x + (reloadR.w - kReloadIcon) * 0.5f,
                   reloadR.y + (reloadR.h - kReloadIcon) * 0.5f,
                   kReloadIcon, kReloadIcon};
        if (m_texReload) {
            ui.Image(m_texReload, iconR,
                     reloadHover ? t.text : t.textDim);
        } else {
            Slate::DrawIcon(ui, iconR, Slate::Icon::Refresh,
                            reloadHover ? t.text : t.textDim);
        }
        if (clicked && m_registry) m_registry->Scan();
    }

    // Import button (round-rect pill with "Import" label + a 90-deg-CW
    // rotated orange import icon matching UE theme). Sits between the reload icon and the
    // breadcrumb box. The whole pill highlights on hover and depresses on
    // press, both driven by Animate so the transitions are smooth (not
    // instant).
    const Slate::Color kImportOrange = t.accent;
    const Slate::Color kImportRest = t.surface3;
    const Slate::Color kImportHover = kImportOrange;
    constexpr f32 kImportIconSide = 16.0f;
    constexpr f32 kImportPadX = 8.0f;
    const Slate::Font& uiF = ui.uiFont();
    Vec2 importTextSize = uiF.Measure("Import");
    const f32 importFullW = kImportIconSide + 6.0f + importTextSize.x +
                            kImportPadX * 2.0f;
    // Responsive toolbar: when the panel is too narrow for the labelled
    // pill plus a usable search bar, Import collapses to an icon button.
    constexpr f32 kMinSearchW = 150.0f;
    const bool compactImport =
        rect.w < 40.0f + importFullW + 8.0f + kMinSearchW + 26.0f + 10.0f;
    f32 importW = compactImport ? kImportIconSide + kImportPadX * 2.0f
                                : importFullW;
    Rect importR{rect.x + 40.0f, rect.y + 6.0f, importW, kBarH};
    u64 importId = Slate::Context::ID("cb.import");
    {
        bool hover = importR.Contains(ui.mouse());
        if (hover) ui.RequestCursor(Luma::CursorShape::Hand);
        if (hover && ui.mousePressed(0)) m_importPressed = true;
        bool pressed = m_importPressed && hover;
        bool clicked = false;
        if (ui.mouseReleased(0)) {
            if (m_importPressed && hover) clicked = true;
            m_importPressed = false;
        }
        // Smooth hover fade (0..1) + press fade (0..1). Press darkens
        // the fill slightly so it reads as a depression.
        f32 hoverT = ui.Animate(importId ^ 0xBEEFCAFEull, hover,
                                t.motion.hover);
        f32 pressT = ui.Animate(importId ^ 0xFEEDFACEull, pressed,
                                t.motion.press);
        Slate::Color bg = Slate::Mix(kImportRest, kImportHover, hoverT);
        bg = Slate::Darken(bg, 0.10f * pressT);
        // Press kit subtly shrinks the pill inward (a "click" feel).
        f32 shrink = 0.5f * pressT;
        Rect drawR = importR.Inset(shrink, shrink);
        ui.PanelRounded(drawR, bg, t.radius.pill);
        ui.PanelRoundedBordered(drawR, Slate::Mix(t.outline, kImportOrange,
                                                  0.6f * hoverT),
                                t.outline, t.radius.pill,
                                t.border.hairline);

        // Rotated (90deg CW) orange icon on the left of the pill; brighter
        // as hover fades in.
        f32 iconCx = drawR.x + kImportPadX + kImportIconSide * 0.5f;
        f32 iconCy = drawR.y + drawR.h * 0.5f;
        Slate::Color iconColor = Slate::Mix(kImportOrange,
                                            Slate::Lighten(kImportOrange, 0.25f),
                                            hoverT);
        if (m_texImport) {
            ui.drawList().AddImageRotated(
                m_texImport, {iconCx, iconCy}, kImportIconSide * 0.5f,
                1.5707963f,  // pi/2 = 90 deg CW (rotate the quad so the
                             // image reads rotated)
                iconColor);
        }
        // "Import" label to the right of the icon; dark on green-hover,
        // normal text at rest. The label rect spans the full pill height
        // so LabelIn centers the baseline inside the pill (a rect scaled
        // to the text height starts mid-pill and clips the bottom off).
        // Cancel LabelIn's internal space.lg padding so the glyph sits
        // snug against the icon rather than drifting right.
        Slate::Color labelRest = t.text;
        Slate::Color labelHover = t.menu.highlightText;
        constexpr f32 kLabelGap = 4.0f;
        f32 labelX = drawR.x + kImportPadX + kImportIconSide + kLabelGap -
                     t.space.lg;
        if (!compactImport) {
            ui.LabelIn({labelX, drawR.y, importTextSize.x + t.space.lg * 2.0f,
                        drawR.h},
                       "Import", Slate::Mix(labelRest, labelHover, hoverT),
                       Align::Left);
        }
        if (clicked && ctx.onImportAssets) ctx.onImportAssets(m_currentFolder);
    }
    ui.Tooltip(Slate::Context::ID("tip.cb.reload"), reloadR,
               "Refresh\nRescan the project's content from disk.");
    ui.Tooltip(Slate::Context::ID("tip.cb.import"), importR,
               "Import\nImport files into the current folder.");
    constexpr f32 kImportGap = 8.0f;
    f32 contentStartX = importR.Right() + kImportGap;

    // Search bar with embedded filter toggle.
    // Layout: [reload] [Import] [breadcrumb...] [search][toggle]
    // The toggle lives inside the bar's right edge and shows a down chevron
    // when the filter menu is closed / up chevron when open.
    constexpr f32 kToggleW = 26.0f;
    constexpr f32 kSepW = 1.0f;
    constexpr f32 kBarMaxW = 200.0f;
    constexpr f32 kBarGap = 4.0f;
    f32 barRight = rect.Right() - 8.0f;
    f32 toggleX = barRight - kToggleW;
    // Keep the search bar a fixed, Unreal-style width (not the full
    // toolbar); right-align it against the filter toggle.
    // Never overlap the Import button: the bar takes what is left (capped).
    f32 barW = std::max(48.0f, std::min(kBarMaxW, toggleX - kSepW - contentStartX));
    f32 barX = toggleX - kSepW - barW;
    Rect barR{barX, rect.y + 6.0f, barW, kBarH};
    Rect textR{barR.x, barR.y, barR.w - kToggleW - kSepW, barR.h};
    Rect toggleR{toggleX, barR.y, kToggleW, barR.h};
    Rect sepR{textR.Right(), barR.y, kSepW, barR.h};

    // The search box reuses the available text rect; its field background
    // covers textR, the leading icon sits at the left of textR, and its
    // built-in clear-X sits at the right of textR (just left of the
    // separator) — none of these overlap the filter toggle zone.
    ui.SearchBox(Slate::Context::ID("cb.search"), textR, m_nameFilter,
                 m_texSearchGlass, "Search assets...");

    // Visual separator between the text field and the filter toggle so
    // they read as distinct zones inside the same bar.
    ui.Panel(sepR, t.separator);

    // Filter toggle button — click toggles the type-filter drop-down.
    // Uses pack chevron icons tinted with textDim at
    // rest, brighter on hover.
    bool toggleHover = toggleR.Contains(ui.mouse());
    if (toggleHover) {
        ui.PanelRoundedBordered(toggleR.Inset(2.0f, 2.0f), t.surface3, t.outline,
                                t.radius.sm, t.border.hairline);
    }
    const u64 filterId = Slate::Context::ID("cb.filter.toggle");
    const bool filterOpen = ui.IsMenuOpen(filterId);
    if (ui.Selectable(filterId, toggleR, "", filterOpen, Icon::None)) {
        Slate::MenuOptions mo;
        mo.minWidth = 200.0f;
        ui.OpenMenuBelow(filterId, toggleR, BuildFilterMenu(), mo);
    }
    ui.Tooltip(Slate::Context::ID("tip.cb.filter"), toggleR,
               "Filter by Type\nShow only one kind of asset.");
    // Draw the chevron on top (icon-only button).
    Slate::Icon chev = filterOpen ? Slate::Icon::ChevronUp
                                  : Slate::Icon::ChevronDown;
    Slate::DrawIcon(ui,
                    {toggleR.x + (kToggleW - 12.0f) * 0.5f,
                     toggleR.y + (kBarH - 12.0f) * 0.5f, 12.0f, 12.0f},
                    chev, toggleHover ? t.text : t.textDim);

    // Breadcrumb: lives in the gap between Refresh and the search bar,
    // same row, wrapped in a rounded "field" box that matches the search
    // bar's look. The box is sized to the breadcrumb content (not the
    // full gap), so every segment + its trailing chevron are visible and
    // the last word is never clipped. No folder icon; no hover/highlight
    // on segments (the text is the click target).
    constexpr f32 kSegH = 20.0f;
    constexpr f32 kSegPadX = 4.0f;
    constexpr f32 kChevW = 16.0f;
    constexpr f32 kBoxPad = 2.0f;   // left/right padding inside the box
    const Slate::Font& f = ui.uiFont();

    // Build the segment list first so we can measure the total content
    // width and size the box to fit it.
    std::filesystem::path rootTarget;
    if (m_registry && !m_registry->Roots().empty())
        rootTarget = m_registry->Roots().front();
    struct Seg { std::string label; std::filesystem::path target; };
    std::vector<Seg> segs;
    // Root segment is the project's content folder (Assets/ at the project
    // root — the registry is rooted there, not in a Content/ subdirectory).
    segs.push_back({"Assets", rootTarget});
    if (!m_currentFolder.empty() && m_registry) {
        std::string rel = m_registry->DisplayPathFor(m_currentFolder);
        const std::string prefix = "Assets/";
        if (rel.size() > prefix.size() && rel.substr(0, prefix.size()) == prefix)
            rel = rel.substr(prefix.size());
        const std::filesystem::path& absRoot = m_registry->Roots().front();
        std::stringstream ss(rel);
        std::string part;
        std::filesystem::path acc;
        while (std::getline(ss, part, '/')) {
            if (part.empty()) continue;
            acc /= part;
            segs.push_back({part, absRoot / acc});
        }
    }

    // Measure content width: root segment + (chevron + segment) for each
    // additional segment, plus a trailing chevron gap pair spacer.
    f32 contentW = kBoxPad;  // left padding
    for (usize i = 0; i < segs.size(); ++i) {
        if (i > 0) contentW += 2.0f + kChevW;
        contentW += f.Measure(segs[i].label).x + kSegPadX;
    }
    contentW += kBoxPad;  // right padding

    // Box: sized to content, capped to the available gap (so a very long
    // path can't push the box past the search bar).
    f32 bLeft = contentStartX;
    f32 bMaxRight = barX - kBarGap;
    f32 boxW = std::min(contentW, bMaxRight - bLeft);
    Rect boxR{bLeft, rect.y + 6.0f, boxW, kBarH};
    // Match the search bar: outer field-border ring + inner field fill,
    // both rounded with the field radius.
    ui.PanelRounded(boxR, t.fieldBorder, t.radius.md);
    ui.PanelRounded(boxR.Inset(t.border.hairline, t.border.hairline),
                    t.fieldBg,
                    std::max(0.0f, t.radius.md - t.border.hairline));

    // Segment text cursor starts inside the box's left padding.
    f32 x = boxR.x + kBoxPad;
    // Segment band vertically centered in the toolbar (matches the old
    // breadcrumb row baseline, not the box height — keeps text sitting
    // where it has always been, not biased by the box).
    f32 yMid = rect.y + (rect.h - kSegH) * 0.5f;

    // Clip the breadcrumb to the box interior so a path that exceeds the
    // capped box can't bleed across the search bar.
    ui.PushClip({boxR.x, boxR.y, boxR.w, boxR.h});

    auto drawChevron = [&](f32 xPos) {
        Slate::DrawIcon(ui,
                        {xPos, yMid + (kSegH - 10.0f) * 0.5f, 10.0f, 10.0f},
                        Icon::ChevronRight, t.textDisabled);
    };
    // No-chrome segment: hit-test inline (no hover/active fill drawn) and
    // lay down plain text. Tints the leaf (current folder) in textDim.
    // Uses Context::Label (direct position) instead of LabelIn because
    // LabelIn always injects m_theme.space.lg (12px) of left padding,
    // which would push each segment's text 12px right of its cursor and
    // clip the right edge inside a box sized exactly to the text.
    auto drawSegment = [&](const char* label,
                           const std::filesystem::path& target, bool isLast) {
        Vec2 ts = f.Measure(label);
        f32 segW = ts.x + kSegPadX;
        Rect r{x, yMid, segW, kSegH};
        bool hover = r.Contains(ui.mouse());
        if (hover) ui.RequestCursor(Luma::CursorShape::Hand);
        if (hover && ui.mousePressed(0)) m_breadPressed = true;
        if (ui.mouseReleased(0)) {
            if (m_breadPressed && hover && !ui.IsDraggingAnything()) NavigateTo(target);
            m_breadPressed = false;
        }
        if (hover) m_importDropFolder = target;
        if (const Slate::DragPayload* drop = AcceptAssetDrop(ui, r)) {
            MoveDropped(*drop, target, ctx);
        }
        // Center the text baseline within the segment band (kSegH) and
        // left-align to the cursor (no implicit padding).
        f32 textY = yMid + (kSegH - f.LineHeight()) * 0.5f;
        ui.Label({x, textY}, label, isLast ? t.textDim : t.text);
        x += segW;
    };

    for (usize i = 0; i < segs.size(); ++i) {
        if (i > 0) {
            x += 2.0f;
            drawChevron(x);
            x += kChevW;
        }
        bool isLast = (i + 1 == segs.size());
        drawSegment(segs[i].label.c_str(), segs[i].target, isLast);
    }
    ui.PopClip();

}

void ContentBrowserPanel::DrawGridPane(Slate::Context& ui,
                                       const Slate::Rect& rect,
                                       PanelContext& ctx) {
    if (m_renderer == nullptr && ctx.renderer != nullptr) {
        m_renderer = ctx.renderer;
    }

    Slate::Theme& t = ui.theme();
    // Light shadow background: the surrounding content-browser body
    // (t.surface1) shows through, with a subtle dark inset so the grid
    // reads as a softer, recessed area without a hard panel fill.
    ui.Panel(rect, Slate::Darken(t.surface1, 0.10f));

    auto entries = CurrentEntries();
    

    if (entries.empty()) {
        ui.LabelIn({rect.x, rect.y + 12.0f, rect.w, 22.0f},
                   "  No assets match the current filter.", t.textDim,
                   Align::Center);
        m_gridScroll = 0.0f;
        return;
    }

    // Sort: folders first, then by name (case-insensitive). The toolbar
    // chevron now drives the filter menu (not sort direction), so the grid
    // always sorts ascending by name.
    std::sort(entries.begin(), entries.end(),
              [](const AssetData* a, const AssetData* b) {
                  if (a->IsFolder() != b->IsFolder())
                      return a->IsFolder();
                  std::string an = a->assetName;
                  std::transform(an.begin(), an.end(), an.begin(),
                                 [](unsigned char c) { return std::tolower(c); });
                  std::string bn = b->assetName;
                  std::transform(bn.begin(), bn.end(), bn.begin(),
                                 [](unsigned char c) { return std::tolower(c); });
                  if (an == bn) return false;
                  return an < bn;
              });

    // Card grid: each card is kTileW x kTileH with kTileGap between them.
    // Compute columns that fit the available width and start rows from
    // the top padding.
    constexpr f32 kPad = 12.0f;
    constexpr f32 kThumbH = 76.0f;      // top thumbnail strip (reduced)
    constexpr f32 kTextRowsH = kTileH - kThumbH;  // name + type rows
    const Slate::Font& f = ui.uiFont();
    f32 availW = rect.w - kPad * 2.0f;
    f32 colW = kTileW + kTileGap;
    int cols = std::max(1, static_cast<int>(availW / colW));
    f32 startX = rect.x + kPad;
    // Cards shift up by the scroll offset so the grid scrolls like the
    // outliner list; the pane is clipped by the dock, and the scrollbar is
    // added after the loop once the row count is known.
    f32 startY = rect.y + kPad - m_gridScroll;

    // Per-card palette: the card has no bg/border at idle (only the icon
    // + text float). On hover, a bg + border + thumbnail frame + shadow
    // fade in together so the card reveals as a self-contained unit.
    const Slate::Color kCardHover = t.surface3;
    const Slate::Color kBorderHover = Slate::Mix(t.outline, t.accent, 0.5f);

    // Clip the cards to the grid pane so scrolled rows can't overlap the
    // toolbar / breadcrumb above the pane.
    ui.PushClip(rect);

    for (usize i = 0; i < entries.size(); ++i) {
        const auto* a = entries[i];
        const auto desc = Describe(*a);
        int row = static_cast<int>(i) / cols;
        int col = static_cast<int>(i) % cols;
        f32 tx = startX + col * colW;
        f32 ty = startY + row * (kTileH + kTileGap);
        Rect card{tx, ty, kTileW, kTileH};

        // Stable id so Animate() can interpolate hover/select per card
        // across frames without resetting.
        u64 cardId = Slate::Context::ID(
            (a->packagePath.string() + "|card").c_str());

        bool hover = card.Contains(ui.mouse()) && rect.Contains(ui.mouse());
        if (hover) ui.RequestCursor(Luma::CursorShape::Hand);
        if (hover && ui.mousePressed(1)) {
            m_contextTarget = a->id;
            if (!a->IsFolder()) m_selected = a->id;
        }

        bool selected = (m_selected == a->id);

        // Materials, textures, and meshes still drag onto the Inspector and
        // viewport. Every tile can also be dropped on a folder to move it.
        {
            Slate::DragPayload payload;
            payload.sourcePath = a->packagePath.string();
            payload.label = a->assetName;
            payload.type = desc->dragType;
            if (!a->IsFolder() && ctx.virtualPathOf) payload.data = ctx.virtualPathOf(a->packagePath);
            if (payload.data.empty()) payload.data = payload.sourcePath;
            if (auto th = m_thumbnailCache.find(a->id); th != m_thumbnailCache.end()) {
                payload.icon = th->second;
            }
            ui.BeginDrag(cardId ^ 0xD2A6ull, card, payload);
        }
        if (a->IsFolder()) {
            if (card.Contains(ui.mouse()) && rect.Contains(ui.mouse())) {
                m_importDropFolder = a->packagePath;
            }
            if (const Slate::DragPayload* drop = AcceptAssetDrop(ui, card)) {
                MoveDropped(*drop, a->packagePath, ctx);
            }
        }

        // Card-body click: single-click selects the asset or enters
        // the folder. If already selected AND the name row is the
        // target, skip the Selectable response so that click falls
        // through to the rename TextField.
        Rect nameRProbe{card.x + 4.0f, card.y + kThumbH + 2.0f,
                        card.w - 8.0f, 14.0f};
        bool onNameRow = nameRProbe.Contains(ui.mouse());
        const bool soundTile = !a->IsFolder() && a->type == AssetType::Sound;
        Rect playR{card.x + 8.0f, card.y + kThumbH - 26.0f, 20.0f, 20.0f};
        const bool onPlay = soundTile && playR.Contains(ui.mouse());
        bool allowCard = !(selected && onNameRow);
        if (ui.Selectable(cardId, card, "", false, Icon::None) && allowCard && !onPlay) {
            if (a->IsFolder()) {
                m_currentFolder = a->packagePath;
                m_selected = a->id;
            } else {
                m_selected = (selected ? AssetId{} : a->id);
            }
        }
        // Double-click opens (Unreal): folders navigate, assets activate (the
        // editor opens their asset editor). Rename is F2 / the context menu.
        if (hover && ui.mouseDoubleClicked(0)) {
            if (a->IsFolder()) {
                m_currentFolder = a->packagePath;
                m_selected = AssetId{};
            } else if (soundTile && ctx.audioPreview) {
                m_selected = a->id;
                ctx.audioPreview->Play(a->packagePath);
            } else {
                m_selected = a->id;
                m_activated = a->id;
            }
        }
        (void)selected;

        // Card chrome is instantaneous: bg/border/shadow appear and
        // disappear with hover — no Animate, no lingering highlight.
        bool isFolder = a->IsFolder();
        if (!isFolder || hover) {
            ui.drawList().AddRectShadow(card, t.radius.md, 0.45f, 3.0f);
            ui.PanelRounded(card, kCardHover, t.radius.md);
            if (hover) {
                ui.PanelRoundedBordered(card, kCardHover, kBorderHover,
                                        t.radius.md, t.border.hairline);
            } else {
                ui.PanelRoundedBordered(card, kCardHover, t.outline,
                                        t.radius.md, t.border.hairline);
            }
        }

        // Thumbnail strip (top portion of the card). Slightly darker so
        // the icon reads as framed inside the card.
        Rect thumb{card.x + 4.0f, card.y + 4.0f, card.w - 8.0f,
                   kThumbH - 4.0f};
        if (!isFolder || hover) {
            ui.PanelRounded(thumb, Slate::Darken(kCardHover, 0.30f),
                            t.radius.sm);
        }
        
        // Centered icon or rendered thumbnail in the thumbnail strip.
        u64 thumbTex = 0;
        if (!isFolder) {
            Luma::Renderer* activeRenderer =
                (ctx.renderer != nullptr) ? ctx.renderer : m_renderer;
            thumbTex = GetThumbnailTexture(a->id, a->packagePath, activeRenderer);
        }

        if (thumbTex != 0) {
            // Draw the rendered thumbnail
            Rect thumbImg = thumb.Inset(2.0f, 2.0f);
            ui.Image(thumbTex, thumbImg);
        } else {
            Slate::Color iconColor = isFolder
                                         ? Slate::Color{255, 178, 92, 255}
                                         : t.textDim;
            f32 iconSide = std::min(thumb.w, thumb.h) * 0.62f;
            Rect iconR{thumb.x + (thumb.w - iconSide) * 0.5f,
                       thumb.y + (thumb.h - iconSide) * 0.5f,
                       iconSide, iconSide};
            if (isFolder && m_texOpenFolder) {
                ui.Image(m_texOpenFolder, iconR);
            } else if (isFolder && m_texFolder) {
                ui.Image(m_texFolder, iconR, iconColor);
            } else {
                DrawNamedIcon(ui, iconR, desc->icon, iconColor);
            }
        }

        if (soundTile && hover && ctx.audioPreview != nullptr) {
            const bool playing = ctx.audioPreview->IsPlaying() && ctx.audioPreview->Current() == a->packagePath;
            ui.PanelRounded(playR, t.surface3, t.radius.sm);
            Slate::DrawIcon(ui, playR.Inset(3.0f, 3.0f), playing ? Icon::Stop : Icon::Play, t.text);
            if (playR.Contains(ui.mouse()) && ui.mousePressed(0)) m_soundPress = a->id;
            if (ui.mouseReleased(0) && m_soundPress == a->id && playR.Contains(ui.mouse()) &&
                !ui.mouseDoubleClicked(0)) {
                if (playing) ctx.audioPreview->Stop();
                else ctx.audioPreview->Play(a->packagePath);
            }
        }

        // sourceChanged is a cached lookup (no disk access in the draw path).
        if (!isFolder && ctx.sourceChanged && ctx.sourceChanged(a->packagePath)) {
            ui.drawList().AddCircleFilled({thumb.Right() - 6.0f, thumb.y + 6.0f}, 4.0f, t.warning);
        }

        // Asset name / rename + type row.
        f32 nameX = card.x + 4.0f;
        f32 nameY = card.y + kThumbH + 2.0f;
        f32 nameW = card.w - 8.0f;
        Rect nameR{nameX, nameY, nameW, 14.0f};
        bool renamingThis = (m_renaming == a->id);
        if (renamingThis) {
            DrawRenameField(ui, nameR, *a, ctx);
        } else {
            ui.LabelIn(nameR, a->assetName, t.text, Align::Center);
        }
        // Asset type (smaller, dimmer, centered at bottom). Skipped for folders.
        if (!isFolder) {
            std::string_view typeName = desc->displayName;
            f32 typeY = card.y + kTileH - 16.0f;  // Position at bottom of card
            ui.LabelIn({nameX, typeY, nameW, 12.0f},
                       soundTile ? SoundCaption(a->packagePath) : std::string(typeName),
                       t.textDisabled, Align::Center);
        }
    }
    if (ui.mouseReleased(0)) m_soundPress = AssetId{};
    ui.PopClip();
    (void)kTextRowsH;  // layout constant kept for future tweaks
    (void)f;  // future-proofing for ellipsizing

    // Scroll region spans the grid pane (bar starts right under the
    // toolbar). Content height = top padding + rows + bottom padding, so the
    // thumb reaches the end exactly when the last row is visible.
    int rows = (static_cast<int>(entries.size()) + cols - 1) / cols;
    f32 contentH = kPad * 2.0f +
                   static_cast<f32>(rows) * (kTileH + kTileGap);
    m_gridScroll = ui.VerticalScroll(Slate::Context::ID("cb.grid"), rect,
                                     contentH, m_gridScroll);

    // Empty grid space (and gaps between tiles) drops into the open folder.
    bool overFolderTile = false;
    for (usize i = 0; i < entries.size(); ++i) {
        if (!entries[i]->IsFolder()) continue;
        const int row = static_cast<int>(i) / cols;
        const int col = static_cast<int>(i) % cols;
        Rect card{startX + col * colW, startY + row * (kTileH + kTileGap), kTileW, kTileH};
        if (card.Contains(ui.mouse()) && rect.Contains(ui.mouse())) overFolderTile = true;
    }
    if (!overFolderTile && rect.Contains(ui.mouse())) {
        m_importDropFolder = m_currentFolder.empty() && m_registry && !m_registry->Roots().empty()
                                 ? m_registry->Roots().front()
                                 : m_currentFolder;
        if (const Slate::DragPayload* drop = AcceptAssetDrop(ui, rect)) {
            MoveDropped(*drop, m_importDropFolder, ctx);
        }
    }
}

u64 ContentBrowserPanel::GetThumbnailTexture(const AssetId& assetId, const std::filesystem::path& nativePath, Luma::Renderer* renderer) {
    if (!renderer) {
        LUMA_LOG_ERROR("ContentBrowser", "GetThumbnailTexture: renderer is null");
        return 0;
    }

    // Check cache first
    auto it = m_thumbnailCache.find(assetId);
    if (it != m_thumbnailCache.end()) {
        return it->second;  // Return cached result (may be 0 if failed before)
    }
    // Producing a thumbnail can mean a GPU preview render; keep the frame
    // responsive and leave the rest for the next frames (tile shows its icon).
    if (!m_thumbBudget.TryStart(SteadyMs())) {
        return 0;
    }
    
    // Request or load thumbnail from thumbnail manager
    auto& thumbnailMgr = ThumbnailManager::Get();
    ThumbnailSettings settings;
    settings.width = 128;
    settings.height = 128;
    settings.autoRegenerate = true;
    
    LUMA_LOG_DEBUG("ContentBrowser", "GetThumbnailTexture() requesting thumbnail for asset {} from {}", 
                   ToString(assetId), nativePath.filename().string());
    
    auto thumbnail = thumbnailMgr.GetThumbnail(assetId, nativePath, settings);
    if (!thumbnail) {
        LUMA_LOG_WARN("ContentBrowser", "Failed to load thumbnail for asset {} from {}", 
                      ToString(assetId), nativePath.filename().string());
        m_thumbnailCache[assetId] = 0;
        return 0;
    }
    
    if (!thumbnail->IsValid()) {
        LUMA_LOG_WARN("ContentBrowser", "Thumbnail data invalid for asset {} (size: {}x{})", 
                      ToString(assetId), thumbnail->width, thumbnail->height);
        m_thumbnailCache[assetId] = 0;
        return 0;
    }
    
    // Upload thumbnail to GPU
    TextureHandle handle = renderer->CreateTexture(thumbnail->width, thumbnail->height, thumbnail->pixels.data());
    if (handle == 0) {
        LUMA_LOG_ERROR("ContentBrowser", "Failed to create GPU texture for thumbnail of asset {} ({}x{})", 
                       ToString(assetId), thumbnail->width, thumbnail->height);
        m_thumbnailCache[assetId] = 0;
        return 0;
    }
    
    // Cache successful result
    m_thumbnailCache[assetId] = handle;
    LUMA_LOG_DEBUG("ContentBrowser", "Cached thumbnail texture for asset {} (handle: {})", 
                   ToString(assetId), handle);
    return handle;
}

void ContentBrowserPanel::RequestThumbnail(const AssetId& assetId, const std::filesystem::path& nativePath) {
    // Request thumbnail from thumbnail manager
    auto& thumbnailMgr = ThumbnailManager::Get();
    thumbnailMgr.RequestThumbnail(assetId, nativePath);
    
    // Cache entry (will be updated when texture is uploaded)
    m_thumbnailCache[assetId] = 0;
}

void ContentBrowserPanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                               PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    
    
    if (!m_registry) {
        // Registry not wired yet — show a hint and return.
        ui.Panel(body, t.windowBg);
        ui.LabelIn({body.x, body.y + body.h * 0.5f - 11.0f, body.w, 22.0f},
                   "Content Browser: no asset registry wired.", t.textDim,
                   Align::Center);
        return;
    }
    UpdateThumbnailTask(ctx);
    m_thumbBudget.BeginFrame(SteadyMs());
    m_contextTarget = AssetId{};
    m_hovered = body.Contains(ui.rawMouse());
    Rect toolbar{body.x, body.y, body.w, kToolbarH};
    f32 bodyTop = toolbar.Bottom();
    // The content-browser body sits on a slightly lighter surface so the
    // dark tree panel reads as an inset panel "inside" the browser,
    // surrounded by a margin rather than flush with the edges.
    Rect bodyArea{body.x, bodyTop, body.w, body.Bottom() - bodyTop};
    ui.Panel(bodyArea, t.surface1);

    // Vertical splitter between the tree and the grid. The user can drag
    // the divider to resize the tree column (SplitterV clamps to a sane
    // range and sets m_treeRatio). The tree sits with a small margin
    // inside the left half; the grid fills the right half with the same
    // margin.
    Rect treeSplit{}, gridSplit{};
    // Keep the folder tree readable in narrow docks: at least ~120px (but
    // never more than 45% of the browser) whatever ratio was dragged to.
    if (bodyArea.w > 0.0f) {
        const f32 minRatio = std::min(0.45f, 120.0f / bodyArea.w);
        m_treeRatio = std::max(m_treeRatio, minRatio);
    }
    ui.SplitterV(Slate::Context::ID("cb.splitter"), bodyArea, m_treeRatio,
                 treeSplit, gridSplit, 1.0f);
    constexpr f32 kTreeMargin = 3.0f;
    Rect treePane{treeSplit.x + kTreeMargin, bodyTop + kTreeMargin,
                  treeSplit.w - kTreeMargin * 2.0f,
                  bodyArea.h - kTreeMargin * 2.0f};
    Rect gridPane{gridSplit.x + kTreeMargin, bodyTop,
                  gridSplit.w - kTreeMargin * 2.0f, bodyArea.h};

    m_importDropFolder.clear();
    if (body.Contains(ui.mouse())) m_importDropFolder = TargetFolder();
    DrawToolbar(ui, toolbar, ctx);
    // Reusable FileSystemTreePanel handles the dark inset folder list.
    // Keep its selection synced with our navigation state both ways.
    m_tree.SetRegistry(m_registry);
    m_tree.SetShowPluginContent(m_showPluginContent);
    m_tree.SetSelectedFolder(m_currentFolder);
    m_tree.Draw(ui, treePane);
    if (m_tree.SelectedFolder() != m_currentFolder) {
        m_currentFolder = m_tree.SelectedFolder();
    }
    std::filesystem::path treeClick;
    for (const FileSystemTreePanel::FolderRow& row : m_tree.Rows()) {
        if (row.rightClicked) treeClick = row.path;
        if (row.row.Contains(ui.mouse()) && treePane.Contains(ui.mouse())) {
            m_importDropFolder = row.path;
        }
        if (const Slate::DragPayload* drop = AcceptAssetDrop(ui, row.row)) {
            MoveDropped(*drop, row.path, ctx);
        }
    }
    DrawGridPane(ui, gridPane, ctx);

    if (m_renaming.IsValid()) {
        const AssetData* renaming = m_registry->Lookup(m_renaming);
        bool shownInGrid = false;
        for (const AssetData* entry : CurrentEntries()) {
            if (entry->id == m_renaming) shownInGrid = true;
        }
        if (renaming && !shownInGrid) {
            for (const FileSystemTreePanel::FolderRow& row : m_tree.Rows()) {
                if (row.path.lexically_normal() == renaming->packagePath.lexically_normal()) {
                    DrawRenameField(ui, row.label, *renaming, ctx);
                }
            }
        }
    }

    // Right-click: the asset under the cursor gets its asset menu; empty
    // grid / tree space gets the folder ("Add") menu, like Unreal's Content
    // Browser. The menu host draws it above every panel.
    if (ui.mousePressed(1) && bodyArea.Contains(ui.mouse())) {
        const AssetData* target =
            m_contextTarget.IsValid() ? m_registry->Lookup(m_contextTarget) : nullptr;
        if (!target && !treeClick.empty()) target = m_registry->LookupByPath(treeClick);
        if (target) {
            ui.OpenMenu(Slate::Context::ID("cb.assetmenu"), ui.mouse(),
                        BuildAssetMenu(*target, ctx));
        } else {
            Slate::MenuOptions mo;
            mo.searchable = true;
            mo.searchHint = "Search actions";
            ui.OpenMenu(Slate::Context::ID("cb.foldermenu"), ui.mouse(),
                        BuildFolderMenu(ctx), mo);
        }
    }

    // F2 renames the selected asset, or the open folder, while the pointer
    // is over the browser. The content root has no registry row and stays put.
    if (ui.keyPressed(291) && body.Contains(ui.mouse()) && !ui.textFieldFocused() &&
        !m_renaming.IsValid()) {
        const AssetData* sel = m_selected.IsValid() ? m_registry->Lookup(m_selected) : nullptr;
        if (!sel && !m_currentFolder.empty()) sel = m_registry->LookupByPath(m_currentFolder);
        if (sel) BeginRename(*sel);
    }
    FinishMutations(ctx);
}

std::filesystem::path ContentBrowserPanel::TargetFolder() const {
    std::filesystem::path target = m_currentFolder;
    if (target.empty() && m_registry && !m_registry->Roots().empty()) {
        target = m_registry->Roots().front();
    }
    return target;
}

void ContentBrowserPanel::CreateFolder() {
    const std::filesystem::path base = TargetFolder();
    if (base.empty()) return;
    std::filesystem::path folder = base / "NewFolder";
    int n = 1;
    std::error_code ec;
    while (std::filesystem::exists(folder, ec)) {
        folder = base / ("NewFolder " + std::to_string(n++));
    }
    if (std::filesystem::create_directories(folder, ec) && !ec) {
        if (m_registry) m_registry->Scan();
        NavigateTo(folder);
        LUMA_USER_INFO("Content", "Created folder '{}'", folder.filename().string());
    } else {
        LUMA_USER_ERROR("Content", "Could not create folder '{}': {}",
                       folder.string(), ec.message());
    }
}

void ContentBrowserPanel::BeginRename(const AssetData& asset) {
    m_renaming = asset.id;
    m_renameBuf = asset.assetName;
    m_renameFocusPending = true;
}

std::vector<Slate::MenuEntry> ContentBrowserPanel::BuildFilterMenu() {
    Slate::MenuBuilder b;
    b.BeginSection("Content");
    Slate::MenuEntryOptions pluginOpt;
    pluginOpt.checked = m_showPluginContent;
    b.AddEntry("Show Plugin Content", Icon::None,
               [this] { SetShowPluginContent(!m_showPluginContent); }, pluginOpt);
    b.BeginSection("Filter by Type");
    Slate::MenuEntryOptions all;
    all.checked = !AssetTypes().Find(m_typeFilter);
    b.AddEntry("All", Icon::None, [this] { m_typeFilter.clear(); }, all);
    for (const auto& desc : AssetTypes().All()) {
        if (desc->extensions.empty()) continue;
        Slate::MenuEntryOptions option;
        option.checked = m_typeFilter == desc->id;
        b.AddEntry(desc->displayName, MenuIconByName(desc->icon),
                   [this, id = desc->id] { m_typeFilter = id; }, option);
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> ContentBrowserPanel::BuildFolderMenu(PanelContext& ctx) {
    using Slate::Icon;
    const std::filesystem::path folder = TargetFolder();
    Slate::MenuBuilder b;

    b.BeginSection("Get");
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onImportAssets) && !folder.empty();
        o.tooltip = "Copy files from disk into this folder.";
        auto import = ctx.onImportAssets;
        b.AddEntry("Import to Current Folder...",
                   Slate::MenuIcon::Texture(m_texImport, Icon::Import),
                   [import, folder] {
                       if (import) import(folder);
                   },
                   o);
    }

    b.BeginSection("Create");
    {
        Slate::MenuEntryOptions o;
        o.enabled = !folder.empty();
        o.tooltip = "Create an empty folder here and open it.";
        b.AddEntry("New Folder", Icon::Folder, [this] { CreateFolder(); }, o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onCreateScene) && !folder.empty();
        o.tooltip = "Create an empty scene asset (.lscene) in this folder.";
        auto create = ctx.onCreateScene;
        b.AddEntry("Scene", Icon::Scene,
                   [create, folder] {
                       if (create) create(folder);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onCreateMaterial) && !folder.empty();
        o.tooltip = "Create a Material (Default Material values) in this folder and open it.";
        auto create = ctx.onCreateMaterial;
        b.AddEntry("Material", Icon::Sphere,
                   [create, folder] {
                       if (create) create(folder);
                   },
                   o);
    }
    {
        // Instance of the selected Material / Material Instance.
        std::string parent;
        if (const AssetData* sel = m_registry ? m_registry->Lookup(m_selected) : nullptr) {
            if ((sel->type == AssetType::Material || sel->type == AssetType::MaterialInstance) &&
                ctx.virtualPathOf) {
                parent = ctx.virtualPathOf(sel->packagePath);
            }
        }
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onCreateMaterialInstance) && !folder.empty() &&
                    !parent.empty();
        o.tooltip = parent.empty() ? "Select a Material first: the instance inherits from it."
                                   : "Create a Material Instance of '" + parent + "' here.";
        auto create = ctx.onCreateMaterialInstance;
        b.AddEntry("Material Instance", Icon::MaterialInstance,
                   [create, parent, folder] {
                       if (create) create(parent, folder);
                   },
                   o);
    }

    b.BeginSection("Folder");
    {
        const AssetData* here = m_registry ? m_registry->LookupByPath(folder) : nullptr;
        const AssetId id = here ? here->id : AssetId{};
        Slate::MenuEntryOptions o;
        o.enabled = id.IsValid();
        o.shortcut = "F2";
        o.tooltip = id.IsValid() ? "Rename this folder." : "The content root stays named Assets.";
        AssetRegistry* registry = m_registry;
        b.AddEntry("Rename", Icon::None,
                   [this, registry, id] {
                       if (!registry || !id.IsValid()) return;
                       if (const AssetData* asset = registry->Lookup(id)) BeginRename(*asset);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onRevealInExplorer) && !folder.empty();
        o.tooltip = "Open this folder in the system file browser.";
        auto reveal = ctx.onRevealInExplorer;
        b.AddEntry("Show in Explorer", Icon::Browse,
                   [reveal, folder] {
                       if (reveal) reveal(folder);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.tooltip = "Rescan the project's content from disk.";
        AssetRegistry* registry = m_registry;
        b.AddEntry("Refresh", Icon::Refresh,
                   [registry] {
                       if (registry) registry->Scan();
                   },
                   o);
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> ContentBrowserPanel::BuildAssetMenu(const AssetData& asset,
                                                                  PanelContext& ctx) {
    using Slate::Icon;
    const AssetId id = asset.id;
    const std::filesystem::path path = asset.packagePath;
    const bool isFolder = asset.IsFolder();
    AssetRegistry* registry = m_registry;
    Slate::MenuBuilder b;

    b.BeginSection(isFolder ? "Folder" : "Asset");
    if (isFolder) {
        b.AddEntry("Open", Icon::Browse, [this, path] { NavigateTo(path); });
    }
    const bool isMaterial =
        asset.type == AssetType::Material || asset.type == AssetType::MaterialInstance;
    const std::string virtualPath =
        ctx.virtualPathOf && !isFolder ? ctx.virtualPathOf(path) : std::string{};
    const auto desc = Describe(asset);
    if (desc->open) {
        AssetOpenContext openContext;
        if (ctx.onOpenAsset && !virtualPath.empty())
            openContext.openMaterial = [open = ctx.onOpenAsset, virtualPath](const auto&) { open(virtualPath); };
        Slate::MenuEntryOptions options;
        options.enabled = !isMaterial || static_cast<bool>(openContext.openMaterial);
        b.AddEntry("Open", MenuIconByName(desc->icon), [id = desc->id, path, openContext] {
            if (auto current = AssetTypes().Find(id); current && current->open) current->open(path, openContext);
        }, options);
    }
    if (isMaterial && !virtualPath.empty() && ctx.onOpenAssetWith) {
        auto openWith = ctx.onOpenAssetWith;
        const std::string materialIcon = desc->icon;
        b.AddSubMenu("Open With", Icon::Browse, [openWith, virtualPath, materialIcon](Slate::MenuBuilder& sub) {
            sub.AddEntry("Instance Editor", Icon::MaterialInstance,
                         [openWith, virtualPath] { openWith(virtualPath, "instance"); });
            sub.AddEntry("Material Editor", MenuIconByName(materialIcon),
                         [openWith, virtualPath] { openWith(virtualPath, "material"); });
        });
    }
    if (isMaterial && !virtualPath.empty()) {
        auto create = ctx.onCreateMaterialInstance;
        Slate::MenuEntryOptions oi;
        oi.enabled = static_cast<bool>(create);
        oi.tooltip = "Create a Material Instance that inherits from this material.";
        const std::filesystem::path folder = path.parent_path();
        b.AddEntry("Create Material Instance", Icon::MaterialInstance,
                   [create, virtualPath, folder] {
                       if (create) create(virtualPath, folder);
                   },
                   oi);
    }
    if (!isFolder && std::filesystem::exists(Import::MetadataPathFor(path)) && ctx.onReimport) {
        auto reimport = ctx.onReimport;
        Slate::MenuEntryOptions o;
        o.tooltip = "Import this asset again from its source file.";
        b.AddEntry("Reimport", Icon::Refresh,
                   [reimport, path] {
                       if (reimport) reimport(path);
                   },
                   o);
        if (ctx.sourceChanged && ctx.sourceChanged(path)) {
            b.AddEntry("Source changed — Reimport", Icon::Refresh,
                       [reimport, path] {
                           if (reimport) reimport(path);
                       });
        }
    }
    if (asset.type == AssetType::Texture) {
        auto create = ctx.onCreateMaterialFromTextures;
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(create);
        o.tooltip = "Create a Material wired to this texture set (textures in this folder "
                    "sharing its name prefix: diff/normal/rough/metal/ao/arm/emissive/opacity).";
        b.AddEntry("Create Material from Textures", Icon::Sphere,
                   [create, path] {
                       if (create) create(path);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.shortcut = "F2";
        b.AddEntry("Rename", Icon::None,
                   [this, registry, id] {
                       if (!registry) return;
                       if (const AssetData* a = registry->Lookup(id)) BeginRename(*a);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onDeleteAsset);
        o.tooltip = isFolder ? "Move this folder and everything in it to the Recycle Bin."
                             : "Move this file to the Recycle Bin.";
        auto del = ctx.onDeleteAsset;
        b.AddEntry("Delete", Icon::Trash,
                   [this, del, path, id] {
                       if (!del) return;
                       if (m_selected == id) m_selected = AssetId{};
                       m_thumbnailCache.erase(id);
                       del(path);
                   },
                   o);
    }

    b.BeginSection("Explore");
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onRevealInExplorer);
        o.tooltip = "Show this item in the system file browser.";
        auto reveal = ctx.onRevealInExplorer;
        b.AddEntry("Show in Explorer", Icon::Browse,
                   [reveal, path] {
                       if (reveal) reveal(path);
                   },
                   o);
    }
    return b.Build();
}

u64 ContentBrowserPanel::NowMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch())
        .count();
}

void ContentBrowserPanel::CommitRename(const AssetData& a, PanelContext& ctx) {
    std::string newName = m_renameBuf;
    m_renaming = AssetId{};
    if (newName.empty() || newName == a.assetName) return;
    if (newName == "." || newName == ".." ||
        newName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        LUMA_USER_ERROR("Content", "Can't rename '{}' to '{}'", a.assetName, newName);
        return;
    }

    namespace fs = std::filesystem;
    const fs::path newPackage = a.packagePath.parent_path() /
                                fs::path(newName + a.packagePath.extension().string());
    const bool renamingOpenFolder = m_currentFolder == a.packagePath;

    // Virtual paths before the rename (the file must still exist to map).
    const std::string oldVirtual = ctx.virtualPathOf ? ctx.virtualPathOf(a.packagePath) : std::string{};
    std::string problem;
    if (!Import::RenameAssetFile(a.packagePath, newPackage, &problem)) {
        LUMA_USER_ERROR("Content", "Could not rename '{}' to '{}': {}", a.assetName, newName, problem);
        return;
    }
    if (!problem.empty()) LUMA_USER_WARN("Content", "Renamed '{}', but {}", a.assetName, problem);

    LUMA_USER_INFO("Content", "Renamed '{}' to '{}'", a.assetName, newName);
    // Scenes and editors that reference the old path follow it. A folder
    // rename retargets every reference under that path.
    if (ctx.onAssetRenamed && ctx.virtualPathOf && !oldVirtual.empty()) {
        const std::string newVirtual = ctx.virtualPathOf(newPackage);
        if (!newVirtual.empty()) ctx.onAssetRenamed(oldVirtual, newVirtual);
    }
    m_thumbnailCache.erase(a.id);
    m_rescan = true;
    if (renamingOpenFolder) NavigateTo(newPackage);
}

void ContentBrowserPanel::DrawRenameField(Slate::Context& ui, const Slate::Rect& nameR,
                                          const AssetData& asset, PanelContext& ctx) {
    std::string buf = m_renameBuf;
    const u64 renameId = Slate::Context::ID((asset.packagePath.string() + "|rename").c_str());
    if (m_renameFocusPending) {
        ui.FocusField(renameId, buf.size());
        m_renameFocusPending = false;
    }
    if (ui.TextField(renameId, nameR, buf, "")) m_renameBuf = buf;
    if (ui.enterPressed()) {
        CommitRename(asset, ctx);
    } else if (ui.keyEscape()) {
        CancelRename();
    } else if (ui.mouseReleased(0) && !nameR.Contains(ui.mouse())) {
        CommitRename(asset, ctx);
    }
}

const Slate::DragPayload* ContentBrowserPanel::AcceptAssetDrop(Slate::Context& ui,
                                                               const Slate::Rect& target) const {
    static constexpr const char* kTypes[] = {
        "asset/material", "asset/texture", "asset/mesh", "asset/file", "asset/folder"};
    const Slate::DragPayload* hit = nullptr;
    for (const char* type : kTypes) {
        if (const Slate::DragPayload* drop = ui.AcceptDrop(target, type)) hit = drop;
    }
    return hit;
}

void ContentBrowserPanel::MoveDropped(const Slate::DragPayload& drop,
                                      const std::filesystem::path& folder, PanelContext&) {
    if (drop.sourcePath.empty() || folder.empty()) return;
    m_pendingMoveSource = drop.sourcePath;
    m_pendingMoveFolder = folder;
}

void ContentBrowserPanel::FinishMutations(PanelContext& ctx) {
    if (!m_pendingMoveSource.empty()) {
        const std::filesystem::path from(m_pendingMoveSource);
        m_pendingMoveSource.clear();
        const std::filesystem::path folder = m_pendingMoveFolder;
        m_pendingMoveFolder.clear();
        const std::string oldVirtual = ctx.virtualPathOf ? ctx.virtualPathOf(from) : std::string{};
        std::filesystem::path movedTo;
        std::string problem;
        if (!Import::MoveAssetToFolder(from, folder, &movedTo, &problem)) {
            if (!problem.empty()) LUMA_USER_ERROR("Content", "{}", problem);
        } else {
            LUMA_USER_INFO("Content", "Moved '{}' to '{}'", from.filename().string(),
                           folder.filename().string());
            if (ctx.onAssetRenamed && ctx.virtualPathOf && !oldVirtual.empty()) {
                const std::string newVirtual = ctx.virtualPathOf(movedTo);
                if (!newVirtual.empty()) ctx.onAssetRenamed(oldVirtual, newVirtual);
            }
            if (m_currentFolder == from) NavigateTo(movedTo);
            m_rescan = true;
        }
    }
    if (m_rescan && m_registry) m_registry->Scan();
    m_rescan = false;
}

void ContentBrowserPanel::CancelRename() noexcept {
    m_renaming = AssetId{};
}

}  // namespace Luma::Editor::Panels
