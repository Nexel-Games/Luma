#include "MaterialJson.h"

#include <fstream>
#include <sstream>

#include "Luma/Serialization/Json.h"

namespace Luma::Material::Json {

SerialValue Vec2Value(const Math::Vec2& v) {
    SerialValue arr = SerialValue::MakeArray();
    arr.PushBack(SerialValue(v.x));
    arr.PushBack(SerialValue(v.y));
    return arr;
}

SerialValue Vec4Value(const Math::Vec4& v) {
    SerialValue arr = SerialValue::MakeArray();
    arr.PushBack(SerialValue(v.x));
    arr.PushBack(SerialValue(v.y));
    arr.PushBack(SerialValue(v.z));
    arr.PushBack(SerialValue(v.w));
    return arr;
}

bool ReadVec4(const SerialValue& v, Math::Vec4& out) {
    if (v.IsNumber()) {
        out.x = static_cast<f32>(v.AsFloat(0.0));
        return true;
    }
    if (!v.IsArray()) return false;
    f32* dst[4] = {&out.x, &out.y, &out.z, &out.w};
    for (usize i = 0; i < 4 && i < v.Size(); ++i) {
        *dst[i] = static_cast<f32>(v.At(i).AsFloat(*dst[i]));
    }
    return true;
}

bool ReadVec2(const SerialValue& v, Math::Vec2& out) {
    if (!v.IsArray() || v.Size() < 2) return false;
    out.x = static_cast<f32>(v.At(0).AsFloat(out.x));
    out.y = static_cast<f32>(v.At(1).AsFloat(out.y));
    return true;
}

const char* BlendModeName(BlendMode m) {
    return m == BlendMode::Masked ? "Masked" : "Opaque";
}
const char* ShadingModelName(ShadingModel m) {
    return m == ShadingModel::Unlit ? "Unlit" : "DefaultLit";
}
const char* ChannelName(TextureChannel c) {
    switch (c) {
        case TextureChannel::R: return "R";
        case TextureChannel::G: return "G";
        case TextureChannel::B: return "B";
        case TextureChannel::A: return "A";
        case TextureChannel::RGB: break;
    }
    return "RGB";
}

bool ParseBlendMode(const std::string& s, BlendMode& out) {
    if (s == "Opaque") { out = BlendMode::Opaque; return true; }
    if (s == "Masked") { out = BlendMode::Masked; return true; }
    return false;
}
bool ParseShadingModel(const std::string& s, ShadingModel& out) {
    if (s == "DefaultLit") { out = ShadingModel::DefaultLit; return true; }
    if (s == "Unlit") { out = ShadingModel::Unlit; return true; }
    return false;
}
bool ParseChannel(const std::string& s, TextureChannel& out) {
    if (s == "RGB") { out = TextureChannel::RGB; return true; }
    if (s == "R") { out = TextureChannel::R; return true; }
    if (s == "G") { out = TextureChannel::G; return true; }
    if (s == "B") { out = TextureChannel::B; return true; }
    if (s == "A") { out = TextureChannel::A; return true; }
    return false;
}

SerialValue InputToJson(const MaterialInput& in) {
    SerialValue o = SerialValue::MakeObject();
    o["constant"] = Vec4Value(in.constant);
    if (!in.texture.empty()) o["texture"] = SerialValue(in.texture);
    o["channel"] = SerialValue(std::string(ChannelName(in.channel)));
    o["tint"] = Vec4Value(in.tint);
    o["uvTiling"] = Vec2Value(in.uvTiling);
    o["uvOffset"] = Vec2Value(in.uvOffset);
    o["normalStrength"] = SerialValue(in.normalStrength);
    o["flipGreen"] = SerialValue(in.flipGreen);
    o["invert"] = SerialValue(in.invert);
    o["emissiveIntensity"] = SerialValue(in.emissiveIntensity);
    return o;
}

void InputFromJson(const SerialValue& v, MaterialInput& out) {
    if (!v.IsObject()) return;
    if (const SerialValue* c = v.Find("constant")) ReadVec4(*c, out.constant);
    if (const SerialValue* t = v.Find("texture"); t && t->IsString()) out.texture = t->AsString();
    if (const SerialValue* ch = v.Find("channel"); ch && ch->IsString()) {
        ParseChannel(ch->AsString(), out.channel);
    }
    if (const SerialValue* t = v.Find("tint")) ReadVec4(*t, out.tint);
    if (const SerialValue* t = v.Find("uvTiling")) ReadVec2(*t, out.uvTiling);
    if (const SerialValue* t = v.Find("uvOffset")) ReadVec2(*t, out.uvOffset);
    if (const SerialValue* t = v.Find("normalStrength"); t && t->IsNumber()) {
        out.normalStrength = static_cast<f32>(t->AsFloat(out.normalStrength));
    }
    if (const SerialValue* t = v.Find("flipGreen")) out.flipGreen = t->AsBool(out.flipGreen);
    if (const SerialValue* t = v.Find("invert")) out.invert = t->AsBool(out.invert);
    if (const SerialValue* t = v.Find("emissiveIntensity"); t && t->IsNumber()) {
        out.emissiveIntensity = static_cast<f32>(t->AsFloat(out.emissiveIntensity));
    }
}

bool WriteFile(const SerialValue& value, const std::filesystem::path& path,
               std::string* outError) {
    const std::string json = WriteJson(value, true);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "cannot open file for writing: " + path.string();
        return false;
    }
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.close();
    if (!file) {
        if (outError) *outError = "failed writing file: " + path.string();
        return false;
    }
    return true;
}

std::optional<SerialValue> ReadFile(const std::filesystem::path& path,
                                    std::string* outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "cannot open file for reading: " + path.string();
        return std::nullopt;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string parseError;
    auto parsed = ParseJson(ss.str(), &parseError);
    if (!parsed && outError) {
        *outError = parseError.empty() ? "malformed JSON: " + path.string() : parseError;
    }
    return parsed;
}

}  // namespace Luma::Material::Json
