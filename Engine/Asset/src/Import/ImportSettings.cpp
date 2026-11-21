#include "Luma/Asset/Import/ImportSettings.h"

#include <algorithm>
#include <cmath>

namespace Luma::Import {
namespace {

bool Bounded(const SettingField& field) {
    return field.min != 0.0 || field.max != 0.0;
}

f64 ClampNumber(const SettingField& field, f64 value) {
    if (!Bounded(field)) {
        return value;
    }
    return std::clamp(value, field.min, field.max);
}

bool EnumContains(const SettingField& field, std::string_view label) {
    for (const std::string& value : field.enumValues) {
        if (value == label) {
            return true;
        }
    }
    return false;
}

Math::Vec3 ReadVec3(const SerialValue& value) {
    return {static_cast<f32>(value.At(0).AsFloat()),
            static_cast<f32>(value.At(1).AsFloat()),
            static_cast<f32>(value.At(2).AsFloat())};
}

}  // namespace

void SettingsBlock::Define(SettingField field, SerialValue defaultValue) {
    Entry entry;
    entry.field = std::move(field);
    entry.defaultValue = Clamp(entry.field, std::move(defaultValue));
    entry.value = entry.defaultValue;
    m_fields.push_back(entry.field);
    m_entries.push_back(std::move(entry));
}

const std::vector<SettingField>& SettingsBlock::Fields() const { return m_fields; }

SettingsBlock::Entry* SettingsBlock::Find(std::string_view key) {
    for (Entry& entry : m_entries) {
        if (entry.field.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

const SettingsBlock::Entry* SettingsBlock::Find(std::string_view key) const {
    for (const Entry& entry : m_entries) {
        if (entry.field.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

bool SettingsBlock::Accepts(const SettingField& field, const SerialValue& value) {
    switch (field.type) {
        case FieldType::Bool:
            return value.IsBool();
        case FieldType::Float:
        case FieldType::Int:
            return value.IsNumber();
        case FieldType::String:
            return value.IsString();
        case FieldType::Enum:
            return value.IsString() && EnumContains(field, value.AsString());
        case FieldType::Vec3:
            if (!value.IsArray() || value.Size() != 3) {
                return false;
            }
            return value.At(0).IsNumber() && value.At(1).IsNumber() && value.At(2).IsNumber();
    }
    return false;
}

SerialValue SettingsBlock::Clamp(const SettingField& field, SerialValue value) {
    if (field.type == FieldType::Float && value.IsNumber()) {
        return ClampNumber(field, value.AsFloat());
    }
    if (field.type == FieldType::Int && value.IsNumber()) {
        const f64 clamped = ClampNumber(field, static_cast<f64>(value.AsInt()));
        return static_cast<i64>(std::llround(clamped));
    }
    return value;
}

bool SettingsBlock::GetBool(std::string_view key) const {
    const Entry* entry = Find(key);
    return entry != nullptr ? entry->value.AsBool() : false;
}

f64 SettingsBlock::GetFloat(std::string_view key) const {
    const Entry* entry = Find(key);
    return entry != nullptr ? entry->value.AsFloat() : 0.0;
}

i64 SettingsBlock::GetInt(std::string_view key) const {
    const Entry* entry = Find(key);
    return entry != nullptr ? entry->value.AsInt() : 0;
}

std::string SettingsBlock::GetString(std::string_view key) const {
    const Entry* entry = Find(key);
    return entry != nullptr ? entry->value.AsString() : std::string{};
}

Math::Vec3 SettingsBlock::GetVec3(std::string_view key) const {
    const Entry* entry = Find(key);
    if (entry == nullptr || !entry->value.IsArray() || entry->value.Size() < 3) {
        return {};
    }
    return ReadVec3(entry->value);
}

void SettingsBlock::Set(std::string_view key, SerialValue value) {
    Entry* entry = Find(key);
    if (entry == nullptr || !Accepts(entry->field, value)) {
        return;
    }
    entry->value = Clamp(entry->field, std::move(value));
}

SerialValue SettingsBlock::ToJson() const {
    SerialValue object = SerialValue::MakeObject();
    for (const Entry& entry : m_entries) {
        object[entry.field.key] = entry.value;
    }
    return object;
}

void SettingsBlock::FromJson(const SerialValue& json) {
    if (!json.IsObject()) {
        return;
    }
    for (Entry& entry : m_entries) {
        const SerialValue* value = json.Find(entry.field.key);
        if (value != nullptr) {
            Set(entry.field.key, *value);
        }
    }
}

void SettingsBlock::ResetToDefaults() {
    for (Entry& entry : m_entries) {
        entry.value = entry.defaultValue;
    }
}

bool SettingsBlock::IsVisible(const SettingField& field) const {
    if (field.visibleIfKey.empty()) {
        return true;
    }
    std::string_view key = field.visibleIfKey;
    bool invert = false;
    if (key.front() == '!') {
        invert = true;
        key.remove_prefix(1);
    }
    const bool value = GetBool(key);
    return invert ? !value : value;
}

}  // namespace Luma::Import
