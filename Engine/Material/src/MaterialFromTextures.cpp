#include "Luma/Material/MaterialFromTextures.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Luma::Material {
namespace {

// Splits the asset name (last path component) into tokens, keeping case.
std::vector<std::string> Tokens(const std::string& virtualPath) {
    const std::string name = virtualPath.substr(virtualPath.find_last_of('/') + 1);
    std::vector<std::string> out;
    std::string cur;
    for (char c : name) {
        if (c == '_' || c == '-' || c == '.' || c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Is(const std::string& token, std::initializer_list<const char*> names) {
    const std::string t = Lower(token);
    for (const char* n : names) {
        if (t == n) return true;
    }
    return false;
}

enum class Role {
    None, BaseColor, Normal, Roughness, Metallic, AO, Packed, Emissive, Opacity,
    Specular, Gloss, Cavity, Height, Bump
};

Role RoleOf(const std::string& token) {
    if (Is(token, {"diff", "diffuse", "albedo", "basecolor", "color", "col"})) return Role::BaseColor;
    if (Is(token, {"nor", "normal", "nrm", "normals"})) return Role::Normal;
    if (Is(token, {"rough", "roughness"})) return Role::Roughness;
    if (Is(token, {"metal", "metallic", "metalness"})) return Role::Metallic;
    if (Is(token, {"ao", "occlusion"})) return Role::AO;
    if (Is(token, {"arm", "orm"})) return Role::Packed;
    if (Is(token, {"emiss", "emissive", "emission"})) return Role::Emissive;
    if (Is(token, {"opacity", "mask", "alpha"})) return Role::Opacity;
    if (Is(token, {"spec", "specular"})) return Role::Specular;
    if (Is(token, {"gloss", "glossiness"})) return Role::Gloss;
    if (Is(token, {"cavity"})) return Role::Cavity;
    if (Is(token, {"disp", "displacement", "height"})) return Role::Height;
    if (Is(token, {"bump"})) return Role::Bump;
    return Role::None;
}

void SetTexture(Material& m, MaterialParameter p, const std::string& path, TextureChannel channel) {
    MaterialInput& in = m.inputs[static_cast<usize>(p)];
    in.texture = path;
    in.channel = channel;
    in.tint = {1.0f, 1.0f, 1.0f, 1.0f};
}

}  // namespace

MaterialFromTexturesResult BuildMaterialFromTextures(
    const std::vector<std::string>& textureVirtualPaths) {
    MaterialFromTexturesResult r;
    r.material = Material::MakeDefault();
    std::string prefix;
    // A gloss map only stands in for roughness when the set has none.
    bool hasRoughness = false;
    for (const std::string& path : textureVirtualPaths) {
        const std::vector<std::string> tokens = Tokens(path);
        for (usize i = tokens.size(); i-- > 0;) {
            const Role role = RoleOf(tokens[i]);
            if (role == Role::None) continue;
            hasRoughness = hasRoughness || role == Role::Roughness || role == Role::Packed;
            break;
        }
    }
    for (const std::string& path : textureVirtualPaths) {
        const std::vector<std::string> tokens = Tokens(path);
        Role role = Role::None;
        usize roleAt = tokens.size();
        // The last role token decides ("rough_plaster_diff" is a color map;
        // earlier words belong to the asset's name).
        for (usize i = tokens.size(); i-- > 0;) {
            role = RoleOf(tokens[i]);
            if (role != Role::None) {
                roleAt = i;
                break;
            }
        }
        Material& m = r.material;
        switch (role) {
            case Role::BaseColor: SetTexture(m, MaterialParameter::BaseColor, path, TextureChannel::RGB); break;
            case Role::Normal: {
                SetTexture(m, MaterialParameter::Normal, path, TextureChannel::RGB);
                bool dx = false;
                for (const std::string& t : tokens) dx = dx || Is(t, {"dx", "directx"});
                m.inputs[static_cast<usize>(MaterialParameter::Normal)].flipGreen = dx;
                break;
            }
            case Role::Roughness: SetTexture(m, MaterialParameter::Roughness, path, TextureChannel::R); break;
            case Role::Metallic: SetTexture(m, MaterialParameter::Metallic, path, TextureChannel::R); break;
            case Role::AO: SetTexture(m, MaterialParameter::AmbientOcclusion, path, TextureChannel::R); break;
            case Role::Packed:
                SetTexture(m, MaterialParameter::AmbientOcclusion, path, TextureChannel::R);
                SetTexture(m, MaterialParameter::Roughness, path, TextureChannel::G);
                SetTexture(m, MaterialParameter::Metallic, path, TextureChannel::B);
                break;
            case Role::Emissive: SetTexture(m, MaterialParameter::EmissiveColor, path, TextureChannel::RGB); break;
            case Role::Opacity:
                SetTexture(m, MaterialParameter::OpacityMask, path, TextureChannel::R);
                m.settings.blendMode = BlendMode::Masked;
                break;
            case Role::Specular: SetTexture(m, MaterialParameter::Specular, path, TextureChannel::R); break;
            case Role::Gloss:
                if (hasRoughness) {
                    r.redundant.push_back(path);
                    continue;
                }
                SetTexture(m, MaterialParameter::Roughness, path, TextureChannel::R);
                m.inputs[static_cast<usize>(MaterialParameter::Roughness)].invert = true;
                break;
            case Role::Cavity: SetTexture(m, MaterialParameter::Cavity, path, TextureChannel::R); break;
            case Role::Height: SetTexture(m, MaterialParameter::Height, path, TextureChannel::R); break;
            case Role::Bump: SetTexture(m, MaterialParameter::Bump, path, TextureChannel::R); break;
            case Role::None:
                r.ignored.push_back(path);
                continue;
        }
        // The material is named after the shared prefix (tokens before the
        // role), minus a leading texture "T" prefix.
        if (prefix.empty()) {
            usize first = 0;
            if (roleAt > 1 && Is(tokens[0], {"t", "tex"})) first = 1;
            for (usize i = first; i < roleAt; ++i) {
                if (!prefix.empty()) prefix += "_";
                prefix += tokens[i];
            }
        }
    }
    r.suggestedName = "M_" + (prefix.empty() ? std::string("NewMaterial") : prefix);
    r.material.name = r.suggestedName;
    return r;
}

}  // namespace Luma::Material
