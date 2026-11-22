#include "Luma/Material/GraphAssets.h"

#include <algorithm>
#include <set>

#include "Luma/Serialization/Json.h"
#include "MaterialJson.h"

namespace Luma::Material {

using MaterialGraph::Graph;
using MaterialGraph::NodeId;
using MaterialGraph::PropertyValue;

// --- Asset helpers ----------------------------------------------------------

GraphMaterial GraphMaterial::MakeDefault() {
    GraphMaterial m;
    m.name = "DefaultMaterial";
    Graph& g = m.graph;
    const NodeId color = g.AddNode("VectorParameter", {-320.0f, -40.0f});
    g.Find(color)->properties["name"] = PropertyValue(std::string("BaseColor"));
    g.Find(color)->properties["default"] = PropertyValue(Math::Vec4{0.82f, 0.82f, 0.85f, 1.0f});
    const NodeId rough = g.AddNode("ScalarParameter", {-320.0f, 120.0f});
    g.Find(rough)->properties["name"] = PropertyValue(std::string("Roughness"));
    g.Find(rough)->properties["default"] = PropertyValue(0.5);
    g.Find(rough)->properties["sliderMin"] = PropertyValue(0.0);
    g.Find(rough)->properties["sliderMax"] = PropertyValue(1.0);
    g.Connect({color, 0}, {g.OutputNode(), 0}, nullptr);  // RGB -> Base Color
    g.Connect({rough, 0}, {g.OutputNode(), 3}, nullptr);  // -> Roughness
    return m;
}

const ParameterOverride* GraphMaterialInstance::Find(std::string_view n) const {
    for (const ParameterOverride& o : overrides) {
        if (o.name == n) return &o;
    }
    return nullptr;
}

void GraphMaterialInstance::Set(ParameterOverride value) {
    for (ParameterOverride& o : overrides) {
        if (o.name == value.name) {
            o = std::move(value);
            return;
        }
    }
    overrides.push_back(std::move(value));
}

bool GraphMaterialInstance::Remove(std::string_view n) {
    const auto before = overrides.size();
    std::erase_if(overrides, [n](const ParameterOverride& o) { return o.name == n; });
    return overrides.size() != before;
}

// --- JSON -------------------------------------------------------------------

namespace {

template <typename E, usize N>
const char* EnumName(E v, const char* const (&names)[N]) {
    const auto i = static_cast<usize>(v);
    return i < N ? names[i] : names[0];
}
template <typename E, usize N>
E EnumParse(const SerialValue* v, const char* const (&names)[N], E def) {
    if (!v || !v->IsString()) return def;
    const std::string s = v->AsString();
    for (usize i = 0; i < N; ++i) {
        if (s == names[i]) return static_cast<E>(i);
    }
    return def;
}

constexpr const char* kDomainNames[] = {"Surface", "PostProcess"};
constexpr const char* kBlendNames[] = {"Opaque", "Masked", "Translucent", "Additive", "Modulate"};
constexpr const char* kShadingNames[] = {"DefaultLit", "Unlit", "Subsurface", "ClearCoat",
                                         "TwoSidedFoliage"};
constexpr const char* kTranslucencyNames[] = {"SurfacePerPixel", "Unlit"};
constexpr const char* kRefractionNames[] = {"None", "IndexOfRefraction", "PixelOffset"};
constexpr const char* kPpLocationNames[] = {"BeforeTonemapping", "AfterTonemapping"};
constexpr const char* kOverrideKindNames[] = {"Scalar", "Vector", "Texture", "StaticSwitch"};

SerialValue SettingsToJson(const MaterialSettings& s) {
    SerialValue v = SerialValue::MakeObject();
    v["domain"] = SerialValue(EnumName(s.domain, kDomainNames));
    v["blendMode"] = SerialValue(EnumName(s.blendMode, kBlendNames));
    v["shadingModel"] = SerialValue(EnumName(s.shadingModel, kShadingNames));
    v["twoSided"] = SerialValue(s.twoSided);
    v["opacityMaskClip"] = SerialValue(static_cast<f64>(s.opacityMaskClip));
    v["translucencyLighting"] = SerialValue(EnumName(s.translucencyLighting, kTranslucencyNames));
    v["refraction"] = SerialValue(EnumName(s.refraction, kRefractionNames));
    v["postProcessLocation"] = SerialValue(EnumName(s.ppLocation, kPpLocationNames));
    v["postProcessPriority"] = SerialValue(static_cast<i64>(s.ppPriority));
    return v;
}

void SettingsFromJson(const SerialValue* v, MaterialSettings& s) {
    if (!v || !v->IsObject()) return;
    s.domain = EnumParse(v->Find("domain"), kDomainNames, s.domain);
    s.blendMode = EnumParse(v->Find("blendMode"), kBlendNames, s.blendMode);
    s.shadingModel = EnumParse(v->Find("shadingModel"), kShadingNames, s.shadingModel);
    if (auto* t = v->Find("twoSided")) s.twoSided = t->AsBool(s.twoSided);
    if (auto* c = v->Find("opacityMaskClip")) s.opacityMaskClip = static_cast<f32>(c->AsFloat(s.opacityMaskClip));
    s.translucencyLighting =
        EnumParse(v->Find("translucencyLighting"), kTranslucencyNames, s.translucencyLighting);
    s.refraction = EnumParse(v->Find("refraction"), kRefractionNames, s.refraction);
    s.ppLocation = EnumParse(v->Find("postProcessLocation"), kPpLocationNames, s.ppLocation);
    if (auto* p = v->Find("postProcessPriority")) s.ppPriority = static_cast<i32>(p->AsInt(s.ppPriority));
}

// Properties are tagged so their kind survives the round trip.
SerialValue PropertyToJson(const PropertyValue& p) {
    SerialValue v = SerialValue::MakeObject();
    if (p.IsBool()) v["b"] = SerialValue(p.AsBool());
    else if (p.IsInt()) v["i"] = SerialValue(p.AsInt());
    else if (p.IsVec4()) v["v"] = Json::Vec4Value(p.AsVec4());
    else if (p.IsString()) v["s"] = SerialValue(p.AsString());
    else v["f"] = SerialValue(p.AsFloat());
    return v;
}

bool PropertyFromJson(const SerialValue& v, PropertyValue& out) {
    if (auto* b = v.Find("b")) out = PropertyValue(b->AsBool());
    else if (auto* i = v.Find("i")) out = PropertyValue(i->AsInt());
    else if (auto* f = v.Find("f")) out = PropertyValue(f->AsFloat());
    else if (auto* s = v.Find("s")) out = PropertyValue(s->AsString());
    else if (auto* vec = v.Find("v")) {
        Math::Vec4 x{0, 0, 0, 0};
        if (!Json::ReadVec4(*vec, x)) return false;
        out = PropertyValue(x);
    } else {
        return false;
    }
    return true;
}

SerialValue GraphToJson(const Graph& g) {
    SerialValue out = SerialValue::MakeObject();
    out["nextId"] = SerialValue(static_cast<i64>(g.NextId()));
    SerialValue nodes = SerialValue::MakeArray();
    for (const auto& n : g.Nodes()) {
        SerialValue nv = SerialValue::MakeObject();
        nv["id"] = SerialValue(static_cast<i64>(n.id));
        nv["type"] = SerialValue(n.type);
        nv["position"] = Json::Vec2Value(n.position);
        if (!n.comment.empty()) nv["comment"] = SerialValue(n.comment);
        if (!n.properties.empty()) {
            SerialValue props = SerialValue::MakeObject();
            for (const auto& [k, p] : n.properties) props[k] = PropertyToJson(p);
            nv["properties"] = std::move(props);
        }
        nodes.PushBack(std::move(nv));
    }
    out["nodes"] = std::move(nodes);
    SerialValue links = SerialValue::MakeArray();
    for (const auto& l : g.Links()) {
        SerialValue lv = SerialValue::MakeObject();
        SerialValue from = SerialValue::MakeArray();
        from.PushBack(SerialValue(static_cast<i64>(l.from.node)));
        from.PushBack(SerialValue(static_cast<i64>(l.from.pin)));
        SerialValue to = SerialValue::MakeArray();
        to.PushBack(SerialValue(static_cast<i64>(l.to.node)));
        to.PushBack(SerialValue(static_cast<i64>(l.to.pin)));
        lv["from"] = std::move(from);
        lv["to"] = std::move(to);
        links.PushBack(std::move(lv));
    }
    out["links"] = std::move(links);
    if (!g.Comments().empty()) {
        SerialValue comments = SerialValue::MakeArray();
        for (const auto& c : g.Comments()) {
            SerialValue cv = SerialValue::MakeObject();
            cv["position"] = Json::Vec2Value(c.position);
            cv["size"] = Json::Vec2Value(c.size);
            cv["text"] = SerialValue(c.text);
            cv["color"] = Json::Vec4Value(c.color);
            comments.PushBack(std::move(cv));
        }
        out["comments"] = std::move(comments);
    }
    return out;
}

bool GraphFromJson(const SerialValue* v, Graph& g, std::string* err) {
    auto fail = [err](std::string m) {
        if (err) *err = std::move(m);
        return false;
    };
    g = Graph();
    if (!v || !v->IsObject()) return fail("missing 'graph'");
    const SerialValue* nodes = v->Find("nodes");
    if (!nodes || !nodes->IsArray()) return fail("graph has no 'nodes' array");
    std::set<NodeId> ids;
    // The Material Output node goes first: it re-identifies the graph's
    // built-in output node, so no other node can collide with its old id.
    std::vector<const SerialValue*> ordered;
    for (const SerialValue& nv : nodes->Elements()) {
        const bool isOutput = nv.Find("type") && nv.Find("type")->AsString() == Graph::kOutputType;
        if (isOutput) ordered.insert(ordered.begin(), &nv);
        else ordered.push_back(&nv);
    }
    for (const SerialValue* nvp : ordered) {
        const SerialValue& nv = *nvp;
        const i64 id = nv.Find("id") ? nv.Find("id")->AsInt(0) : 0;
        const std::string type = nv.Find("type") ? nv.Find("type")->AsString() : std::string{};
        if (id <= 0) return fail("node with invalid id " + std::to_string(id));
        if (type.empty()) return fail("node " + std::to_string(id) + " has no type");
        if (!ids.insert(static_cast<NodeId>(id)).second) return fail("duplicate node id " + std::to_string(id));
        Math::Vec2 pos{0, 0};
        if (auto* p = nv.Find("position")) Json::ReadVec2(*p, pos);
        if (!g.AddNodeWithId(static_cast<NodeId>(id), type, pos)) {
            return fail("node " + std::to_string(id) + " could not be added");
        }
        auto* node = g.Find(static_cast<NodeId>(id));
        if (auto* c = nv.Find("comment")) node->comment = c->AsString();
        if (auto* props = nv.Find("properties"); props && props->IsObject()) {
            for (const auto& member : props->Members()) {
                PropertyValue pv;
                if (!PropertyFromJson(member.second, pv)) {
                    return fail("node " + std::to_string(id) + " property '" + member.first + "' is malformed");
                }
                node->properties[member.first] = std::move(pv);
            }
        }
    }
    if (auto* links = v->Find("links"); links && links->IsArray()) {
        for (const SerialValue& lv : links->Elements()) {
            const SerialValue* from = lv.Find("from");
            const SerialValue* to = lv.Find("to");
            if (!from || !to || from->Size() < 2 || to->Size() < 2) return fail("malformed link");
            const MaterialGraph::PinRef f{static_cast<NodeId>(from->At(0).AsInt(0)),
                                          static_cast<u32>(from->At(1).AsInt(0))};
            const MaterialGraph::PinRef t{static_cast<NodeId>(to->At(0).AsInt(0)),
                                          static_cast<u32>(to->At(1).AsInt(0))};
            std::string linkErr;
            if (!g.Connect(f, t, &linkErr)) return fail("link: " + linkErr);
        }
    }
    if (auto* comments = v->Find("comments"); comments && comments->IsArray()) {
        for (const SerialValue& cv : comments->Elements()) {
            Math::Vec2 pos{0, 0}, size{200, 120};
            if (auto* p = cv.Find("position")) Json::ReadVec2(*p, pos);
            if (auto* s = cv.Find("size")) Json::ReadVec2(*s, size);
            const u64 id = g.AddComment(pos, size, cv.Find("text") ? cv.Find("text")->AsString() : "");
            if (auto* c = cv.Find("color")) {
                for (auto& com : g.Comments()) {
                    if (com.id == id) Json::ReadVec4(*c, com.color);
                }
            }
        }
    }
    return true;
}

SerialValue Header(const char* type, i64 version, const std::string& name) {
    SerialValue v = SerialValue::MakeObject();
    v["version"] = SerialValue(version);
    v["type"] = SerialValue(type);
    v["name"] = SerialValue(name);
    return v;
}

std::optional<SerialValue> ReadAsset(const std::filesystem::path& path, const char* type,
                                     i64 maxVersion, i64 minVersion, std::string* err) {
    auto doc = Json::ReadFile(path, err);
    if (!doc) return std::nullopt;
    const std::string t = doc->Find("type") ? doc->Find("type")->AsString() : std::string{};
    if (t != type) {
        if (err) *err = path.filename().string() + " is not a " + type;
        return std::nullopt;
    }
    const i64 version = doc->Find("version") ? doc->Find("version")->AsInt(0) : 0;
    if (version > maxVersion || version < minVersion) {
        if (err) {
            *err = path.filename().string() + ": unsupported " + type + " version " +
                   std::to_string(version);
        }
        return std::nullopt;
    }
    return doc;
}

std::string NameOf(const SerialValue& doc) {
    return doc.Find("name") ? doc.Find("name")->AsString() : std::string{};
}

}  // namespace

std::string GraphToJsonText(const Graph& graph) { return ::Luma::WriteJson(GraphToJson(graph), false); }

bool GraphFromJsonText(std::string_view text, Graph& graph, std::string* err) {
    auto doc = ::Luma::ParseJson(text, err);
    if (!doc) return false;
    return GraphFromJson(&*doc, graph, err);
}

// --- Serializer ---------------------------------------------------------------

bool GraphAssetSerializer::Save(const GraphMaterial& m, const std::filesystem::path& path,
                                std::string* err) {
    SerialValue v = Header("Material", kGraphMaterialVersion, m.name);
    v["settings"] = SettingsToJson(m.settings);
    v["graph"] = GraphToJson(m.graph);
    return Json::WriteFile(v, path, err);
}

bool GraphAssetSerializer::Load(GraphMaterial& m, const std::filesystem::path& path, std::string* err) {
    auto doc = ReadAsset(path, "Material", kGraphMaterialVersion, kGraphMaterialVersion, err);
    if (!doc) return false;
    GraphMaterial out;
    out.name = NameOf(*doc);
    SettingsFromJson(doc->Find("settings"), out.settings);
    if (!GraphFromJson(doc->Find("graph"), out.graph, err)) return false;
    m = std::move(out);
    return true;
}

bool GraphAssetSerializer::Save(const GraphMaterialInstance& inst, const std::filesystem::path& path,
                                std::string* err) {
    SerialValue v = Header("MaterialInstance", kGraphInstanceVersion, inst.name);
    v["parent"] = SerialValue(inst.parent);
    SerialValue overrides = SerialValue::MakeArray();
    for (const ParameterOverride& o : inst.overrides) {
        SerialValue ov = SerialValue::MakeObject();
        ov["name"] = SerialValue(o.name);
        ov["kind"] = SerialValue(EnumName(o.kind, kOverrideKindNames));
        switch (o.kind) {
            case ParameterOverride::Kind::Scalar: ov["value"] = SerialValue(static_cast<f64>(o.scalar)); break;
            case ParameterOverride::Kind::Vector: ov["value"] = Json::Vec4Value(o.vector); break;
            case ParameterOverride::Kind::Texture: ov["value"] = SerialValue(o.texture); break;
            case ParameterOverride::Kind::StaticSwitch: ov["value"] = SerialValue(o.staticSwitch); break;
        }
        overrides.PushBack(std::move(ov));
    }
    v["overrides"] = std::move(overrides);
    if (inst.twoSided) v["twoSided"] = SerialValue(*inst.twoSided);
    if (inst.opacityMaskClip) v["opacityMaskClip"] = SerialValue(static_cast<f64>(*inst.opacityMaskClip));
    return Json::WriteFile(v, path, err);
}

bool GraphAssetSerializer::Load(GraphMaterialInstance& inst, const std::filesystem::path& path,
                                std::string* err) {
    auto doc = ReadAsset(path, "MaterialInstance", kGraphInstanceVersion, kGraphInstanceVersion, err);
    if (!doc) return false;
    GraphMaterialInstance out;
    out.name = NameOf(*doc);
    out.parent = doc->Find("parent") ? doc->Find("parent")->AsString() : std::string{};
    if (auto* overrides = doc->Find("overrides"); overrides && overrides->IsArray()) {
        for (const SerialValue& ov : overrides->Elements()) {
            ParameterOverride o;
            o.name = ov.Find("name") ? ov.Find("name")->AsString() : std::string{};
            if (o.name.empty()) continue;
            o.kind = EnumParse(ov.Find("kind"), kOverrideKindNames, ParameterOverride::Kind::Scalar);
            const SerialValue* val = ov.Find("value");
            if (val) {
                switch (o.kind) {
                    case ParameterOverride::Kind::Scalar: o.scalar = static_cast<f32>(val->AsFloat(0.0)); break;
                    case ParameterOverride::Kind::Vector: Json::ReadVec4(*val, o.vector); break;
                    case ParameterOverride::Kind::Texture: o.texture = val->AsString(); break;
                    case ParameterOverride::Kind::StaticSwitch: o.staticSwitch = val->AsBool(false); break;
                }
            }
            out.overrides.push_back(std::move(o));
        }
    }
    if (auto* t = doc->Find("twoSided")) out.twoSided = t->AsBool(false);
    if (auto* c = doc->Find("opacityMaskClip")) out.opacityMaskClip = static_cast<f32>(c->AsFloat(0.3333));
    inst = std::move(out);
    return true;
}

bool GraphAssetSerializer::Save(const MaterialFunction& fn, const std::filesystem::path& path,
                                std::string* err) {
    SerialValue v = Header("MaterialFunction", kFunctionVersion, fn.name);
    v["description"] = SerialValue(fn.description);
    v["category"] = SerialValue(fn.category);
    v["exposeToLibrary"] = SerialValue(fn.exposeToLibrary);
    v["graph"] = GraphToJson(fn.graph);
    return Json::WriteFile(v, path, err);
}

bool GraphAssetSerializer::Load(MaterialFunction& fn, const std::filesystem::path& path,
                                std::string* err) {
    auto doc = ReadAsset(path, "MaterialFunction", kFunctionVersion, 1, err);
    if (!doc) return false;
    MaterialFunction out;
    out.name = NameOf(*doc);
    if (auto* d = doc->Find("description")) out.description = d->AsString();
    if (auto* c = doc->Find("category")) out.category = c->AsString(out.category);
    if (auto* e = doc->Find("exposeToLibrary")) out.exposeToLibrary = e->AsBool(true);
    if (!GraphFromJson(doc->Find("graph"), out.graph, err)) return false;
    fn = std::move(out);
    return true;
}

bool GraphAssetSerializer::Save(const ParameterCollection& pc, const std::filesystem::path& path,
                                std::string* err) {
    SerialValue v = Header("ParameterCollection", kCollectionVersion, pc.name);
    SerialValue scalars = SerialValue::MakeArray();
    for (const auto& s : pc.scalars) {
        SerialValue sv = SerialValue::MakeObject();
        sv["name"] = SerialValue(s.name);
        sv["value"] = SerialValue(static_cast<f64>(s.value));
        scalars.PushBack(std::move(sv));
    }
    v["scalars"] = std::move(scalars);
    SerialValue vectors = SerialValue::MakeArray();
    for (const auto& vec : pc.vectors) {
        SerialValue vv = SerialValue::MakeObject();
        vv["name"] = SerialValue(vec.name);
        vv["value"] = Json::Vec4Value(vec.value);
        vectors.PushBack(std::move(vv));
    }
    v["vectors"] = std::move(vectors);
    return Json::WriteFile(v, path, err);
}

bool GraphAssetSerializer::Load(ParameterCollection& pc, const std::filesystem::path& path,
                                std::string* err) {
    auto doc = ReadAsset(path, "ParameterCollection", kCollectionVersion, 1, err);
    if (!doc) return false;
    ParameterCollection out;
    out.name = NameOf(*doc);
    if (auto* scalars = doc->Find("scalars"); scalars && scalars->IsArray()) {
        for (const SerialValue& sv : scalars->Elements()) {
            out.scalars.push_back({sv.Find("name") ? sv.Find("name")->AsString() : std::string{},
                                   sv.Find("value") ? static_cast<f32>(sv.Find("value")->AsFloat(0.0)) : 0.0f});
        }
    }
    if (auto* vectors = doc->Find("vectors"); vectors && vectors->IsArray()) {
        for (const SerialValue& vv : vectors->Elements()) {
            ParameterCollection::Vector vec;
            vec.name = vv.Find("name") ? vv.Find("name")->AsString() : std::string{};
            if (auto* val = vv.Find("value")) Json::ReadVec4(*val, vec.value);
            out.vectors.push_back(std::move(vec));
        }
    }
    pc = std::move(out);
    return true;
}

i64 GraphAssetSerializer::ProbeVersion(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return 0;
    auto doc = Json::ReadFile(path, nullptr);
    if (!doc || !doc->Find("version")) return 0;
    return doc->Find("version")->AsInt(0);
}

}  // namespace Luma::Material
