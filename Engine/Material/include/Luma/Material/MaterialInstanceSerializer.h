#pragma once

#include <filesystem>
#include <string>

#include "Luma/Material/MaterialInstance.h"
#include "Luma/Serialization/SerialValue.h"

// Persists a MaterialInstance as a `.lmatinst` asset (JSON). Only overridden
// inputs are written; the rest are inherited from the parent at resolve time.

namespace Luma::Material {

class MaterialInstanceSerializer {
public:
    static bool SaveToFile(const MaterialInstance& instance,
                           const std::filesystem::path& path,
                           std::string* outError = nullptr);
    static bool LoadFromFile(MaterialInstance& out, const std::filesystem::path& path,
                             std::string* outError = nullptr);
    static SerialValue ToJson(const MaterialInstance& instance);
    static bool FromJson(MaterialInstance& out, const SerialValue& value,
                         std::string* outError = nullptr);
};

}  // namespace Luma::Material
