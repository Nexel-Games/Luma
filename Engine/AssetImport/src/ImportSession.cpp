#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/AssetImport/ImportSession.h"
#include "Luma/AssetImport/SourceWatch.h"
#include "Luma/AssetImport/Translators/ModelTranslator.h"

#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/Core/Log.h"

namespace Luma::Import {
namespace {

void RestoreSettings(PipelineStack& settings, const SerialValue& stored, AssetType type) {
    if (stored.Find("Common") != nullptr || stored.Find("Meshes") != nullptr || stored.Find("Materials") != nullptr ||
        stored.Find("Textures") != nullptr || stored.Find("Audio") != nullptr) {
        settings.FromJson(stored);
        return;
    }
    const auto desc = AssetTypes().FindBuiltin(type);
    if (desc && !desc->importSection.empty()) {
        auto sections = SerialValue::MakeObject();
        sections[desc->importSection] = stored;
        settings.FromJson(sections);
    }
}

}  // namespace

ImportSession::ImportSession() : m_registry(Translators()) {}

ImportSession::~ImportSession() {
    Cancel();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ImportSession::Translate(const std::vector<std::filesystem::path>& files, std::string* err) {
    if (err != nullptr) {
        *err = {};
    }
    m_scene = ImportScene{};
    for (const std::filesystem::path& file : files) {
        auto translator = m_registry.Acquire(file);
        if (translator == nullptr) {
            const std::string message = "No importer for '" + file.filename().string() + "'";
            if (err != nullptr) {
                *err = message;
            }
            LUMA_USER_ERROR("Import", "{}", message);
            return false;
        }
        if (!translator->Translate(file, m_scene, err)) {
            return false;
        }
    }
    LUMA_LOG_INFO("Import", "Translated {} file(s) into {} nodes", files.size(), m_scene.nodes.size());
    return true;
}

ImportScene& ImportSession::Scene() { return m_scene; }

PipelineStack& ImportSession::Settings() { return m_settings; }

ImportPlan ImportSession::Plan(const std::filesystem::path& destFolder) const {
    return m_settings.BuildPlan(m_scene, destFolder);
}

void ImportSession::Start(const ImportPlan& plan, const FactoryContext& base) {
    if (m_thread.joinable()) {
        Cancel();
        m_thread.join();
    }
    m_plan = plan;
    m_materials = base.materials;
    m_paths = &base.paths;
    m_boundScene = &base.scene;
    m_boundSettings = &base.settings;
    m_userCancel = base.cancelled;
    m_cancel.store(false);
    m_taken = false;
    m_cancelledRun = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.clear();
        m_materialQueue.clear();
    }
    m_progress.store(0.0f);
    m_running.store(true);
    m_thread = std::thread([this] { Run(); });
}

f32 ImportSession::Progress() const { return m_progress.load(); }

bool ImportSession::Running() const { return m_running.load(std::memory_order_acquire); }

void ImportSession::Cancel() { m_cancel.store(true); }

bool ImportSession::IsCancelled() const {
    if (m_cancel.load()) {
        return true;
    }
    return static_cast<bool>(m_userCancel) && m_userCancel();
}

FactoryContext ImportSession::Context() const {
    return FactoryContext{*m_boundScene, *m_boundSettings, m_materials, *m_paths, [this] { return IsCancelled(); }};
}

void ImportSession::Record(ImportSummary& summary, const AssetRequest& request, const FactoryResult& result) const {
    if (!result.ok) {
        ++summary.failed;
        if (!result.error.empty()) {
            summary.messages.push_back(result.error);
        }
        return;
    }
    if (result.skipped) {
        ++summary.skipped;
    } else if (request.exists && request.policy == ConflictPolicy::Overwrite) {
        ++summary.overwritten;
    } else {
        ++summary.created;
    }
    if (!result.file.empty()) {
        summary.files.push_back(result.file);
    }
}

void ImportSession::Run() {
    std::vector<Finished> done;
    std::vector<AssetRequest> materials;
    bool cancelled = false;
    const size_t total = m_plan.requests.size();
    // One parse per source model for the whole run (a 100-mesh FBX is read
    // once, not 100 times).
    ModelPayloadCache models;
    FactoryContext context = Context();
    context.modelCache = &models;
    for (size_t index = 0; index < total; ++index) {
        if (IsCancelled()) {
            cancelled = true;
            break;
        }
        const AssetRequest& request = m_plan.requests[index];
        if (request.type == AssetType::Material) {
            materials.push_back(request);
        } else {
            FactoryResult result = CreateAsset(request, context);
            if (!result.ok && result.error.find("cancel") != std::string::npos) {
                cancelled = true;
            }
            done.push_back(Finished{request, std::move(result)});
            if (cancelled) {
                m_progress.store(static_cast<float>(index + 1) / static_cast<float>(total));
                break;
            }
        }
        m_progress.store(static_cast<float>(index + 1) / static_cast<float>(total));
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done = std::move(done);
        m_materialQueue = std::move(materials);
        m_cancelledRun = cancelled;
    }
    if (!cancelled && total > 0) {
        m_progress.store(1.0f);
    }
    if (total == 0) {
        m_progress.store(1.0f);
    }
    m_running.store(false, std::memory_order_release);
}

std::optional<ImportSummary> ImportSession::TakeResult() {
    if (m_running.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    if (m_taken) {
        return std::nullopt;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }

    ImportSummary summary;
    summary.warnings = m_plan.warnings;
    std::vector<Finished> done;
    std::vector<AssetRequest> materials;
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        done = std::move(m_done);
        materials = std::move(m_materialQueue);
        cancelled = m_cancelledRun;
    }
    for (const Finished& item : done) {
        Record(summary, item.request, item.result);
    }
    if (!cancelled && m_boundScene != nullptr && m_paths != nullptr) {
        for (const AssetRequest& request : materials) {
            if (IsCancelled()) {
                cancelled = true;
                break;
            }
            Record(summary, request, CreateAsset(request, Context()));
        }
    }
    if (cancelled) {
        bool reported = false;
        for (const std::string& message : summary.messages) {
            if (message.find("cancel") != std::string::npos) {
                reported = true;
            }
        }
        if (!reported) {
            summary.messages.emplace_back("Import was cancelled");
        }
    }
    m_taken = true;
    return summary;
}

std::unique_ptr<ImportSession> ImportSession::ForReimport(const std::filesystem::path& assetFile, std::string* err) {
    if (err != nullptr) {
        *err = {};
    }
    const std::optional<ImportMetadata> meta = ReadMetadata(assetFile);
    if (!meta) {
        if (err != nullptr) {
            *err = "No import metadata for '" + assetFile.filename().string() + "'";
        }
        return nullptr;
    }
    std::error_code io;
    if (meta->sourceFile.empty() || !std::filesystem::exists(meta->sourceFile, io)) {
        if (err != nullptr) {
            *err = "Import source for '" + assetFile.filename().string() + "' is missing";
        }
        return nullptr;
    }
    auto session = std::make_unique<ImportSession>();
    if (!session->Translate({meta->sourceFile}, err)) {
        return nullptr;
    }
    RestoreSettings(session->Settings(), meta->settings, meta->type);
    return session;
}

bool SourceChanged(const std::filesystem::path& assetFile) {
    return CheckSource(assetFile) == SourceState::Changed;
}

}  // namespace Luma::Import
