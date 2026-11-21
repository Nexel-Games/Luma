#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Material/Material.h"
#include "Luma/MaterialGraph/Graph.h"

// Graph-based material assets (Material Editor project 1):
//
//   .lmat v4     GraphMaterial          settings + node graph
//   .lmatinst v4 GraphMaterialInstance  parent + parameter overrides by name
//   .lmatfunc    MaterialFunction       reusable sub-graph (Function Input/Output nodes)
//   .lmpc        ParameterCollection    global scalar/vector parameters
//
// Phase-1 (.lmat v3 / .lmatinst v1) assets keep loading through the legacy
// Material / MaterialInstance types until the renderer switches to compiled
// materials; v3 files convert to GraphMaterial via MaterialMigrationV4.h.

namespace Luma::Material {

struct GraphMaterial {
    std::string name;
    MaterialSettings settings;
    MaterialGraph::Graph graph;

    // The engine Default Material: BaseColor (0.82, 0.82, 0.85) and
    // Roughness 0.5 exposed as parameters so instances can tweak them.
    static GraphMaterial MakeDefault();
};

struct ParameterOverride {
    enum class Kind : u8 { Scalar, Vector, Texture, StaticSwitch };
    std::string name;
    Kind kind = Kind::Scalar;
    f32 scalar = 0.0f;
    Math::Vec4 vector{0.0f, 0.0f, 0.0f, 1.0f};
    std::string texture;       // virtual path
    bool staticSwitch = false;
};

struct GraphMaterialInstance {
    std::string name;
    std::string parent;  // virtual path of a GraphMaterial or another instance
    std::vector<ParameterOverride> overrides;
    // Settings an instance may override without recompiling.
    std::optional<bool> twoSided;
    std::optional<f32> opacityMaskClip;

    const ParameterOverride* Find(std::string_view name) const;
    void Set(ParameterOverride value);  // replaces an override with the same name
    bool Remove(std::string_view name);
};

struct MaterialFunction {
    std::string name;
    std::string description;
    std::string category = "Misc";
    bool exposeToLibrary = true;  // listed in the graph editor's palette
    MaterialGraph::Graph graph;   // its MaterialOutput node is unused
};

struct ParameterCollection {
    struct Scalar {
        std::string name;
        f32 value = 0.0f;
    };
    struct Vector {
        std::string name;
        Math::Vec4 value{0.0f, 0.0f, 0.0f, 0.0f};
    };
    std::string name;
    std::vector<Scalar> scalars;
    std::vector<Vector> vectors;
    u32 revision = 0;  // bumped on every value change (GPU re-upload)
};

// File I/O for the graph assets. Load fails (with a message) on malformed
// JSON, a wrong asset type, a newer version, or an invalid graph (id 0,
// duplicate ids, links to unknown nodes).
class GraphAssetSerializer {
public:
    static bool Save(const GraphMaterial&, const std::filesystem::path&, std::string* err);
    static bool Save(const GraphMaterialInstance&, const std::filesystem::path&, std::string* err);
    static bool Save(const MaterialFunction&, const std::filesystem::path&, std::string* err);
    static bool Save(const ParameterCollection&, const std::filesystem::path&, std::string* err);
    static bool Load(GraphMaterial&, const std::filesystem::path&, std::string* err);
    static bool Load(GraphMaterialInstance&, const std::filesystem::path&, std::string* err);
    static bool Load(MaterialFunction&, const std::filesystem::path&, std::string* err);
    static bool Load(ParameterCollection&, const std::filesystem::path&, std::string* err);
    // The "version" field of a material asset file (0 = missing/unreadable).
    static i64 ProbeVersion(const std::filesystem::path&);
};

inline constexpr i64 kGraphMaterialVersion = 4;
// Instances share version 4 with materials: Phase-1 instance files were
// written as version 3, so anything below 4 is the legacy format.
inline constexpr i64 kGraphInstanceVersion = 4;
inline constexpr i64 kFunctionVersion = 1;
inline constexpr i64 kCollectionVersion = 1;

}  // namespace Luma::Material
