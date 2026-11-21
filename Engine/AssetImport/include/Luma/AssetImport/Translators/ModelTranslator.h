#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/AssetImport/ImportScene.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Math/Math.h"

namespace Luma::Import {

class ITranslator {
public:
    virtual ~ITranslator() = default;
    virtual std::string_view Name() const = 0;
    virtual bool CanTranslate(const std::filesystem::path& file) const = 0;
    // Adds a File node and its children. Does not create assets.
    virtual bool Translate(const std::filesystem::path& file, ImportScene& out,
                           std::string* error) = 0;
};

struct ModelPayloadOptions {
    f32 uniformScale = 1.0f;
    Luma::Math::Vec3 offsetTranslation{0, 0, 0};
    Luma::Math::Vec3 offsetRotationDeg{0, 0, 0};
    bool importNormals = true;      // false = always compute
    f32 smoothingAngleDeg = 66.0f;  // when computing normals
    bool computeTangents = true;
    bool flipUVs = false;  // extra flip on top of the format convention fix
    bool combineMeshes = false;
    enum class Pivot : u8 { Source, BoundsCenter, BoundsBottom };
    Pivot pivot = Pivot::Source;
    enum class AxisOverride : u8 { Auto, YUp, ZUp };
    AxisOverride axis = AxisOverride::Auto;
};

// Parsed source models kept for one import run: a file with many meshes is
// read once instead of once per mesh asset. Holds the most recent model only
// (requests are grouped by file), so memory stays bounded. Not thread-safe;
// one per import worker.
class ModelPayloadCache {
public:
    ModelPayloadCache();
    ~ModelPayloadCache();
    ModelPayloadCache(const ModelPayloadCache&) = delete;
    ModelPayloadCache& operator=(const ModelPayloadCache&) = delete;
    u32 Reads() const { return m_reads; }  // files actually parsed

private:
    friend class ModelTranslator;
    struct Entry;
    std::unique_ptr<Entry> m_entry;
    u32 m_reads = 0;
};

class ModelTranslator final : public ITranslator {
public:
    std::string_view Name() const override;
    bool CanTranslate(const std::filesystem::path& file) const override;
    bool Translate(const std::filesystem::path& file, ImportScene& out,
                   std::string* error) override;

    // One LumaMeshData per mesh node, or a single mesh with one section per
    // material when combineMeshes is set.
    std::vector<Luma::LumaMeshData> BuildMeshes(const std::filesystem::path& file,
                                                const std::vector<std::string>& meshNodeNames,
                                                const ModelPayloadOptions& options,
                                                std::string* error,
                                                ModelPayloadCache* cache = nullptr);
};

}  // namespace Luma::Import
