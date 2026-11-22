#include "Luma/Editor/InspectorDrawers.h"

#include <algorithm>
#include <cmath>

#include "Luma/Core/Log.h"
#include "Luma/Serialization/Json.h"

namespace Luma::Editor {

using Slate::Rect;

// --- InspectorColorPickers --------------------------------------------------------

Slate::ColorPickerPopup& InspectorColorPickers::Use(u64 rowId) {
    auto& slot = m_pickers[rowId];
    if (!slot) slot = std::make_unique<Slate::ColorPickerPopup>();
    if (std::find(m_used.begin(), m_used.end(), rowId) == m_used.end()) m_used.push_back(rowId);
    return *slot;
}

void InspectorColorPickers::BeginFrame() { m_used.clear(); }

void InspectorColorPickers::DrawPanels(Slate::Context& ui) {
    for (auto& [id, picker] : m_pickers) {
        const bool used = std::find(m_used.begin(), m_used.end(), id) != m_used.end();
        if (!used) {
            picker->Close();  // its row is gone (component removed / selection changed)
            continue;
        }
        picker->DrawPanel(ui);
    }
}

void InspectorColorPickers::CloseAll() {
    for (auto& [id, picker] : m_pickers) picker->Close();
}

// --- InspectorRows ----------------------------------------------------------------

InspectorRows::InspectorRows(Slate::Context& ui_, Panels::PanelContext& panel_, Scene& scene_,
                             Entity entity_, f32 x_, f32 y_, f32 w_,
                             InspectorColorPickers& pickers)
    : ui(ui_), panel(panel_), scene(scene_), entity(entity_), x(x_), y(y_), w(w_),
      m_pickers(pickers) {}

u64 InspectorRows::RowId(std::string_view key, Entity entity) {
    const u64 e = static_cast<u64>(entt::to_integral(entity)) + 1;
    return Slate::Context::ID(key) ^ (e * 0x9E3779B97F4A7C15ull);
}

void InspectorRows::SubHeading(std::string_view text) {
    y += 4.0f;
    ui.LabelIn({x, y, w, 20}, text, ui.theme().accent);
    y += 24.0f;
}

bool InspectorRows::Float(std::string_view label, std::string_view key, f32& value,
                          const Slate::SpinBoxOptions& options) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    const bool changed =
        ui.SpinBox(Id(key), {x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, value, options)
            .changed;
    y += kRowStep;
    return changed;
}

bool InspectorRows::Int(std::string_view label, std::string_view key, i32& value,
                        const Slate::SpinBoxOptions& options) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    const bool changed =
        ui.SpinBoxInt(Id(key), {x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, value, options)
            .changed;
    y += kRowStep;
    return changed;
}

bool InspectorRows::Vec3(std::string_view label, std::string_view key, Math::Vec3& value) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    const bool changed =
        ui.Vector3Field(Id(key), {x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, &value.x);
    y += kRowStep;
    return changed;
}

bool InspectorRows::Check(std::string_view label, std::string_view key, bool& value,
                          f32 indent) {
    const bool changed = ui.Checkbox(Id(key), {x + indent, y, 18, 18}, label, value);
    y += 26.0f;
    return changed;
}

bool InspectorRows::Combo(std::string_view label, std::string_view key,
                          const std::vector<std::string>& items, int& current) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    const int picked =
        ui.Combo(Id(key), {x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, items, current);
    y += kRowStep;
    if (picked == current) return false;
    current = picked;
    return true;
}

bool InspectorRows::Text(std::string_view label, std::string_view key, std::string& value) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    const std::string before = value;
    ui.TextField(Id(key), {x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, value);
    y += kRowStep;
    return value != before;
}

void InspectorRows::Color(std::string_view label, std::string_view key,
                          const Math::Vec3& current,
                          std::function<void(const Math::Vec3&)> set) {
    auto toColor = [](const Math::Vec3& v) {
        return Slate::Color::RGB(
            static_cast<u8>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<u8>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<u8>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f));
    };
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    Slate::ColorPickerPopup& popup = m_pickers.Use(Id(key));
    popup.OnColorChanged = [set = std::move(set)](const Slate::Color& c) {
        set(Math::Vec3(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f));
    };
    popup.SetColor(toColor(current));  // follows the component (no-op while editing)
    popup.DrawSwatch(ui, {x + kLabelWidth, y + 2, w - kLabelWidth, 18});
    y += kRowStep;
}

void InspectorRows::ReadOnly(std::string_view label, std::string_view value) {
    ui.LabelIn({x, y, kLabelWidth, kRowHeight}, label, ui.theme().textDim);
    ui.LabelIn({x + kLabelWidth, y, w - kLabelWidth, kRowHeight}, value,
               ui.theme().textDisabled);
    y += kRowStep;
}

void InspectorRows::Note(std::string_view text, Slate::Color color) {
    ui.LabelIn({x, y, w, kRowHeight}, text, color);
    y += kRowStep;
}

// --- InspectorDrawerRegistry ------------------------------------------------------

InspectorDrawerRegistry::InspectorDrawerRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken InspectorDrawerRegistry::Register(InspectorDrawer drawer, std::string* error) {
    auto fail = [error](std::string message) {
        LUMA_LOG_ERROR("Inspector", "{}", message);
        if (error) *error = std::move(message);
        return RegistrationToken{};
    };
    if (drawer.componentId.empty()) return fail("inspector drawer rejected: component id is empty");
    if (drawer.owner.empty()) {
        return fail("inspector drawer for '" + drawer.componentId + "' rejected: owner is empty");
    }
    if (!drawer.hidden && !drawer.draw) {
        return fail("inspector drawer for '" + drawer.componentId +
                    "' rejected: missing draw callback");
    }
    if (const InspectorDrawer* existing = Find(drawer.componentId)) {
        return fail("an inspector drawer for '" + drawer.componentId +
                    "' is already registered (by '" + existing->owner + "')");
    }
    const u64 handle = m_nextHandle++;
    m_entries.push_back({handle, std::make_unique<InspectorDrawer>(std::move(drawer))});
    ++m_revision;
    return RegistrationToken(m_alive, [this, handle] {
        auto it = std::find_if(m_entries.begin(), m_entries.end(),
                               [handle](const Entry& e) { return e.handle == handle; });
        if (it == m_entries.end()) return;
        m_entries.erase(it);
        ++m_revision;
    });
}

const InspectorDrawer* InspectorDrawerRegistry::Find(std::string_view componentId) const {
    for (const Entry& e : m_entries) {
        if (e.drawer->componentId == componentId) return e.drawer.get();
    }
    return nullptr;
}

std::vector<const InspectorDrawer*> InspectorDrawerRegistry::All() const {
    std::vector<const InspectorDrawer*> out;
    out.reserve(m_entries.size());
    for (const Entry& e : m_entries) out.push_back(e.drawer.get());
    return out;
}

InspectorDrawerRegistry& InspectorDrawers() {
    static InspectorDrawerRegistry registry;
    return registry;
}

// --- Default (reflection) drawer --------------------------------------------------

namespace {

bool IsNumberTriple(const SerialValue& v) {
    if (!v.IsArray() || v.Size() != 3) return false;
    for (const SerialValue& e : v.Elements()) {
        if (!e.IsNumber()) return false;
    }
    return true;
}

bool IsStringList(const SerialValue& v) {
    if (!v.IsArray()) return false;
    for (const SerialValue& e : v.Elements()) {
        if (!e.IsString()) return false;
    }
    return true;
}

Slate::SpinBoxOptions RangeOptions(const PropertyMeta& meta, bool integer) {
    Slate::SpinBoxOptions o;
    o.speed = integer ? 0.1 : 0.01;
    if (meta.hasRange) {
        o.min = meta.rangeMin;
        o.max = meta.rangeMax;
        o.sliderMin = meta.rangeMin;
        o.sliderMax = meta.rangeMax;
    }
    if (integer) o.delta = 1.0;
    return o;
}

Math::Vec3 ReadVec3(const SerialValue& v) {
    return Math::Vec3(static_cast<f32>(v.At(0).AsFloat()), static_cast<f32>(v.At(1).AsFloat()),
                      static_cast<f32>(v.At(2).AsFloat()));
}

SerialValue WriteVec3(const Math::Vec3& v) {
    SerialValue a = SerialValue::MakeArray();
    a.PushBack(static_cast<f64>(v.x));
    a.PushBack(static_cast<f64>(v.y));
    a.PushBack(static_cast<f64>(v.z));
    return a;
}

}  // namespace

std::vector<ReflectedRow> BuildReflectedRows(const ComponentDesc& desc,
                                             const SerialValue& payload) {
    std::vector<ReflectedRow> rows;
    if (!desc.reflection) return rows;
    const TypeInfoBase& info = *desc.reflection;
    for (usize i = 0; i < info.PropertyCount(); ++i) {
        const PropertyMeta& meta = info.PropertyMetaAt(i);
        ReflectedRow row;
        row.property = i;
        row.name = info.PropertyName(i);
        row.label = meta.displayName.empty() ? PrettyPropertyName(row.name) : meta.displayName;
        row.category = meta.category.empty() ? std::string{} : PrettyPropertyName(meta.category);
        row.tooltip = meta.tooltip;
        row.readOnly = meta.readOnly;
        const SerialValue* value = payload.Find(row.name);
        if (!value) {
            row.kind = ReflectedRow::Kind::Other;
            row.readOnly = true;
        } else if (value->IsBool()) {
            row.kind = ReflectedRow::Kind::Bool;
        } else if (value->IsInt() && !meta.enumNames.empty()) {
            row.kind = ReflectedRow::Kind::Enum;
        } else if (value->IsInt()) {
            row.kind = ReflectedRow::Kind::Int;
        } else if (value->IsFloat()) {
            row.kind = ReflectedRow::Kind::Float;
        } else if (value->IsString()) {
            row.kind = ReflectedRow::Kind::String;
        } else if (IsNumberTriple(*value)) {
            row.kind = meta.color ? ReflectedRow::Kind::Color : ReflectedRow::Kind::Vec3;
        } else if (IsStringList(*value)) {
            row.kind = ReflectedRow::Kind::StringList;
            row.readOnly = true;
        } else {
            row.kind = ReflectedRow::Kind::Other;
            row.readOnly = true;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void DrawReflectedComponent(InspectorRows& rows, const ComponentDesc& desc) {
    if (!desc.reflection) {
        rows.Note("No editable properties.", rows.ui.theme().textDim);
        return;
    }
    SerialValue payload;
    desc.write(rows.scene, rows.entity, payload);
    const std::vector<ReflectedRow> layout = BuildReflectedRows(desc, payload);
    if (layout.empty()) {
        rows.Note("No editable properties.", rows.ui.theme().textDim);
        return;
    }

    bool dirty = false;
    std::string category;
    for (const ReflectedRow& row : layout) {
        if (!row.category.empty() && row.category != category && row.category != desc.displayName) {
            rows.SubHeading(row.category);
        }
        category = row.category;
        const PropertyMeta& meta = desc.reflection->PropertyMetaAt(row.property);
        const std::string key = "refl." + desc.id + "." + row.name;
        SerialValue& value = payload[row.name];
        const f32 rowTop = rows.y;

        if (row.readOnly) {
            std::string text;
            if (row.kind == ReflectedRow::Kind::StringList) {
                for (const SerialValue& s : value.Elements()) {
                    if (!text.empty()) text += ", ";
                    text += s.AsString().empty() ? std::string("(empty)") : s.AsString();
                }
            } else if (value.IsString()) {
                text = value.AsString();
            } else {
                text = WriteJson(value, false);
            }
            rows.ReadOnly(row.label, text);
        } else {
            switch (row.kind) {
                case ReflectedRow::Kind::Bool: {
                    bool v = value.AsBool();
                    if (rows.Check(row.label, key, v)) {
                        value = SerialValue(v);
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::Int: {
                    i32 v = static_cast<i32>(value.AsInt());
                    if (rows.Int(row.label, key, v, RangeOptions(meta, true))) {
                        value = SerialValue(static_cast<i64>(v));
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::Float: {
                    f32 v = static_cast<f32>(value.AsFloat());
                    if (rows.Float(row.label, key, v, RangeOptions(meta, false))) {
                        value = SerialValue(static_cast<f64>(v));
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::Enum: {
                    int current = static_cast<int>(value.AsInt());
                    if (current < 0 || current >= static_cast<int>(meta.enumNames.size())) {
                        current = 0;
                    }
                    if (rows.Combo(row.label, key, meta.enumNames, current)) {
                        value = SerialValue(static_cast<i64>(current));
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::String: {
                    std::string v = value.AsString();
                    if (rows.Text(row.label, key, v)) {
                        value = SerialValue(v);
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::Vec3: {
                    Math::Vec3 v = ReadVec3(value);
                    if (rows.Vec3(row.label, key, v)) {
                        value = WriteVec3(v);
                        dirty = true;
                    }
                    break;
                }
                case ReflectedRow::Kind::Color: {
                    // The picker reports changes later in the frame; look the
                    // component type up again then (it may have been removed).
                    Scene* scene = &rows.scene;
                    const Entity entity = rows.entity;
                    const std::string id = desc.id;
                    const std::string name = row.name;
                    rows.Color(row.label, key, ReadVec3(value),
                               [scene, entity, id, name](const Math::Vec3& c) {
                                   const ComponentDesc* d = Components().Find(id);
                                   if (!d || !scene->IsValid(entity) || !d->has(*scene, entity)) {
                                       return;
                                   }
                                   SerialValue p;
                                   d->write(*scene, entity, p);
                                   p[name] = WriteVec3(c);
                                   d->read(*scene, entity, p);
                               });
                    break;
                }
                case ReflectedRow::Kind::StringList:
                case ReflectedRow::Kind::Other:
                    break;
            }
        }
        if (!row.tooltip.empty()) {
            rows.ui.Tooltip(rows.Id(key + ".tip"),
                            {rows.x, rowTop, InspectorRows::kLabelWidth, InspectorRows::kRowHeight},
                            row.label + "\n" + row.tooltip);
        }
    }
    if (dirty) desc.read(rows.scene, rows.entity, payload);
}

std::string MissingComponentLabel(const UnresolvedComponent& component) {
    const std::string owner = component.owner.empty() ? std::string("unknown") : component.owner;
    return component.id + " (plugin " + owner + " not loaded) \xE2\x80\x94 data kept";
}

}  // namespace Luma::Editor
