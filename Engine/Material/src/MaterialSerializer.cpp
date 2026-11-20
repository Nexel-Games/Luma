#include "Luma/Material/MaterialSerializer.h"

#include "MaterialJson.h"

namespace Luma::Material {

namespace {

// Legacy (pre-v3) files stored flat constants under "constants"; texture
// "maps" were raw AssetIds (path hashes) and are not carried forward.
void ReadLegacyConstants(const SerialValue& constants, Material& out) {
    struct Key {
        const char* key;
        MaterialParameter param;
    };
    static const Key kKeys[] = {
        {"baseColor", MaterialParameter::BaseColor},
        {"metallic", MaterialParameter::Metallic},
        {"specular", MaterialParameter::Specular},
        {"roughness", MaterialParameter::Roughness},
        {"normal", MaterialParameter::Normal},
        {"emissive", MaterialParameter::EmissiveColor},
        {"ambientOcclusion", MaterialParameter::AmbientOcclusion},
        {"opacityMask", MaterialParameter::OpacityMask},
    };
    for (const Key& k : kKeys) {
        if (const SerialValue* v = constants.Find(k.key)) {
            Json::ReadVec4(*v, out.Input(k.param).constant);
        }
    }
}

}  // namespace

SerialValue MaterialSerializer::ToJson(const Material& material) {
    SerialValue root = SerialValue::MakeObject();
    root["version"] = SerialValue(Json::kFormatVersion);
    root["type"] = SerialValue(std::string("Material"));
    root["name"] = SerialValue(material.name);
    SerialValue settings = SerialValue::MakeObject();
    settings["blendMode"] = SerialValue(std::string(Json::BlendModeName(material.settings.blendMode)));
    settings["shadingModel"] =
        SerialValue(std::string(Json::ShadingModelName(material.settings.shadingModel)));
    settings["twoSided"] = SerialValue(material.settings.twoSided);
    settings["opacityMaskClip"] = SerialValue(material.settings.opacityMaskClip);
    root["settings"] = settings;
    SerialValue inputs = SerialValue::MakeObject();
    for (usize i = 0; i < kMaterialParameterCount; ++i) {
        inputs[ParameterKey(static_cast<MaterialParameter>(i))] =
            Json::InputToJson(material.inputs[i]);
    }
    root["inputs"] = inputs;
    return root;
}

bool MaterialSerializer::FromJson(Material& out, const SerialValue& root,
                                  std::string* outError) {
    if (!root.IsObject()) {
        if (outError) *outError = "material file is not a JSON object";
        return false;
    }
    out = Material{};
    if (const SerialValue* n = root.Find("name"); n && n->IsString()) out.name = n->AsString();

    // Legacy layout (versions 1-2): flat constants + numeric blend mode.
    if (const SerialValue* constants = root.Find("constants"); constants && constants->IsObject()) {
        ReadLegacyConstants(*constants, out);
        if (const SerialValue* b = root.Find("blendMode"); b && b->IsNumber()) {
            out.settings.blendMode = b->AsInt(0) == 1 ? BlendMode::Masked : BlendMode::Opaque;
        }
        return true;
    }

    if (const SerialValue* s = root.Find("settings"); s && s->IsObject()) {
        if (const SerialValue* v = s->Find("blendMode"); v && v->IsString()) {
            Json::ParseBlendMode(v->AsString(), out.settings.blendMode);
        }
        if (const SerialValue* v = s->Find("shadingModel"); v && v->IsString()) {
            Json::ParseShadingModel(v->AsString(), out.settings.shadingModel);
        }
        if (const SerialValue* v = s->Find("twoSided")) {
            out.settings.twoSided = v->AsBool(out.settings.twoSided);
        }
        if (const SerialValue* v = s->Find("opacityMaskClip"); v && v->IsNumber()) {
            out.settings.opacityMaskClip = static_cast<f32>(v->AsFloat(0.3333));
        }
    }
    if (const SerialValue* inputs = root.Find("inputs"); inputs && inputs->IsObject()) {
        for (usize i = 0; i < kMaterialParameterCount; ++i) {
            if (const SerialValue* v = inputs->Find(ParameterKey(static_cast<MaterialParameter>(i)))) {
                Json::InputFromJson(*v, out.inputs[i]);
            }
        }
    }
    return true;
}

bool MaterialSerializer::SaveToFile(const Material& material,
                                    const std::filesystem::path& path,
                                    std::string* outError) {
    return Json::WriteFile(ToJson(material), path, outError);
}

bool MaterialSerializer::LoadFromFile(Material& out, const std::filesystem::path& path,
                                      std::string* outError) {
    auto parsed = Json::ReadFile(path, outError);
    if (!parsed) return false;
    return FromJson(out, *parsed, outError);
}

}  // namespace Luma::Material
