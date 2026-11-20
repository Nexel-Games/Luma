#include "EditorScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

#include "Luma/Asset/LumaMeshFormat.h"
#include "Luma/Asset/MeshImporter.h"
#include "Luma/Asset/TextureImporter.h"
#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Core/Log.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Slate/Image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma {
using Slate::Align;
using Slate::Color;
using Slate::Rect;
using Slate::Vec2;

namespace {
// Default colors cycled for newly created entities.
const Math::Vec3 kEntityPalette[] = {
    {0.86f, 0.36f, 0.36f}, {0.40f, 0.72f, 0.92f}, {0.52f, 0.85f, 0.50f},
    {0.92f, 0.78f, 0.36f}, {0.74f, 0.52f, 0.92f}, {0.40f, 0.86f, 0.80f}};

// Returns `base` unchanged when no entity in the scene shares that name;
// otherwise appends the lowest free number (base 1, base 2, ...) so the first
// of a kind stays unnumbered and duplicates get distinguished.
std::string UniqueEntityName(Luma::Scene& scene, const std::string& base) {
    auto view = scene.Registry().view<const NameComponent>();
    auto taken = [&view](const std::string& name) {
        for (Entity e : view) {
            if (view.get<const NameComponent>(e).name == name) return true;
        }
        return false;
    };
    if (!taken(base)) return base;
    int n = 1;
    while (taken(base + " " + std::to_string(n))) ++n;
    return base + " " + std::to_string(n);
}
}  // namespace

EditorScreen::EditorScreen(const std::filesystem::path& projectFile) {
    std::string err;
    m_project = Project::Load(projectFile, &err);
    if (m_project) {
        m_title = m_project->Name() + " - Luma Editor";
        LUMA_USER_INFO("Project", "Opened project '{}'", m_project->Name());
    } else {
        m_title = "Luma Editor";
        LUMA_USER_ERROR("Project", "Could not open project: {}", err);
    }
    if (!LoadScene()) {
        CreateEnvironment();
        // New projects already declare a startup scene path. Materialize the
        // default environment scene on first open so the editor and runtime
        // operate on the same persisted scene instead of an in-memory fallback.
        SaveScene();
    }

    // Wire the content browser to an asset registry rooted at the project's
    // Assets/ folder (source assets live directly at the project root, not in
    // a Content/ subdirectory). The panel reads from this; the registry is
    // owned here so we can call Scan() on file-watcher events.
    if (m_project) {
        auto contentRoot = m_project->AssetsDir();
        std::error_code ec;
        if (std::filesystem::exists(contentRoot, ec)) {
            m_assetRegistry.AddRoot(contentRoot);
            m_assetRegistry.Scan();
            LUMA_LOG_INFO("Editor", "content root: {} ({} entries)",
                          contentRoot.string(), m_assetRegistry.Size());
        }
        m_contentBrowser.SetRegistry(&m_assetRegistry);

        // Route the thumbnail cache to <project>/Intermediate/Thumbnails so
        // thumbnails live in the project's scratch area, not inside Assets/.
        ThumbnailManager::Get().SetProjectRoot(m_project->RootDir());
        ThumbnailManager::Get().SetRegistry(&m_assetRegistry);
    }
}

void EditorScreen::CreateEnvironment() {
    m_environment = m_scene.CreateEntity("Environment");
    m_scene.Registry().emplace<EnvironmentComponent>(m_environment);
}

bool EditorScreen::LoadScene() {
    if (!m_project) return false;
    std::filesystem::path path = m_project->StartupScenePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    std::string err;
    if (!SceneSerializer::LoadFromFile(m_scene, path, &err)) {
        LUMA_USER_ERROR("Scene", "Failed to load scene '{}': {}", ProjectRelative(path),
                       err);
        return false;
    }
    m_selected = kNullEntity;
    m_outlinerPanel.ClearSelection();
    m_inspectorPanel.ClosePopups();
    auto envView = m_scene.Registry().view<EnvironmentComponent>();
    if (envView.begin() == envView.end()) CreateEnvironment();
    LUMA_USER_INFO("Scene", "Loaded scene '{}'", ProjectRelative(path));
    return true;
}

void EditorScreen::SaveScene() {
    if (!m_project) return;
    std::filesystem::path path = m_project->StartupScenePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string err;
    if (SceneSerializer::SaveToFile(m_scene, path, &err)) {
        LUMA_USER_INFO("Scene", "Saved scene '{}'", ProjectRelative(path));
    } else {
        LUMA_USER_ERROR("Scene", "Failed to save scene '{}': {}", ProjectRelative(path),
                       err);
    }
}

void EditorScreen::CaptureProjectPreview() {
    if (!m_project || !m_renderer) return;
    std::error_code ec;
    const std::filesystem::path dir =
        m_project->IntermediateDir() / "Thumbnails";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        LUMA_LOG_WARN("Editor", "failed to create project preview directory: {}",
                      ec.message());
        return;
    }
    m_renderer->CaptureFrame((dir / "ScenePreview.full.png").string());
}

void EditorScreen::FinalizeProjectPreview() {
    if (!m_project) return;
    const std::filesystem::path dir =
        m_project->IntermediateDir() / "Thumbnails";
    const std::filesystem::path fullPath = dir / "ScenePreview.full.png";
    const std::filesystem::path previewPath = dir / "ScenePreview.png";

    u32 sourceWidth = 0;
    u32 sourceHeight = 0;
    std::vector<u8> pixels;
    if (!Slate::LoadImagePixels(fullPath.string(), sourceWidth, sourceHeight,
                                pixels)) {
        return;
    }

    const Rect viewport = m_viewportPanel.Rect();
    const u32 x = std::min<u32>(sourceWidth, static_cast<u32>(
        std::max(0.0f, viewport.x)));
    const u32 y = std::min<u32>(sourceHeight, static_cast<u32>(
        std::max(0.0f, viewport.y)));
    const u32 width = std::min<u32>(sourceWidth - x, static_cast<u32>(
        std::max(1.0f, viewport.w)));
    const u32 height = std::min<u32>(sourceHeight - y, static_cast<u32>(
        std::max(1.0f, viewport.h)));
    if (width == 0 || height == 0) return;

    std::vector<u8> cropped(static_cast<usize>(width) * height * 4);
    for (u32 row = 0; row < height; ++row) {
        const u8* source = pixels.data() +
            (static_cast<usize>(y + row) * sourceWidth + x) * 4;
        u8* destination = cropped.data() + static_cast<usize>(row) * width * 4;
        std::memcpy(destination, source, static_cast<usize>(width) * 4);
    }
    stbi_write_png(previewPath.string().c_str(), static_cast<int>(width),
                   static_cast<int>(height), 4, cropped.data(),
                   static_cast<int>(width) * 4);
    std::error_code ec;
    std::filesystem::remove(fullPath, ec);
}

void EditorScreen::CreateActor(Editor::Panels::CreateActorKind kind) {
    int index = m_nextNumber++;
    const Math::Vec3 paletteColor = kEntityPalette[(index - 1) %
        (sizeof(kEntityPalette) / sizeof(Math::Vec3))];

    // --- Primitives / Empty -------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::Empty) {
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, "Empty Actor"));
        m_selected = e;
        return;
    }
    if (kind == Editor::Panels::CreateActorKind::Cube ||
        kind == Editor::Panels::CreateActorKind::Plane ||
        kind == Editor::Panels::CreateActorKind::Sphere ||
        kind == Editor::Panels::CreateActorKind::Cylinder) {
        const char* names[] = {"Cube", "Plane", "Sphere", "Cylinder"};
        const MeshPrimitive prims[] = {MeshPrimitive::Cube, MeshPrimitive::Plane,
                                       MeshPrimitive::Sphere,
                                       MeshPrimitive::Cylinder};
        int i = static_cast<int>(kind) - static_cast<int>(Editor::Panels::CreateActorKind::Cube);
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, names[i]));
        auto& mesh = m_scene.Registry().emplace<MeshRendererComponent>(e);
        mesh.primitive = prims[i];
        mesh.albedo = paletteColor;
        auto& tf = m_scene.Registry().get<TransformComponent>(e);
        tf.position = Math::Vec3(static_cast<f32>(index - 1) * 2.0f, 1.0f, 0.0f);
        m_selected = e;
        return;
    }

    // --- Lights -------------------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::DirectionalLight ||
        kind == Editor::Panels::CreateActorKind::PointLight ||
        kind == Editor::Panels::CreateActorKind::SpotLight ||
        kind == Editor::Panels::CreateActorKind::TubeLight) {
        const char* names[] = {"Directional Light", "Point Light",
                               "Spot Light", "Tube Light"};
        const LightType types[] = {LightType::Directional, LightType::Point,
                                   LightType::Spot, LightType::Tube};
        int i = static_cast<int>(kind) - static_cast<int>(Editor::Panels::CreateActorKind::DirectionalLight);
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, names[i]));
        auto& light = m_scene.Registry().emplace<LightComponent>(e);
        light.type = types[i];
        auto& tf = m_scene.Registry().get<TransformComponent>(e);
        // Lights sit above the origin. Directional defaults to a three-quarter
        // sun angle (pitch down, yawed) so the ground and meshes both catch
        // light instead of a horizon-grazing identity rotation.
        tf.position = Math::Vec3(static_cast<f32>(index - 1) * 3.0f, 4.0f, 2.0f);
        if (types[i] == LightType::Directional) {
            tf.rotationEuler = Math::Vec3(-50.0f, 35.0f, 0.0f);
        }
        m_selected = e;
        return;
    }

    // --- Environment --------------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::Environment) {
        auto envView = m_scene.Registry().view<EnvironmentComponent>();
        if (envView.begin() == envView.end()) {
            Entity e = m_scene.CreateEntity("Environment");
            m_scene.Registry().emplace<EnvironmentComponent>(e);
            m_selected = e;
        } else {
            // Only one environment per scene — select the existing one.
            m_selected = *envView.begin();
        }
        return;
    }

    // --- Post Process Volume -------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::PostProcessVolume) {
        Entity e = m_scene.CreateEntity(
            UniqueEntityName(m_scene, "Post Process Volume"));
        m_scene.Registry().emplace<PostProcessComponent>(e);
        m_selected = e;
        return;
    }
}

Editor::Panels::PanelContext EditorScreen::BuildPanelContext() {
    Editor::Panels::PanelContext ctx;
    ctx.renderer = m_renderer;
    ctx.assetRegistry = &m_assetRegistry;
    ctx.scene = &m_scene;
    ctx.selected = &m_selected;
    ctx.camYaw = &m_camYaw;
    ctx.camPitch = &m_camPitch;
    ctx.camDistance = &m_camDistance;
    ctx.camTarget = &m_camTarget;
    ctx.fovY = m_fovY;
    ctx.nearZ = m_nearZ;
    ctx.farZ = m_farZ;
    ctx.view = &m_view;
    ctx.gizmoScale = &m_gizmoScale;
    ctx.gizmo = &m_gizmo;
    ctx.onCreateActor = [this](Editor::Panels::CreateActorKind kind) {
        CreateActor(kind);
    };
    ctx.onSaveScene = [this] { SaveScene(); };
    ctx.onImportAssets = [this](const std::filesystem::path& folder) {
        ImportAssetsInto(folder);
    };
    ctx.onCreateScene = [this](const std::filesystem::path& folder) {
        CreateSceneAsset(folder);
    };
    ctx.onDeleteAsset = [this](const std::filesystem::path& path) {
        DeleteAssetPath(path);
    };
    ctx.onRevealInExplorer = [this](const std::filesystem::path& path) {
        RevealInExplorer(path);
    };
    ctx.onDuplicateEntity = [this](Entity e) { DuplicateEntity(e); };
    ctx.onDeleteEntity = [this](Entity e) { DeleteEntity(e); };
    ctx.onFocusEntity = [this](Entity e) { FocusEntity(e); };
    ctx.onPickRay = [this](const Math::Vec3& origin, const Math::Vec3& dir) {
        m_selected = PickEntity(origin, dir);
    };
    ctx.onResetCamera = [this] {
        m_camYaw = kDefaultCamYaw;
        m_camPitch = kDefaultCamPitch;
        m_camDistance = kDefaultCamDistance;
        m_camTarget = Math::Vec3(0.0f, 0.0f, 0.0f);
    };
    return ctx;
}

SceneView EditorScreen::BuildSceneView() {
    using namespace Math;
    Vec3 eye{m_camTarget.x + m_camDistance * std::cos(m_camPitch) *
                              std::sin(m_camYaw),
             m_camTarget.y + m_camDistance * std::sin(m_camPitch),
             m_camTarget.z + m_camDistance * std::cos(m_camPitch) *
                              std::cos(m_camYaw)};
    m_view = LookAt(eye, m_camTarget, Vec3(0.0f, 1.0f, 0.0f));
    // Constant on-screen gizmo size: scale with the eye-to-actor distance
    // (the fly camera moves the eye independently of the orbit distance) and
    // the field of view.
    {
        f32 dist = m_camDistance;
        if (m_scene.IsValid(m_selected) &&
            m_scene.Registry().all_of<TransformComponent>(m_selected)) {
            const Vec3 d = m_scene.Registry().get<TransformComponent>(m_selected).position - eye;
            dist = std::sqrt(Dot(d, d));
        }
        const f32 fovScale = std::tan(m_viewportPanel.Settings().fovYRadians * 0.5f) /
                             std::tan(0.45f);
        m_gizmoScale = std::max(dist, 0.05f) * 0.16f * fovScale;
    }

    m_instances.clear();
    auto view = m_scene.Registry().view<TransformComponent, MeshRendererComponent>();
    for (Entity e : view) {
        const auto& mr = view.get<MeshRendererComponent>(e);
        SceneInstance inst;
        inst.model = view.get<TransformComponent>(e).Matrix();
        inst.primitive = mr.primitive;
        
        // Load mesh asset if available
        if (mr.UsesMeshAsset() && m_project) {
            auto contentRoot = m_project->AssetsDir();
            auto assetEntry = m_assetRegistry.Lookup(mr.meshAsset);
            if (assetEntry) {
                auto meshPath = contentRoot / assetEntry->packagePath;
                
                // Check cache first
                auto it = m_meshCache.find(mr.meshAsset);
                if (it != m_meshCache.end()) {
                    // Use cached mesh data
                    const auto& meshData = it->second;
                    if (meshData.IsValid()) {
                        // Check if we have cached vertex positions
                        auto vertIt = m_vertexPositionCache.find(mr.meshAsset);
                        if (vertIt == m_vertexPositionCache.end()) {
                            // Extract and cache vertex positions
                            std::vector<Math::Vec3> positions;
                            positions.reserve(meshData.vertices.size());
                            for (const auto& v : meshData.vertices) {
                                positions.push_back(v.position);
                            }
                            m_vertexPositionCache[mr.meshAsset] = std::move(positions);
                            vertIt = m_vertexPositionCache.find(mr.meshAsset);
                        }
                        
                        // Validate data before using
                        if (vertIt->second.size() > 0 && vertIt->second.size() < 10000000) {
                            inst.customVertices = vertIt->second.data();
                            inst.customVertexCount = static_cast<u32>(vertIt->second.size());
                            inst.customIndices = meshData.indices.data();
                            inst.customIndexCount = static_cast<u32>(meshData.indices.size());
                            inst.customMeshValid = true;
                        } else {
                            LUMA_LOG_ERROR("Editor", "Invalid cached vertex data for asset");
                        }
                    }
                } else {
                    // Load and cache mesh
                    auto result = LumaMeshIO::ReadMesh(meshPath);
                    if (result && result->IsValid()) {
                        m_meshCache[mr.meshAsset] = std::move(*result);
                        const auto& cached = m_meshCache[mr.meshAsset];
                        
                        // Extract and cache vertex positions
                        std::vector<Math::Vec3> positions;
                        positions.reserve(cached.vertices.size());
                        for (const auto& v : cached.vertices) {
                            positions.push_back(v.position);
                        }
                        m_vertexPositionCache[mr.meshAsset] = std::move(positions);
                        
                        auto vertIt = m_vertexPositionCache.find(mr.meshAsset);
                        
                        // Validate data before using
                        if (vertIt->second.size() > 0 && vertIt->second.size() < 10000000) {
                            inst.customVertices = vertIt->second.data();
                            inst.customVertexCount = static_cast<u32>(vertIt->second.size());
                            inst.customIndices = cached.indices.data();
                            inst.customIndexCount = static_cast<u32>(cached.indices.size());
                            inst.customMeshValid = true;
                        } else {
                            LUMA_LOG_ERROR("Editor", "Invalid loaded vertex data for asset");
                        }
                    }
                }
            }
        }
        
        Vec3 c = mr.albedo;
        if (e == m_selected) {
            c = Vec3(std::min(1.0f, c.x + 0.20f), std::min(1.0f, c.y + 0.20f),
                     std::min(1.0f, c.z + 0.20f));
        }
        inst.albedo = c;
        inst.metallic = mr.metallic;
        inst.roughness = mr.roughness;
        m_instances.push_back(inst);
    }

    // The first directional light is the scene's sun: it drives the sky
    // atmosphere, the sun disk, cascaded shadows and the directional term in
    // every renderer. It is not added to the punctual list (it is rendered as
    // the sun instead). Point/spot lights (and any extra directional lights)
    // go into the punctual list.
    m_lights.clear();
    bool hasSun = false;
    SceneLight sun;
    auto lightView =
        m_scene.Registry().view<TransformComponent, LightComponent>();
    for (Entity e : lightView) {
        const auto& tf = lightView.get<TransformComponent>(e);
        const auto& lc = lightView.get<LightComponent>(e);
        Mat4 mtx = tf.Matrix();
        Vec3 fwd = Normalize(Vec3(-mtx.m[8], -mtx.m[9], -mtx.m[10]));
        SceneLight sl;
        sl.type = static_cast<u32>(lc.type);
        sl.position = tf.position;
        sl.direction = fwd;
        // The Inspector edits attenuationRadius/attenuationPower (the physical
        // falloff controls); those drive the light's reach and falloff curve.
        sl.range = lc.attenuationRadius;
        sl.falloffExponent = lc.attenuationPower;
        sl.color = lc.color;
        sl.intensity = lc.intensity;
        sl.cosInner = std::cos(Radians(lc.innerAngleDeg));
        sl.cosOuter = std::cos(Radians(lc.outerAngleDeg));
        sl.castShadows = lc.castShadows;
        sl.shadowBias = lc.shadowBias;
        sl.normalBias = lc.normalBias;
        sl.shadowSoftness = lc.shadowSoftness;
        sl.shadowMapSize = static_cast<u32>(lc.shadowMapSize);
        sl.cascadeCount = static_cast<u32>(lc.cascadeCount);
        sl.shadowDistance = lc.shadowDistance;
        sl.cascadeSplitLambda = lc.cascadeSplitLambda;
        sl.sunDiskSizeDeg = lc.sunDiskSizeDeg;
        sl.sunDiskIntensity = lc.sunDiskIntensity;
        if (lc.type == LightType::Directional && !hasSun) {
            // First directional light = sun (drives sky + lighting + shadows).
            hasSun = true;
            sun = sl;
            continue;
        }
        m_lights.push_back(sl);
    }

    SceneView scene;
    auto envView = m_scene.Registry().view<const EnvironmentComponent>();
    for (Entity e : envView) {
        const auto& env = envView.get<const EnvironmentComponent>(e);
        // --- Sky (physical atmosphere) ---
        scene.sky.enabled = env.skyEnabled;
        if (hasSun) {
            scene.sky.sunDirection = sun.direction;
            scene.sky.sunColor = sun.color;
            scene.sky.sunIntensity = sun.intensity;
            scene.sky.sunDiskSizeDeg = sun.sunDiskSizeDeg;
            scene.sky.sunDiskIntensity = sun.sunDiskIntensity;
        }
        scene.sky.rayleighScattering = env.rayleighScattering;
        scene.sky.rayleighScaleHeight = env.rayleighScaleHeight;
        scene.sky.mieScattering = env.mieScattering;
        scene.sky.mieAbsorption = env.mieAbsorption;
        scene.sky.mieScaleHeight = env.mieScaleHeight;
        scene.sky.mieAnisotropy = env.mieAnisotropy;
        scene.sky.ozoneScale = env.ozoneScale;
        scene.sky.skyIntensity = env.skyIntensity;
        scene.sky.saturation = env.saturation;
        scene.sky.exposure = env.exposure;
        scene.sky.skyTint = env.skyTint;
        scene.sky.groundColor = env.groundColor;
        // --- Lighting (sun + IBL ambient) ---
        if (hasSun) {
            scene.lighting.sunDirection = sun.direction;
            scene.lighting.sunColor = sun.color;
            scene.lighting.sunIntensity = sun.intensity;
            scene.lighting.sunShadows = sun.castShadows;
            scene.lighting.shadowDistance = sun.shadowDistance;
            scene.lighting.shadowSoftness = sun.shadowSoftness;
            scene.lighting.shadowBias = sun.shadowBias;
            scene.lighting.normalBias = sun.normalBias;
            scene.lighting.cascadeSplitLambda = sun.cascadeSplitLambda;
            scene.lighting.cascadeCount = sun.cascadeCount;
        }
        scene.lighting.groundColor = env.groundColor;
        scene.lighting.iblIntensity = env.iblIntensity;
        // Analytic skylight colours (Unreal-style hemisphere): a deep zenith,
        // a brighter horizon, and the environment ground bounce.
        scene.lighting.skyZenith = Math::Vec3(
            0.14f * env.skyTint.x, 0.30f * env.skyTint.y, 0.64f * env.skyTint.z);
        scene.lighting.skyHorizon = Math::Vec3(
            0.58f * env.skyTint.x, 0.66f * env.skyTint.y, 0.80f * env.skyTint.z);
        break;
    }

    // Post-processing: the highest-priority enabled PostProcessComponent wins.
    // Volume extents/blend radius are not implemented yet, so the winner is
    // applied globally (like Unreal's "Infinite Extent (Unbound)" volumes).
    {
        const PostProcessComponent* active = nullptr;
        auto ppView = m_scene.Registry().view<const PostProcessComponent>();
        for (Entity e : ppView) {
            const auto& pp = ppView.get<const PostProcessComponent>(e);
            if (pp.enabled && (!active || pp.priority > active->priority)) active = &pp;
        }
        if (active) {
            const f32 w = std::clamp(active->blendWeight, 0.0f, 1.0f);
            const PostProcessParams defaults{};
            PostProcessParams& out = scene.postProcess;
            out.bloomEnabled = active->bloomEnabled;
            out.bloomIntensity = active->bloomIntensity * w;
            out.bloomThreshold = active->bloomThreshold;
            out.bloomSoftThreshold = active->bloomSoftThreshold;
            out.bloomTint = active->bloomTint;
            out.toneMapper =
                static_cast<PostProcessParams::ToneMapper>(active->toneMapper);
            // blendWeight fades exposure toward the renderer default.
            out.exposure = defaults.exposure +
                           (active->exposure - defaults.exposure) * w;
            out.autoExposure = active->autoExposure;
            out.autoExposureSpeed = active->autoExposureSpeed;
            out.autoExposureMinLuminance = active->autoExposureMin;
            out.autoExposureMaxLuminance = active->autoExposureMax;
        }
    }

    // Viewport toolbar settings (view mode, show flags, FOV).
    const auto& vp = m_viewportPanel.Settings();
    m_fovY = vp.fovYRadians;
    scene.viewMode = vp.viewMode;
    if (!vp.showBloom) scene.postProcess.bloomEnabled = false;
    if (!vp.showEyeAdaptation) scene.postProcess.autoExposure = false;

    scene.grid.enabled = vp.showGrid;
    scene.grid.cellSize = 1.0f;
    scene.grid.majorEvery = 10;
    scene.grid.fadeStart = m_camDistance * 3.0f;
    scene.grid.fadeEnd = m_camDistance * 60.0f;
    scene.view = m_view;
    scene.fovYRadians = m_fovY;
    scene.nearZ = m_nearZ;
    scene.farZ = m_farZ;
    scene.instances = m_instances.data();
    scene.instanceCount = static_cast<u32>(m_instances.size());
    scene.lights = m_lights.data();
    scene.lightCount = static_cast<u32>(m_lights.size());

    if (m_scene.IsValid(m_selected) &&
        m_scene.Registry().all_of<TransformComponent>(m_selected)) {
        const auto& tf = m_scene.Registry().get<TransformComponent>(m_selected);
        GizmoTransform gt{tf.position, tf.rotationEuler, tf.scale};
        const auto& lines = m_gizmo.BuildLines(gt, m_gizmoScale, m_view);
        scene.overlayLines = lines.data();
        scene.overlayLineVertexCount = static_cast<u32>(lines.size());
    }
    return scene;
}

void EditorScreen::BuildDock() {
    m_dock.AddPanel("outliner", "World Outliner",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_outlinerPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabOutliner);
    m_dock.AddPanel("viewport", "Viewport",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_viewportPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabViewport);
    m_dock.AddPanel("inspector", "Inspector",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_inspectorPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabDetails);
    m_dock.AddPanel("console", "Console",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_consolePanel.Draw(c, r, ctx);
                    },
                    m_icons.tabOutputLog);
    m_dock.AddPanel("content", "Content Browser",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_contentBrowser.Draw(c, r, ctx);
                    },
                    m_icons.tabContent);
    m_dock.DockRoot("viewport");
    m_dock.DockWith("console", "viewport", Slate::DockDir::Down, 0.28f);
    m_dock.DockWith("outliner", "viewport", Slate::DockDir::Left, 0.2f);
    m_dock.DockWith("inspector", "viewport", Slate::DockDir::Right, 0.24f);
    m_dock.DockWith("content", "outliner", Slate::DockDir::Down, 0.45f);
    m_dockBuilt = true;
}

Renderer2::DeferredSceneView EditorScreen::BuildDeferredSceneView(u32 width,
                                                                u32 height,
                                                                f32 deltaSeconds) {
    using namespace Math;
    
    // Build the RHI scene view first. Store it in a member so its instance /
    // light pointers stay valid for the lifetime of the returned view.
    m_deferredSceneSource = BuildSceneView();
    const SceneView& rhiScene = m_deferredSceneSource;
    
    // Convert to Renderer2 DeferredSceneView
    Renderer2::DeferredSceneView deferredScene;
    deferredScene.cameraPosition = Vec3(m_camTarget.x + m_camDistance * std::cos(m_camPitch) * std::sin(m_camYaw),
                                       m_camTarget.y + m_camDistance * std::sin(m_camPitch),
                                       m_camTarget.z + m_camDistance * std::cos(m_camPitch) * std::cos(m_camYaw));
    deferredScene.cameraDirection = Normalize(m_camTarget - deferredScene.cameraPosition);
    deferredScene.viewMatrix = m_view;

    const u32 vpW = width > 0 ? width : 1;
    const u32 vpH = height > 0 ? height : 1;
    deferredScene.width = vpW;
    deferredScene.height = vpH;
    // Vertical FOV is fixed; aspect comes from the live viewport rect so
    // meshes keep their proportions when the panel is resized.
    deferredScene.projectionMatrix =
        PerspectiveForViewport(m_fovY, static_cast<f32>(vpW),
                               static_cast<f32>(vpH), m_nearZ, m_farZ);
    deferredScene.viewProjectionMatrix =
        deferredScene.projectionMatrix * deferredScene.viewMatrix;
    deferredScene.fov = m_fovY;
    deferredScene.nearPlane = m_nearZ;
    deferredScene.farPlane = m_farZ;
    deferredScene.time = 0.0f; // TODO: Pass actual time
    // Clamp so a hitch (or a long first frame) doesn't jump adaptation.
    deferredScene.deltaTime = std::clamp(deltaSeconds, 0.0f, 0.25f);
    
    // Set pointers to RHI scene data
    deferredScene.sceneData = &rhiScene;
    deferredScene.lightingParams = &rhiScene.lighting;
    
    return deferredScene;
}

void EditorScreen::HandleDroppedFiles(const std::vector<std::filesystem::path>& files) {
    if (files.empty() || !m_project) return;

    std::filesystem::path targetDir = m_contentBrowser.CurrentFolder();
    if (targetDir.empty() || !std::filesystem::exists(targetDir)) {
        targetDir = m_project->AssetsDir();
    }
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);

    LUMA_LOG_INFO("Editor", "Handling dropped assets/packs ({}) into target folder {}", files.size(), targetDir.string());

    auto processSingleFile = [&](const std::filesystem::path& srcPath, const std::filesystem::path& destDir) {
        std::error_code copyEc;
        std::filesystem::path destPath = destDir / srcPath.filename();

        // Copy source file to target directory if not already there
        if (std::filesystem::canonical(srcPath, copyEc) != std::filesystem::canonical(destPath, copyEc)) {
            std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, copyEc);
            if (copyEc) {
                LUMA_USER_ERROR("Content", "Could not import '{}': {}", srcPath.filename().string(), copyEc.message());
                return;
            }
        }
        LUMA_LOG_INFO("Editor", "Copied dropped file to {}", destPath.string());
    };

    for (const auto& file : files) {
        if (std::filesystem::is_directory(file)) {
            // Recursive copy for directories
            std::filesystem::path destDir = targetDir / file.filename();
            std::error_code copyEc;
            std::filesystem::copy(file, destDir, std::filesystem::copy_options::recursive, copyEc);
            if (copyEc) {
                LUMA_USER_ERROR("Content", "Could not import folder '{}': {}", file.filename().string(), copyEc.message());
            } else {
                LUMA_LOG_INFO("Editor", "Copied dropped directory to {}", destDir.string());
            }
        } else {
            processSingleFile(file, targetDir);
        }
    }

    // Refresh the registry after dropping files
    m_assetRegistry.Scan();
    LUMA_LOG_INFO("Editor", "Registry refreshed after drop ({} entries)", m_assetRegistry.Size());
    LUMA_USER_INFO("Content", "Imported {} item(s) into '{}'", files.size(),
                   ProjectRelative(targetDir));
}

void EditorScreen::ImportAssetsInto(const std::filesystem::path& folder) {
    if (!m_project) return;
    std::filesystem::path target = folder;
    if (target.empty()) target = m_project->AssetsDir();
    std::error_code ec;
    std::filesystem::create_directories(target, ec);

    // Native multi-select file picker (Windows).
    std::vector<std::filesystem::path> picked;
#ifdef _WIN32
    {
        char buffer[16384] = {0};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer));
        ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
        ofn.lpstrTitle = "Import Assets";
        if (GetOpenFileNameA(&ofn)) {
            // With OFN_ALLOWMULTISELECT + OFN_EXPLORER the buffer is
            // "dir\0file1\0file2\0\0" for several files, or a single full
            // path when exactly one file is chosen.
            const char* p = buffer;
            std::string first(p);
            p += first.size() + 1;
            if (*p == '\0') {
                picked.push_back(first);
            } else {
                const std::filesystem::path dir(first);
                while (*p != '\0') {
                    picked.push_back(dir / p);
                    p += std::strlen(p) + 1;
                }
            }
        }
    }
#endif
    if (picked.empty()) return;

    LUMA_LOG_INFO("Editor", "Importing {} file(s) into {}", picked.size(),
                  target.string());
    usize failed = 0;
    for (const auto& src : picked) {
        std::error_code copyEc;
        if (!std::filesystem::exists(src, copyEc) || copyEc) {
            continue;  // no longer exists — skip
        }
        std::filesystem::path dest = target / src.filename();
        copyEc.clear();
        std::filesystem::path srcReal = std::filesystem::canonical(src, copyEc);
        if (copyEc) continue;
        copyEc.clear();
        std::filesystem::path destReal =
            std::filesystem::canonical(dest, copyEc);
        if (!copyEc && srcReal == destReal) continue;  // already there
        copyEc.clear();
        std::filesystem::copy_file(src, dest,
                                   std::filesystem::copy_options::overwrite_existing,
                                   copyEc);
        if (copyEc) {
            ++failed;
            LUMA_USER_ERROR("Content", "Could not import '{}' -> {}: {}",
                           src.string(), dest.string(), copyEc.message());
        }
    }

    m_assetRegistry.Scan();
    if (picked.size() > failed) {
        LUMA_USER_INFO("Content", "Imported {} file(s) into '{}'", picked.size() - failed,
                       ProjectRelative(target));
    }
    LUMA_LOG_INFO("Editor", "Registry refreshed after import ({} entries)",
                  m_assetRegistry.Size());
}

void EditorScreen::Draw(Slate::Context& ui, f32 width, f32 height) {
    Slate::Theme& t = ui.theme();
    if (!m_dockBuilt) BuildDock();
    ui.Panel({0, 0, width, height}, t.windowBg);

    Rect menu{0, 0, width, 32};
    ui.GradientRect(menu, t.surface2, t.surface1);
    ui.Panel({0, 31, width, 1}, t.separator);

    if (m_iconLogo) {
        ui.Image(m_iconLogo, {8, 4, 24, 24});
    } else {
        ui.LogoMark({20, 16}, 9.0f);
    }
    f32 mx = 42.0f;

    // Main menu bar (Unreal-style): each title drops a Slate menu.
    const char* items[] = {"File", "Edit", "Assets", "Window"};
    constexpr int kMenuCount = static_cast<int>(std::size(items));
    u64 ids[kMenuCount];
    Rect rects[kMenuCount];
    int openIndex = -1;
    for (int i = 0; i < kMenuCount; ++i) {
        f32 w = ui.uiFont().Measure(items[i]).x + 22.0f;
        ids[i] = Slate::Context::ID(items[i]) ^ 0x4D454E55ull;
        rects[i] = {mx, 4, w, 24};
        if (ui.IsMenuOpen(ids[i])) openIndex = i;
        mx += w + 2.0f;
    }
    // While one bar menu is open, hovering another title switches to it
    // (the bar reads the raw pointer: widgets are input-blocked by the menu).
    if (openIndex >= 0) {
        for (int i = 0; i < kMenuCount; ++i) {
            if (i != openIndex && rects[i].Contains(ui.rawMouse())) {
                const Rect& r = rects[i];
                ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f}, BuildMainMenu(i));
                openIndex = i;
                break;
            }
        }
    }
    for (int i = 0; i < kMenuCount; ++i) {
        const Rect& r = rects[i];
        if (ui.MenuButton(ids[i], r, items[i])) {
            ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f}, BuildMainMenu(i));
        }
        if (i == openIndex) {
            ui.drawList().AddRectFilledRounded(r, t.menu.hoverFill, t.radius.sm);
            ui.LabelIn(r, items[i], t.text, Slate::Align::Center);
        }
    }

    std::string projName = m_project ? m_project->Name() : "(no project)";
    f32 nameW = ui.uiFont().Measure(projName).x;
    // [dot][name] pill: 24px lead-in for the dot, 12px trailing padding.
    f32 pillW = nameW + 36.0f;
    Rect pill{width - pillW - 10.0f, 5, pillW, 22};
    ui.PanelRoundedBordered(pill, t.surface1, t.outline,
                            t.radius.pill, t.border.hairline);
    ui.PanelRounded({pill.x + 11.0f, pill.y + 7.0f, 7.0f, 7.0f}, t.accent, 3.5f);
    // Heading insets its text by space.lg, so start the rect that much
    // earlier and give it room for the whole name.
    ui.Heading({pill.x + 24.0f - t.space.lg, pill.y, nameW + t.space.lg * 2.0f, pill.h},
               projName, t.text);

    Rect toolbar{0, 32, width, 36};
    ui.GradientRect(toolbar, t.surface1, t.surface0);
    ui.Panel({0, 67, width, 1}, t.separator);
    // Left side of the main toolbar: Save (Ctrl+S), like Unreal's toolbar.
    if (m_icons.save) {
        const Rect saveR{10, 35, 30, 28};
        if (ui.IconButton(Slate::Context::ID("toolbar.save"), saveR, m_icons.save)) {
            SaveScene();
        }
        ui.Tooltip(Slate::Context::ID("tip.save"), saveR,
                   "Save Scene (Ctrl+S)\nWrite the open scene to disk.");
    }
    const Rect playR{width / 2 - 46, 35, 30, 28};
    const Rect pauseR{width / 2 - 14, 35, 30, 28};
    const Rect stopR{width / 2 + 18, 35, 30, 28};
    ui.IconButton(Slate::Context::ID("play"), playR, m_iconPlay);
    ui.IconButton(Slate::Context::ID("pause"), pauseR, m_iconPause);
    ui.IconButton(Slate::Context::ID("stop"), stopR, m_iconStop);
    ui.Tooltip(Slate::Context::ID("tip.play"), playR,
               "Play\nPlay-in-editor is not implemented yet.");
    ui.Tooltip(Slate::Context::ID("tip.pause"), pauseR,
               "Pause\nPlay-in-editor is not implemented yet.");
    ui.Tooltip(Slate::Context::ID("tip.stop"), stopR,
               "Stop\nPlay-in-editor is not implemented yet.");

    m_dock.Draw(ui, {0, 68.0f, width, height - 68.0f});
    HandleShortcuts(ui);

    // Content Browser double-click: open the right editor for the activated
    // asset. Materials open the Material Editor (docked lazily on first use).
    {
        AssetId activated = m_contentBrowser.Activated();
        m_contentBrowser.ClearActivated();
        if (activated.IsValid()) {
            (void)m_assetRegistry.Lookup(activated);
        }
    }

    // Floating color pickers — drawn after the dock so the panels aren't
    // clipped to the inspector column (the dock scissor-clips every panel).
    {
        auto ctx = BuildPanelContext();
        m_inspectorPanel.DrawFloatingPickers(ui, ctx);
    }
}

Entity EditorScreen::PickEntity(const Math::Vec3& origin, const Math::Vec3& dir) const {
    using namespace Math;
    auto& reg = const_cast<Scene&>(m_scene).Registry();
    Entity best = kNullEntity;
    f32 bestT = 1e30f;
    // Slab test of a ray against an AABB; returns the entry parameter.
    auto rayBox = [](const Vec3& o, const Vec3& d, const Vec3& lo, const Vec3& hi, f32& t) {
        f32 tmin = -1e30f, tmax = 1e30f;
        const f32 os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
        const f32 los[3] = {lo.x, lo.y, lo.z}, his[3] = {hi.x, hi.y, hi.z};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(ds[i]) < 1e-9f) {
                if (os[i] < los[i] || os[i] > his[i]) return false;
                continue;
            }
            f32 t0 = (los[i] - os[i]) / ds[i], t1 = (his[i] - os[i]) / ds[i];
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
        if (tmax < 0.0f) return false;
        t = tmin >= 0.0f ? tmin : tmax;
        return true;
    };
    auto transformPoint = [](const Mat4& m, const Vec3& p) {
        return Vec3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                    m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                    m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
    };
    auto transformDir = [](const Mat4& m, const Vec3& v) {
        return Vec3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
                    m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
                    m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
    };

    auto view = reg.view<const TransformComponent>();
    for (Entity e : view) {
        const auto& tf = view.get<const TransformComponent>(e);
        f32 t = 0.0f;
        bool hit = false;
        if (const auto* mr = reg.try_get<MeshRendererComponent>(e)) {
            // Local-space bounds, tested against the ray in local space (the
            // unnormalized local direction keeps t in world ray units).
            Vec3 lo{-0.5f, -0.5f, -0.5f}, hi{0.5f, 0.5f, 0.5f};
            if (mr->UsesMeshAsset()) {
                auto it = m_vertexPositionCache.find(mr->meshAsset);
                if (it != m_vertexPositionCache.end() && !it->second.empty()) {
                    lo = hi = it->second.front();
                    for (const Vec3& p : it->second) {
                        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
                    }
                }
            } else if (mr->primitive == MeshPrimitive::Plane) {
                lo.y = -0.02f;
                hi.y = 0.02f;
            }
            const Mat4 inv = Inverse(tf.Matrix());
            hit = rayBox(transformPoint(inv, origin), transformDir(inv, dir), lo, hi, t);
        } else {
            // Non-mesh actors (lights, cameras, volumes...): a small sphere.
            constexpr f32 kRadius = 0.35f;
            const Vec3 oc = origin - tf.position;
            const f32 b = Dot(oc, dir);
            const f32 c = Dot(oc, oc) - kRadius * kRadius;
            const f32 disc = b * b - c;
            if (disc >= 0.0f) {
                t = -b - std::sqrt(disc);
                hit = t >= 0.0f;
            }
        }
        if (hit && t < bestT) {
            bestT = t;
            best = e;
        }
    }
    return best;
}

std::string EditorScreen::ProjectRelative(const std::filesystem::path& path) const {
    if (!m_project) return path.string();
    std::error_code ec;
    const std::filesystem::path root = m_project->AssetsDir().parent_path();
    std::filesystem::path rel = std::filesystem::relative(path, root, ec);
    if (ec || rel.empty() || rel.native().rfind(L"..", 0) == 0) return path.string();
    return rel.generic_string();
}

void EditorScreen::HandleShortcuts(Slate::Context& ui) {
    // Editor-wide shortcuts. Text entry owns the keyboard while focused, and
    // an open menu hides key presses from this pass entirely.
    if (ui.textFieldFocused() || ui.AnyMenuOpen()) return;
    constexpr int kKeyS = 83, kKeyD = 68, kKeyF = 70;
    const bool ctrl = ui.ctrlDown();
    if (ctrl && ui.keyPressed(kKeyS)) {
        SaveScene();
        return;
    }
    // Transform tools (Unreal W / E / R). While the right mouse button is
    // held these keys fly the camera instead (handled by the viewport).
    constexpr int kKeyW = 87, kKeyE = 69, kKeyR = 82;
    if (!ctrl && !ui.isMouseDown(1)) {
        if (ui.keyPressed(kKeyW)) m_gizmo.SetMode(GizmoMode::Translate);
        if (ui.keyPressed(kKeyE)) m_gizmo.SetMode(GizmoMode::Rotate);
        if (ui.keyPressed(kKeyR)) m_gizmo.SetMode(GizmoMode::Scale);
    }
    if (!m_scene.IsValid(m_selected)) return;
    if (ctrl && ui.keyPressed(kKeyD)) {
        DuplicateEntity(m_selected);
    } else if (!ctrl && ui.keyPressed(kKeyF)) {
        FocusEntity(m_selected);
    } else if (ui.keyDelete() && !m_contentBrowser.Hovered()) {
        // Over the Content Browser, Delete belongs to assets, not actors.
        DeleteEntity(m_selected);
    }
}

void EditorScreen::DuplicateEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    if (m_scene.Registry().all_of<EnvironmentComponent>(e)) return;  // one per scene
    const std::string base = m_scene.Registry().get<NameComponent>(e).name;
    Entity copy = m_scene.DuplicateEntity(e);
    if (copy == kNullEntity) return;
    const std::string name = UniqueEntityName(m_scene, base);
    m_scene.Registry().get<NameComponent>(copy).name = name;
    m_selected = copy;
    LUMA_USER_INFO("Scene", "Duplicated '{}' as '{}'", base, name);
}

void EditorScreen::DeleteEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    if (m_selected == e) {
        m_inspectorPanel.ClosePopups();
        m_selected = kNullEntity;
    }
    const std::string name = m_scene.Registry().get<NameComponent>(e).name;
    m_scene.DestroyEntity(e);
    LUMA_USER_INFO("Scene", "Deleted '{}'", name);
}

void EditorScreen::FocusEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    const auto& tf = m_scene.Registry().get<TransformComponent>(e);
    m_camTarget = tf.position;
    // Frame by the entity's largest scale axis (unit primitives).
    const f32 extent = std::max({std::fabs(tf.scale.x), std::fabs(tf.scale.y),
                                 std::fabs(tf.scale.z), 1.0f});
    m_camDistance = std::clamp(extent * 4.0f, 3.0f, m_farZ * 0.5f);
    m_selected = e;
}

void EditorScreen::CreateSceneAsset(const std::filesystem::path& folder) {
    if (folder.empty()) return;
    std::error_code ec;
    std::filesystem::path path = folder / "NewScene.lscene";
    for (int n = 1; std::filesystem::exists(path, ec); ++n) {
        path = folder / ("NewScene" + std::to_string(n) + ".lscene");
    }
    Scene empty;
    std::string err;
    if (!SceneSerializer::SaveToFile(empty, path, &err)) {
        LUMA_USER_ERROR("Content", "Could not create scene '{}': {}", ProjectRelative(path), err);
        return;
    }
    m_assetRegistry.RefreshPath(path);
    LUMA_USER_INFO("Content", "Created scene '{}'", ProjectRelative(path));
}

void EditorScreen::DeleteAssetPath(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;
#ifdef _WIN32
    // Recycle Bin (recoverable), never a permanent delete. SHFileOperation
    // wants a double-NUL-terminated wide path list.
    std::wstring from = std::filesystem::absolute(path, ec).wstring();
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&op);
    if (rc != 0 || op.fAnyOperationsAborted) {
        LUMA_USER_ERROR("Content", "Could not delete '{}' (code {})",
                        ProjectRelative(path), rc);
        return;
    }
    LUMA_USER_INFO("Content", "Deleted '{}' (moved to the Recycle Bin)", ProjectRelative(path));
#else
    LUMA_USER_ERROR("Content", "Delete is only supported on Windows (no recycle bin)");
    return;
#endif
    m_assetRegistry.RemoveUnder(path.parent_path());
    m_assetRegistry.RefreshPath(path.parent_path());
    m_assetRegistry.Scan();
}

void EditorScreen::RevealInExplorer(const std::filesystem::path& path) {
#ifdef _WIN32
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(path, ec);
    std::wstring args = std::filesystem::is_directory(abs, ec)
                            ? L"\"" + abs.wstring() + L"\""
                            : L"/select,\"" + abs.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                  SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

std::vector<Slate::MenuEntry> EditorScreen::BuildMainMenu(int index) {
    using Slate::Icon;
    Slate::MenuBuilder b;
    const bool hasSel = m_scene.IsValid(m_selected);
    auto when = [](bool enabled, const char* tip = "") {
        Slate::MenuEntryOptions o;
        o.enabled = enabled;
        o.tooltip = tip;
        return o;
    };
    switch (index) {
        case 0:  // File
            b.BeginSection("Scene");
            {
                auto o = when(m_project.has_value(), "Write the open scene to disk.");
                o.shortcut = "Ctrl+S";
                b.AddEntry("Save Scene", Icon::Save, [this] { SaveScene(); }, o);
            }
            b.BeginSection("Project");
            b.AddEntry("Show Project in Explorer", Icon::FolderOpen,
                       [this] {
                           if (m_project) RevealInExplorer(m_project->AssetsDir().parent_path());
                       },
                       when(m_project.has_value()));
            break;
        case 1:  // Edit
            b.BeginSection("Selection");
            {
                auto o = when(hasSel && !m_scene.Registry().all_of<EnvironmentComponent>(m_selected));
                o.shortcut = "Ctrl+D";
                b.AddEntry("Duplicate", Icon::Plus, [this] { DuplicateEntity(m_selected); }, o);
            }
            {
                auto o = when(hasSel);
                o.shortcut = "Delete";
                b.AddEntry("Delete", Icon::Trash, [this] { DeleteEntity(m_selected); }, o);
            }
            {
                auto o = when(hasSel);
                o.shortcut = "F";
                b.AddEntry("Focus in Viewport", Icon::Eye, [this] { FocusEntity(m_selected); }, o);
            }
            break;
        case 2:  // Assets
            b.BeginSection("Content");
            b.AddEntry("Import...", Icon::Plus,
                       [this] { ImportAssetsInto(m_contentBrowser.CurrentFolder()); },
                       when(m_project.has_value(), "Import files into the open Content Browser folder."));
            b.AddEntry("Refresh Content", Icon::Refresh, [this] { m_assetRegistry.Scan(); },
                       when(true, "Rescan the project's content from disk."));
            break;
        case 3:  // Window
            b.BeginSection("Layout");
            b.AddEntry("Reset Layout", Icon::Grip,
                       [this] {
                           m_dock = Slate::DockSpace{};
                           m_dockBuilt = false;
                       },
                       when(true, "Restore the default panel arrangement."));
            break;
        default:
            break;
    }
    return b.Build();
}

}  // namespace Luma
