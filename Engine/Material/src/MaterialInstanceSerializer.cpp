#include "Luma/Material/MaterialInstanceSerializer.h"

#include "MaterialJson.h"

namespace Luma::Material {

SerialValue MaterialInstanceSerializer::ToJson(const MaterialInstance& mi) {
    SerialValue root = SerialValue::MakeObject();
    root["version"] = SerialValue(Json::kFormatVersion);
    root["type"] = SerialValue(std::string("MaterialInstance"));
    root["name"] = SerialValue(mi.name);
    root["parent"] = SerialValue(mi.parent);
    SerialValue settings = SerialValue::MakeObject();
    if (mi.blendMode) settings["blendMode"] = SerialValue(std::string(Json::BlendModeName(*mi.blendMode)));
    if (mi.twoSided) settings["twoSided"] = SerialValue(*mi.twoSided);
    if (mi.opacityMaskClip) settings["opacityMaskClip"] = SerialValue(*mi.opacityMaskClip);
    root["overrideSettings"] = settings;
    SerialValue overrides = SerialValue::MakeObject();
    for (usize i = 0; i < kMaterialParameterCount; ++i) {
        if (!mi.overridden[i]) continue;
        overrides[ParameterKey(static_cast<MaterialParameter>(i))] = Json::InputToJson(mi.inputs[i]);
    }
    root["overrides"] = overrides;
    return root;
}

bool MaterialInstanceSerializer::FromJson(MaterialInstance& out, const SerialValue& root,
                                          std::string* outError) {
    if (!root.IsObject()) {
        if (outError) *outError = "material instance file is not a JSON object";
        return false;
    }
    out = MaterialInstance{};
    if (const SerialValue* n = root.Find("name"); n && n->IsString()) out.name = n->AsString();
    if (const SerialValue* p = root.Find("parent"); p && p->IsString()) out.parent = p->AsString();
    if (const SerialValue* s = root.Find("overrideSettings"); s && s->IsObject()) {
        if (const SerialValue* v = s->Find("blendMode"); v && v->IsString()) {
            BlendMode m{};
            if (Json::ParseBlendMode(v->AsString(), m)) out.blendMode = m;
        }
        if (const SerialValue* v = s->Find("twoSided")) out.twoSided = v->AsBool(false);
        if (const SerialValue* v = s->Find("opacityMaskClip"); v && v->IsNumber()) {
            out.opacityMaskClip = static_cast<f32>(v->AsFloat(0.3333));
        }
    }
    if (const SerialValue* o = root.Find("overrides"); o && o->IsObject()) {
        for (usize i = 0; i < kMaterialParameterCount; ++i) {
            const SerialValue* v = o->Find(ParameterKey(static_cast<MaterialParameter>(i)));
            if (!v) continue;
            out.overridden[i] = true;
            Json::InputFromJson(*v, out.inputs[i]);
        }
    }
    return true;
}

bool MaterialInstanceSerializer::SaveToFile(const MaterialInstance& instance,
                                            const std::filesystem::path& path,
                                            std::string* outError) {
    return Json::WriteFile(ToJson(instance), path, outError);
}

bool MaterialInstanceSerializer::LoadFromFile(MaterialInstance& out,
                                              const std::filesystem::path& path,
                                              std::string* outError) {
    auto parsed = Json::ReadFile(path, outError);
    if (!parsed) return false;
    return FromJson(out, *parsed, outError);
}

}  // namespace Luma::Material
