#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Luma/AssetImport/Factories/FactoryCommon.h"
#include "Luma/AssetImport/TranslatorRegistry.h"
#include "Luma/Core/Types.h"

namespace Luma::Import {

struct ImportSummary {
    u32 created = 0;
    u32 overwritten = 0;
    u32 skipped = 0;
    u32 failed = 0;
    std::vector<std::string> messages;  // failures
    std::vector<std::string> warnings;  // e.g. a referenced texture that is missing
    std::vector<std::filesystem::path> files;
};

// Phase 1 (Translate / Plan) is synchronous. Phase 2 (Start) runs mesh, texture,
// and sound factories on a worker thread.
//
// MaterialLibrary is not thread-safe. Material requests are queued by the worker
// and created on the calling thread inside TakeResult, before the summary is returned.
class ImportSession {
public:
    ImportSession();
    ~ImportSession();

    ImportSession(const ImportSession&) = delete;
    ImportSession& operator=(const ImportSession&) = delete;

    bool Translate(const std::vector<std::filesystem::path>& files, std::string* err);
    ImportScene& Scene();
    PipelineStack& Settings();
    ImportPlan Plan(const std::filesystem::path& destFolder) const;

    // The worker reads `base` (scene, settings, materials, paths, cancelled).
    // Those objects must outlive the run. TakeResult joins the worker.
    void Start(const ImportPlan& plan, const FactoryContext& base);
    f32 Progress() const;
    bool Running() const;
    void Cancel();
    // Empty while the worker is still running, and empty after the summary has been taken.
    std::optional<ImportSummary> TakeResult();

    // Translates the source recorded in the asset's .meta and restores pipeline settings.
    static std::unique_ptr<ImportSession> ForReimport(const std::filesystem::path& assetFile, std::string* err);

private:
    struct Finished {
        AssetRequest request;
        FactoryResult result;
    };

    void Run();
    bool IsCancelled() const;
    FactoryContext Context() const;
    void Record(ImportSummary& summary, const AssetRequest& request, const FactoryResult& result) const;

    TranslatorRegistry m_registry;
    ImportScene m_scene;
    PipelineStack m_settings;
    ImportPlan m_plan;
    Material::MaterialLibrary* m_materials = nullptr;
    const AssetPathResolver* m_paths = nullptr;
    const ImportScene* m_boundScene = nullptr;
    const PipelineStack* m_boundSettings = nullptr;
    std::function<bool()> m_userCancel;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
    std::atomic<float> m_progress{0.0f};
    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<Finished> m_done;
    std::vector<AssetRequest> m_materialQueue;
    bool m_cancelledRun = false;
    bool m_taken = false;
};

// True when the source file's hash or mtime no longer matches the asset's .meta.
bool SourceChanged(const std::filesystem::path& assetFile);

}  // namespace Luma::Import
