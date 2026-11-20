#pragma once

#include <filesystem>
#include <string>

#include "Luma/Material/Material.h"
#include "Luma/Serialization/SerialValue.h"

// Shared JSON encoding of material inputs/settings, used by the Material and
// Material Instance serializers so both formats stay identical per input.

namespace Luma::Material::Json {

inline constexpr i64 kFormatVersion = 3;

SerialValue Vec2Value(const Math::Vec2& v);
SerialValue Vec4Value(const Math::Vec4& v);
// Reads 1..4 numbers (or a bare number) into `out`; missing components keep
// their current values. Returns false if `v` is neither.
bool ReadVec4(const SerialValue& v, Math::Vec4& out);
bool ReadVec2(const SerialValue& v, Math::Vec2& out);

const char* BlendModeName(BlendMode m);
const char* ShadingModelName(ShadingModel m);
const char* ChannelName(TextureChannel c);
bool ParseBlendMode(const std::string& s, BlendMode& out);
bool ParseShadingModel(const std::string& s, ShadingModel& out);
bool ParseChannel(const std::string& s, TextureChannel& out);

SerialValue InputToJson(const MaterialInput& in);
// Fields present in `v` overwrite `out`; absent fields keep their values.
void InputFromJson(const SerialValue& v, MaterialInput& out);

bool WriteFile(const SerialValue& value, const std::filesystem::path& path,
               std::string* outError);
std::optional<SerialValue> ReadFile(const std::filesystem::path& path,
                                    std::string* outError);

}  // namespace Luma::Material::Json
