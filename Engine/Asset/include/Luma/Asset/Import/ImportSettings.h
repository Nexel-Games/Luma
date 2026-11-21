#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/Serialization/SerialValue.h"

namespace Luma::Import {

enum class FieldType : u8 { Bool, Float, Int, Enum, Vec3, String };

struct SettingField {
    std::string key;
    std::string label;
    std::string tooltip;
    std::string category;
    FieldType type{};
    f64 min = 0;
    f64 max = 0;
    f64 step = 0;
    std::vector<std::string> enumValues;
    std::string visibleIfKey;
};

class SettingsBlock {
public:
    void Define(SettingField field, SerialValue defaultValue);
    const std::vector<SettingField>& Fields() const;
    bool GetBool(std::string_view key) const;
    f64 GetFloat(std::string_view key) const;
    i64 GetInt(std::string_view key) const;
    std::string GetString(std::string_view key) const;
    Math::Vec3 GetVec3(std::string_view key) const;
    void Set(std::string_view key, SerialValue value);
    SerialValue ToJson() const;
    void FromJson(const SerialValue& json);
    void ResetToDefaults();

private:
    struct Entry {
        SettingField field;
        SerialValue value;
        SerialValue defaultValue;
    };

    std::vector<Entry> m_entries;
    std::vector<SettingField> m_fields;

    Entry* Find(std::string_view key);
    const Entry* Find(std::string_view key) const;
    static bool Accepts(const SettingField& field, const SerialValue& value);
    static SerialValue Clamp(const SettingField& field, SerialValue value);
};

}  // namespace Luma::Import
