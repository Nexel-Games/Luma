#include "Luma/MaterialCompiler/Translator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "ExpressionCompilers.h"

namespace Luma::MaterialCompiler {

using MaterialGraph::ExpressionDesc;
using MaterialGraph::ExpressionRegistry;
using MaterialGraph::Node;
using MaterialGraph::NodeId;
using MaterialGraph::PinRef;
using MaterialGraph::ValueType;

namespace {

const char* ShadingModelName(Material::ShadingModel m) {
    switch (m) {
        case Material::ShadingModel::Unlit: return "the Unlit shading model";
        case Material::ShadingModel::Subsurface: return "the Subsurface shading model";
        case Material::ShadingModel::ClearCoat: return "the Clear Coat shading model";
        case Material::ShadingModel::TwoSidedFoliage: return "the Two Sided Foliage shading model";
        default: return "the Default Lit shading model";
    }
}

const char* BlendModeName(Material::BlendMode b) {
    switch (b) {
        case Material::BlendMode::Masked: return "the Masked blend mode";
        case Material::BlendMode::Translucent: return "the Translucent blend mode";
        case Material::BlendMode::Additive: return "the Additive blend mode";
        case Material::BlendMode::Modulate: return "the Modulate blend mode";
        default: return "the Opaque blend mode";
    }
}

bool IsTranslucent(Material::BlendMode b) {
    return b == Material::BlendMode::Translucent || b == Material::BlendMode::Additive ||
           b == Material::BlendMode::Modulate;
}

// Unreal's material-attribute visibility: whether `pin` of Material Output
// feeds anything for these settings. When it doesn't, `why` names the
// setting that excludes it.
bool PinUsed(const Material::MaterialSettings& s, std::string_view pin, std::string& why) {
    using Material::ShadingModel;
    if (s.domain == Material::MaterialDomain::PostProcess) {
        why = "the Post Process domain";
        return pin == "EmissiveColor";
    }
    if (pin == "EmissiveColor" || pin == "WorldPositionOffset" || pin == "PixelDepthOffset" ||
        pin.starts_with("CustomUV")) {
        return true;
    }
    if (pin == "Opacity") {
        why = BlendModeName(s.blendMode);
        return IsTranslucent(s.blendMode) || s.shadingModel == ShadingModel::Subsurface ||
               s.shadingModel == ShadingModel::TwoSidedFoliage;
    }
    if (pin == "OpacityMask") {
        why = BlendModeName(s.blendMode);
        return s.blendMode == Material::BlendMode::Masked;
    }
    if (pin == "Refraction") {
        why = IsTranslucent(s.blendMode) ? "a material without a refraction method"
                                         : BlendModeName(s.blendMode);
        return IsTranslucent(s.blendMode) && s.refraction != Material::RefractionMethod::None;
    }
    why = ShadingModelName(s.shadingModel);
    if (pin == "SubsurfaceColor") {
        return s.shadingModel == ShadingModel::Subsurface ||
               s.shadingModel == ShadingModel::TwoSidedFoliage;
    }
    if (pin == "CustomData0" || pin == "CustomData1") return s.shadingModel == ShadingModel::ClearCoat;
    // BaseColor, Metallic, Specular, Roughness, Anisotropy, Normal, Tangent, AO.
    return s.shadingModel != ShadingModel::Unlit;
}

bool IsVertexPin(std::string_view pin) {
    return pin == "WorldPositionOffset" || pin.starts_with("CustomUV");
}

// Lane `i` of a constant of width `w` (a Float1 broadcasts).
f32 Lane(const Math::Vec4& v, u32 w, u32 i) {
    if (w == 1) return v.x;
    const f32 lanes[4] = {v.x, v.y, v.z, v.w};
    return i < w ? lanes[i] : 0.0f;
}

std::string Swizzle(u32 width) {
    static const char* kSwizzles[] = {"", ".x", ".xy", ".xyz", ".xyzw"};
    return kSwizzles[std::min(width, 4u)];
}

// A chunk's code is "atomic" when it can be an operand without parentheses:
// no operator outside brackets ("v3", "vec3(1.0, 2.0)", "texture(t, uv).rgb").
bool Atomic(std::string_view code) {
    int depth = 0;
    for (char c : code) {
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (c == ' ' && depth == 0) return false;
    }
    return true;
}

}  // namespace

std::string CompiledMaterial::Fingerprint() const {
    u64 h = 1469598103934665603ull;
    auto mix = [&h](std::string_view text) {
        for (unsigned char c : text) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= 0xFF;
        h *= 1099511628211ull;
    };
    mix(functions);
    for (const std::string& inc : includes) mix(inc);
    mix(vertexBody);
    mix(pixelBody);
    for (const Interpolator& i : interpolators) {
        mix(GlslType(i.type));
        mix(i.vertexCode);
    }
    mix(std::to_string(layout.vec4Count));
    for (const TextureSlot& s : layout.textures) {
        mix(std::to_string(s.slot) + (s.cube ? "c" : "2") + (s.srgb ? "s" : "l") + s.defaultTexture + "|" +
            s.parameter + "|" + std::to_string(static_cast<int>(s.sampler.address)) +
            std::to_string(static_cast<int>(s.sampler.filter)) + std::to_string(static_cast<int>(s.samplerType)));
    }
    for (const MaterialOutputValue& o : outputs) {
        mix(o.pin);
        mix(o.stage == Stage::Vertex ? "v" : "p");
        mix(GlslType(o.type));
        mix(o.code);
    }
    mix(staticSwitchKey);
    mix(std::string_view(reinterpret_cast<const char*>(preshader.code.data()), preshader.code.size()));
    for (const Math::Vec4& c : preshader.constants) {
        mix(std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z) + "," + std::to_string(c.w));
    }
    for (const PreshaderParam& p : preshader.params) mix(p.name + (p.vector ? "v" : "s"));
    for (const auto& [collection, name] : preshader.collectionValues) mix(collection + "|" + name);
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return hex;
}

Translator::Translator(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                       StaticSwitchSet staticSwitches)
    : m_material(material), m_library(library), m_switches(std::move(staticSwitches)) {
    Scope root;
    root.graph = &m_material.graph;
    m_scopes.push_back(std::move(root));
}

const MaterialGraph::Graph& Translator::CurrentGraph() const { return *m_scopes[m_scope].graph; }

const Material::MaterialFunction* Translator::CurrentFunction() const { return m_scopes[m_scope].function; }

std::pair<NodeId, std::string> Translator::Attribute(NodeId node, std::string message) const {
    if (m_scope == 0) return {node, std::move(message)};
    std::string path;
    u32 s = m_scope;
    NodeId call = node;
    while (s != 0) {
        const Scope& scope = m_scopes[s];
        const std::string name = scope.function && !scope.function->name.empty() ? scope.function->name : scope.path;
        path = path.empty() ? name : name + " > " + path;
        call = scope.call;
        s = scope.parent;
    }
    return {call, path + ": " + message};
}

i32 Translator::CompileFunctionOutput(NodeId call, const std::string& path, const Material::MaterialFunction& function,
                                      NodeId outputNode) {
    for (u32 s = m_scope; s != 0; s = m_scopes[s].parent) {
        if (m_scopes[s].path == path) {
            Error(call, -1, "'" + path + "' calls itself");
            return -1;
        }
    }
    constexpr u32 kMaxDepth = 16;
    if (m_scopes[m_scope].depth >= kMaxDepth) {
        Error(call, -1, "Material Functions nest more than " + std::to_string(kMaxDepth) + " deep");
        return -1;
    }
    auto [it, added] = m_callScopes.try_emplace(std::make_pair(m_scope, call), 0u);
    if (added) {
        Scope scope;
        scope.graph = &function.graph;
        scope.function = &function;
        scope.path = path;
        scope.parent = m_scope;
        scope.call = call;
        scope.depth = m_scopes[m_scope].depth + 1;
        m_scopes.push_back(std::move(scope));
        it->second = static_cast<u32>(m_scopes.size() - 1);
    }
    const u32 saved = m_scope;
    m_scope = it->second;
    const i32 result = CompileInput(outputNode, 0, m_stage);
    m_scope = saved;
    return result;
}

i32 Translator::CompileCallerInput(u32 pin, bool& linked) {
    linked = false;
    if (m_scope == 0) return -1;
    const Scope& scope = m_scopes[m_scope];
    if (!m_scopes[scope.parent].graph->Source(PinRef{scope.call, pin})) return -1;
    linked = true;
    const u32 saved = m_scope;
    m_scope = scope.parent;
    const i32 result = CompileInput(scope.call, pin, m_stage);
    m_scope = saved;
    return result;
}

void Translator::Error(NodeId node, i32 pin, std::string message) {
    auto [at, text] = Attribute(node, std::move(message));
    m_errors.push_back({at, m_scope == 0 ? pin : -1, std::move(text), false});
}

void Translator::Warning(NodeId node, i32 pin, std::string message) {
    auto [at, text] = Attribute(node, std::move(message));
    m_warnings.push_back({at, m_scope == 0 ? pin : -1, std::move(text), true});
}

i32 Translator::AddChunk(std::string code, ValueType type, Frequency freq, std::optional<Math::Vec4> constant) {
    Chunk chunk;
    chunk.type = type;
    chunk.freq = freq;
    chunk.constant = constant;
    chunk.stage = m_stage;
    if (freq == Frequency::Varying) {
        // Per-vertex/pixel values become locals, so a shared value is
        // computed once in the stage body.
        const std::string name = "v" + std::to_string(m_nextLocal++);
        m_body[static_cast<usize>(m_stage)] +=
            "    " + std::string(GlslType(type)) + " " + name + " = " + code + ";\n";
        chunk.code = name;
    } else {
        chunk.code = Atomic(code) ? std::move(code) : "(" + code + ")";
    }
    m_chunks.push_back(std::move(chunk));
    return static_cast<i32>(m_chunks.size() - 1);
}

i32 Translator::Constant(const Math::Vec4& v, ValueType type) {
    if (type == ValueType::Bool) {
        const bool on = v.x != 0.0f;
        return AddChunk(on ? "true" : "false", type, Frequency::Constant,
                        Math::Vec4{on ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f});
    }
    Math::Vec4 value = v;
    const u32 w = std::max(Width(type), 1u);
    if (w < 4) value.w = 0.0f;
    if (w < 3) value.z = 0.0f;
    if (w < 2) value.y = 0.0f;
    return AddChunk(GlslLiteral(value, type), type, Frequency::Constant, value);
}

i32 Translator::Emit(std::string code, ValueType type, Frequency freq) {
    return AddChunk(std::move(code), type, freq, std::nullopt);
}

i32 Translator::Reference(std::string code, ValueType type, Frequency freq) {
    Chunk chunk;
    chunk.code = std::move(code);
    chunk.type = type;
    chunk.freq = freq;
    chunk.stage = m_stage;
    m_chunks.push_back(std::move(chunk));
    return static_cast<i32>(m_chunks.size() - 1);
}

void Translator::Statement(std::string code) {
    m_body[static_cast<usize>(m_stage)] += "    " + code + "\n";
}

i32 Translator::Memo(NodeId node, std::string_view key, const std::function<i32()>& make) {
    const auto k = std::make_tuple(m_scope, node, std::string(key), m_stage);
    if (const auto it = m_memo.find(k); it != m_memo.end()) return it->second;
    const i32 chunk = make();
    m_memo[k] = chunk;
    return chunk;
}

u32 Translator::AddFunction(std::string definition) {
    m_functions += definition;
    if (!m_functions.empty() && m_functions.back() != '\n') m_functions += '\n';
    m_functions += '\n';
    return m_functionCount++;
}

void Translator::AddInclude(std::string include) {
    if (std::find(m_includes.begin(), m_includes.end(), include) == m_includes.end()) {
        m_includes.push_back(std::move(include));
    }
}

u32 Translator::AddInterpolator(ValueType type, std::string vertexCode) {
    m_interpolators.push_back({type, std::move(vertexCode)});
    return static_cast<u32>(m_interpolators.size() - 1);
}

bool Translator::StaticSwitch(const std::string& name, bool defaultValue) const {
    const auto it = m_switches.find(name);
    return it == m_switches.end() ? defaultValue : it->second;
}

namespace {

const char* KindName(ParameterInfo::Kind k) {
    switch (k) {
        case ParameterInfo::Kind::Vector: return "Vector";
        case ParameterInfo::Kind::Texture: return "Texture";
        case ParameterInfo::Kind::StaticSwitch: return "Static Switch";
        default: return "Scalar";
    }
}

}  // namespace

bool Translator::DeclareParameter(ParameterInfo info) {
    for (const ParameterInfo& p : m_parameters) {
        if (p.name != info.name) continue;
        if (p.kind != info.kind) {
            Error(info.node, -1,
                  "Parameter '" + info.name + "' is already a " + KindName(p.kind) + " parameter (node #" +
                      std::to_string(p.node) + ")");
            return false;
        }
        return true;
    }
    m_parameters.push_back(std::move(info));
    return true;
}

namespace {

// Placeholders for slots assigned at the end: \x01U<chunk>\x01 for a uniform
// value, \x01T<texture>\x01 for a texture binding.
constexpr char kMark = '\x01';

std::string Placeholder(char kind, usize index) {
    return std::string(1, kMark) + kind + std::to_string(index) + std::string(1, kMark);
}

}  // namespace

u16 Translator::PreParam(const std::string& name, bool vector, const Math::Vec4& def) {
    for (usize i = 0; i < m_preParams.size(); ++i) {
        if (m_preParams[i].name == name) return static_cast<u16>(i);
    }
    m_preParams.push_back({name, vector, def});
    return static_cast<u16>(m_preParams.size() - 1);
}

i32 Translator::UniformFromPre(i32 node, ValueType type) {
    Chunk chunk;
    chunk.type = type;
    chunk.freq = Frequency::Uniform;
    chunk.pre = node;
    chunk.stage = m_stage;
    chunk.code = Placeholder('U', m_chunks.size());
    m_chunks.push_back(std::move(chunk));
    return static_cast<i32>(m_chunks.size() - 1);
}

bool Translator::Preshadable(i32 chunk) const {
    if (chunk < 0) return false;
    const Chunk& c = ChunkAt(chunk);
    return c.constant.has_value() || (c.freq == Frequency::Uniform && c.pre >= 0);
}

i32 Translator::PreNodeOf(i32 chunk) {
    const Chunk& c = ChunkAt(chunk);
    if (c.pre >= 0) return c.pre;
    if (!c.constant) return -1;
    PreNode node;
    node.op = PreOp::PushConst;
    // Scalars are kept broadcast in every lane.
    node.constant = Width(c.type) == 1 ? Math::Vec4{c.constant->x, c.constant->x, c.constant->x, c.constant->x}
                                       : *c.constant;
    m_pre.push_back(node);
    return static_cast<i32>(m_pre.size() - 1);
}

i32 Translator::Preshade(PreOp op, std::initializer_list<i32> operands, ValueType result, std::array<u8, 5> extra) {
    PreNode node;
    node.op = op;
    node.extra = extra;
    for (i32 c : operands) {
        if (!Preshadable(c)) return -1;
        node.args.push_back(PreNodeOf(c));
    }
    m_pre.push_back(std::move(node));
    return UniformFromPre(static_cast<i32>(m_pre.size() - 1), result);
}

i32 Translator::CollectionValue(const std::string& collection, const std::string& parameter, ValueType type) {
    u16 index = 0;
    const auto key = std::make_pair(collection, parameter);
    const auto it = std::find(m_collectionValues.begin(), m_collectionValues.end(), key);
    if (it == m_collectionValues.end()) {
        m_collectionValues.push_back(key);
        index = static_cast<u16>(m_collectionValues.size() - 1);
    } else {
        index = static_cast<u16>(it - m_collectionValues.begin());
    }
    PreNode node;
    node.op = PreOp::PushCollection;
    node.index = index;
    m_pre.push_back(node);
    if (Width(type) == 1) {
        // A scalar is broadcast whatever the value source puts in y/z/w.
        PreNode broadcast;
        broadcast.op = PreOp::Swizzle;
        broadcast.args = {static_cast<i32>(m_pre.size() - 1)};
        broadcast.extra = {1, 0, 0, 0, 0};
        m_pre.push_back(std::move(broadcast));
    }
    return UniformFromPre(static_cast<i32>(m_pre.size() - 1), type);
}

i32 Translator::ScalarParameter(ParameterInfo info) {
    info.kind = ParameterInfo::Kind::Scalar;
    const std::string name = info.name;
    const Math::Vec4 def = info.defaultValue;
    if (!DeclareParameter(std::move(info))) return -1;
    PreNode node;
    node.op = PreOp::PushScalarParam;
    node.index = PreParam(name, false, def);
    m_pre.push_back(node);
    return UniformFromPre(static_cast<i32>(m_pre.size() - 1), ValueType::Float1);
}

i32 Translator::VectorParameter(ParameterInfo info) {
    info.kind = ParameterInfo::Kind::Vector;
    const std::string name = info.name;
    const Math::Vec4 def = info.defaultValue;
    if (!DeclareParameter(std::move(info))) return -1;
    PreNode node;
    node.op = PreOp::PushVectorParam;
    node.index = PreParam(name, true, def);
    m_pre.push_back(node);
    return UniformFromPre(static_cast<i32>(m_pre.size() - 1), ValueType::Float4);
}

i32 Translator::Texture(TextureSlot slot, const ParameterInfo* param) {
    if (param) {
        for (const ParameterInfo& p : m_parameters) {
            if (p.name != param->name) continue;
            if (p.kind != ParameterInfo::Kind::Texture) {
                Error(param->node, -1,
                      "Parameter '" + param->name + "' is already a " + KindName(p.kind) + " parameter (node #" +
                          std::to_string(p.node) + ")");
                return -1;
            }
        }
        if (std::none_of(m_parameters.begin(), m_parameters.end(),
                         [&](const ParameterInfo& p) { return p.name == param->name; })) {
            m_parameters.push_back(*param);
        }
        slot.parameter = param->name;
    }
    // One binding per distinct (texture, parameter, sampler, decode).
    const ValueType type = slot.cube ? ValueType::TextureCube : ValueType::Texture2D;
    for (usize i = 0; i < m_textures.size(); ++i) {
        const TextureSlot& s = m_textures[i];
        if (s.defaultTexture == slot.defaultTexture && s.parameter == slot.parameter && s.cube == slot.cube &&
            s.sampler == slot.sampler && s.samplerType == slot.samplerType && s.srgb == slot.srgb) {
            return Reference(Placeholder('T', i), type, Frequency::Uniform);
        }
    }
    m_textures.push_back(std::move(slot));
    return Reference(Placeholder('T', m_textures.size() - 1), type, Frequency::Uniform);
}

void Translator::EmitPreshader(i32 index, PreshaderProgram& program) {
    const PreNode& node = m_pre[static_cast<usize>(index)];
    for (i32 arg : node.args) EmitPreshader(arg, program);
    auto u16le = [&program](u16 v) {
        program.code.push_back(static_cast<u8>(v & 0xFF));
        program.code.push_back(static_cast<u8>(v >> 8));
    };
    program.code.push_back(static_cast<u8>(node.op));
    switch (node.op) {
        case PreOp::PushConst: {
            auto it = std::find_if(program.constants.begin(), program.constants.end(), [&](const Math::Vec4& c) {
                return c.x == node.constant.x && c.y == node.constant.y && c.z == node.constant.z &&
                       c.w == node.constant.w;
            });
            if (it == program.constants.end()) {
                program.constants.push_back(node.constant);
                it = program.constants.end() - 1;
            }
            u16le(static_cast<u16>(it - program.constants.begin()));
            break;
        }
        case PreOp::PushScalarParam:
        case PreOp::PushVectorParam:
        case PreOp::PushCollection: u16le(node.index); break;
        case PreOp::Dot: program.code.push_back(node.extra[0]); break;
        case PreOp::Swizzle:
            for (u8 b : node.extra) program.code.push_back(b);
            break;
        case PreOp::Append:
            program.code.push_back(node.extra[0]);
            program.code.push_back(node.extra[1]);
            break;
        default: break;
    }
}

std::string Translator::Resolve(const std::string& text, CompiledMaterial& out) {
    std::string result;
    result.reserve(text.size());
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] != kMark) {
            result += text[i];
            continue;
        }
        const usize end = text.find(kMark, i + 1);
        const char kind = text[i + 1];
        const usize index = static_cast<usize>(std::stoull(text.substr(i + 2, end - i - 2)));
        i = end;
        if (kind == 'T') {
            auto [it, added] = m_textureSlots.try_emplace(static_cast<u32>(index), 0u);
            if (added) {
                TextureSlot slot = m_textures[index];
                slot.slot = out.layout.vec4Count++;
                it->second = slot.slot;
                out.layout.textures.push_back(std::move(slot));
            }
            result += std::to_string(it->second);
            continue;
        }
        const Chunk& c = m_chunks[index];
        auto [it, added] = m_uniformSlots.try_emplace(static_cast<i32>(index), 0u);
        if (added) {
            it->second = out.layout.vec4Count++;
            EmitPreshader(c.pre, out.preshader);
            out.preshader.code.push_back(static_cast<u8>(PreOp::Store));
            out.preshader.code.push_back(static_cast<u8>(it->second & 0xFF));
            out.preshader.code.push_back(static_cast<u8>(it->second >> 8));
        }
        static const char* kLanes[] = {"", ".x", ".xy", ".xyz", ""};
        result += "MP(" + std::to_string(it->second) + ")" + kLanes[std::min(Width(c.type), 4u)];
    }
    return result;
}

i32 Translator::Binary(std::string_view op, i32 a, i32 b, NodeId at) {
    if (a < 0 || b < 0) return -1;
    const Chunk ca = ChunkAt(a);
    const Chunk cb = ChunkAt(b);
    const u32 wa = Width(ca.type), wb = Width(cb.type);
    if (wa == 0 || wb == 0) {
        Error(at, -1, "'" + std::string(op) + "' needs float inputs");
        return -1;
    }
    if (wa != wb && wa != 1 && wb != 1) {
        Error(at, -1,
              "Can't combine a Float" + std::to_string(wa) + " and a Float" + std::to_string(wb) +
                  " (use Append or Component Mask to match widths)");
        return -1;
    }
    const u32 w = std::max(wa, wb);
    const ValueType type = FloatType(w);
    if (ca.constant && cb.constant) {
        f32 out[4] = {0, 0, 0, 0};
        bool finite = true;
        for (u32 i = 0; i < w; ++i) {
            const f32 x = Lane(*ca.constant, wa, i), y = Lane(*cb.constant, wb, i);
            switch (op.front()) {
                case '+': out[i] = x + y; break;
                case '-': out[i] = x - y; break;
                case '*': out[i] = x * y; break;
                default: out[i] = x / y; break;
            }
            finite = finite && std::isfinite(out[i]);
        }
        if (finite) return Constant({out[0], out[1], out[2], out[3]}, type);
    }
    const Frequency freq = std::max(ca.freq, cb.freq);
    if (freq == Frequency::Uniform) {
        const PreOp pre = op.front() == '+' ? PreOp::Add : op.front() == '-' ? PreOp::Sub
                        : op.front() == '*' ? PreOp::Mul : PreOp::Div;
        const i32 r = Preshade(pre, {a, b}, type);
        if (r >= 0) return r;
    }
    return AddChunk(ca.code + " " + std::string(op) + " " + cb.code, type, freq, std::nullopt);
}

i32 Translator::Call(std::string_view fn, std::span<const i32> args, ValueType result, NodeId,
                     Frequency minFreq) {
    Frequency freq = minFreq;
    std::string code = std::string(fn) + "(";
    for (usize i = 0; i < args.size(); ++i) {
        if (args[i] < 0) return -1;
        const Chunk& c = ChunkAt(args[i]);
        freq = std::max(freq, c.freq);
        if (i > 0) code += ", ";
        code += c.code;
    }
    return AddChunk(code + ")", result, freq, std::nullopt);
}

i32 Translator::Cast(i32 chunk, ValueType to, NodeId at) {
    if (chunk < 0) return -1;
    const Chunk c = ChunkAt(chunk);
    if (c.type == to) return chunk;
    const u32 from = Width(c.type), w = Width(to);
    if (from == 0 || w == 0) {
        Error(at, -1, "Can't convert this value to a Float" + std::to_string(w));
        return -1;
    }
    if (c.freq == Frequency::Uniform && c.pre >= 0 && (from == 1 || from > w)) {
        if (from > w) Warning(at, -1, "Truncated a Float" + std::to_string(from) + " to a Float" + std::to_string(w));
        return UniformFromPre(c.pre, to);
    }
    if (from == 1) {
        if (c.constant) return Constant({c.constant->x, c.constant->x, c.constant->x, c.constant->x}, to);
        return AddChunk(std::string(GlslType(to)) + "(" + c.code + ")", to, c.freq, std::nullopt);
    }
    if (from > w) {
        Warning(at, -1, "Truncated a Float" + std::to_string(from) + " to a Float" + std::to_string(w));
        if (c.constant) return Constant(*c.constant, to);
        return AddChunk(c.code + Swizzle(w), to, c.freq, std::nullopt);
    }
    Error(at, -1,
          "Can't convert a Float" + std::to_string(from) + " to a Float" + std::to_string(w) +
              " (use Append)");
    return -1;
}

i32 Translator::CompileInput(NodeId node, u32 inputPin, Stage stage) {
    const MaterialGraph::Graph& graph = CurrentGraph();
    const Node* n = graph.Find(node);
    const ExpressionDesc* desc = n ? ExpressionRegistry::Get().Find(n->type) : nullptr;
    if (!desc) return -1;
    if (const auto source = graph.Source(PinRef{node, inputPin})) {
        return CompileOutput(source->node, source->pin, stage);
    }
    if (inputPin >= desc->inputs.size()) {
        // Dynamic pins (Custom, Function Call) default to 0 when unlinked.
        if (desc->dynamicPins) {
            const Stage saved = m_stage;
            m_stage = stage;
            const i32 zero = Constant({0.0f, 0.0f, 0.0f, 0.0f}, ValueType::Float1);
            m_stage = saved;
            return zero;
        }
        Error(node, static_cast<i32>(inputPin), "Unknown input pin");
        return -1;
    }
    const MaterialGraph::PinDesc& pin = desc->inputs[inputPin];
    if (!pin.optional) {
        Error(node, static_cast<i32>(inputPin), "Missing input '" + pin.name + "'");
        return -1;
    }
    const MaterialGraph::PropertyValue def = MaterialGraph::InputDefault(*desc, *n, inputPin);
    ValueType type = pin.type;
    if (type == ValueType::Any) type = def.IsVec4() ? ValueType::Float4 : ValueType::Float1;
    if (Width(type) == 0) {
        Error(node, static_cast<i32>(inputPin), "Input '" + pin.name + "' needs a link");
        return -1;
    }
    const Stage saved = m_stage;
    m_stage = stage;
    const i32 chunk = Constant(def.AsVec4(), type);
    m_stage = saved;
    return chunk;
}

i32 Translator::CompileOutput(NodeId node, u32 outputPin, Stage stage) {
    const auto key = std::make_tuple(m_scope, node, outputPin, stage);
    if (const auto it = m_cache.find(key); it != m_cache.end()) return it->second;

    const Node* n = CurrentGraph().Find(node);
    const ExpressionDesc* desc = n ? ExpressionRegistry::Get().Find(n->type) : nullptr;
    if (!desc) {
        Error(node, -1, n ? "Unknown expression '" + n->type + "'" : "Missing node");
        return m_cache[key] = -1;
    }
    if (desc->pixelOnly && stage == Stage::Vertex) {
        Error(node, -1,
              desc->displayName + " can't be used in the vertex shader (World Position Offset / Custom UVs)");
        return m_cache[key] = -1;
    }
    const auto progress = std::make_tuple(m_scope, node, stage);
    if (m_inProgress.contains(progress)) {
        Error(node, -1, desc->displayName + " depends on itself");
        return -1;
    }
    const Detail::CompileFn fn = Detail::FindCompiler(n->type);
    if (!fn) {
        Error(node, -1, desc->displayName + " can't be compiled yet");
        return m_cache[key] = -1;
    }
    m_inProgress.insert(progress);
    const Stage saved = m_stage;
    m_stage = stage;
    const i32 result = fn(*this, *n, *desc, outputPin);
    m_stage = saved;
    m_inProgress.erase(progress);
    return m_cache[key] = result;
}

CompiledMaterial Translator::Finish() {
    CompiledMaterial out;
    for (const auto& [name, value] : m_switches) {
        if (!out.staticSwitchKey.empty()) out.staticSwitchKey += "|";
        out.staticSwitchKey += name + (value ? "=1" : "=0");
    }

    const MaterialGraph::Graph& graph = m_material.graph;
    if (const std::vector<NodeId> cycle = graph.FindCycle(); !cycle.empty()) {
        std::string path;
        for (NodeId id : cycle) {
            const Node* n = graph.Find(id);
            const ExpressionDesc* d = n ? ExpressionRegistry::Get().Find(n->type) : nullptr;
            if (!path.empty()) path += " -> ";
            path += (d ? d->displayName : std::string("?")) + " #" + std::to_string(id);
        }
        for (NodeId id : cycle) Error(id, -1, "The graph has a cycle: " + path);
        out.errors = std::move(m_errors);
        return out;
    }

    const NodeId outputNode = graph.OutputNode();
    const Node* output = graph.Find(outputNode);
    const ExpressionDesc* desc = output ? ExpressionRegistry::Get().Find(output->type) : nullptr;
    if (desc) {
        for (u32 pin = 0; pin < desc->inputs.size(); ++pin) {
            const MaterialGraph::PinDesc& p = desc->inputs[pin];
            const bool linked = graph.Source(PinRef{outputNode, pin}).has_value();
            std::string why;
            if (!PinUsed(m_material.settings, p.name, why)) {
                if (linked) Warning(outputNode, static_cast<i32>(pin), "Pin " + p.name + " is ignored for " + why);
                continue;
            }
            // Custom UVs only replace the mesh UVs when something drives them.
            if (p.name.starts_with("CustomUV") && !linked) continue;
            const Stage stage = IsVertexPin(p.name) ? Stage::Vertex : Stage::Pixel;
            const i32 value = Cast(CompileInput(outputNode, pin, stage), p.type, outputNode);
            if (value < 0) continue;
            out.outputs.push_back({p.name, stage, p.type, ChunkAt(value).code});
        }
    }
    // Slots in first-use order: vertex body, pixel body, interpolants,
    // outputs, functions (deterministic for identical graphs).
    out.vertexBody = Resolve(m_body[static_cast<usize>(Stage::Vertex)], out);
    out.pixelBody = Resolve(m_body[static_cast<usize>(Stage::Pixel)], out);
    out.interpolators = m_interpolators;
    for (Interpolator& i : out.interpolators) i.vertexCode = Resolve(i.vertexCode, out);
    for (MaterialOutputValue& o : out.outputs) o.code = Resolve(o.code, out);
    out.functions = Resolve(m_functions, out);
    out.includes = m_includes;
    out.parameters = m_parameters;
    out.preshader.params = m_preParams;
    out.preshader.collectionValues = m_collectionValues;
    m_layout = out.layout;
    out.errors = std::move(m_errors);
    out.warnings = std::move(m_warnings);
    return out;
}

}  // namespace Luma::MaterialCompiler
