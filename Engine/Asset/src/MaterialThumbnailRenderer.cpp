#include "Luma/Asset/MaterialThumbnailRenderer.h"
#include "Luma/Core/Log.h"
#include "Luma/Math/Math.h"
#include "Luma/Math/MathUtils.h"
#include "Luma/Serialization/Json.h"

#include <fstream>
#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma {

MaterialThumbnailRenderer::MaterialThumbnailRenderer() = default;

bool MaterialThumbnailRenderer::CanRender(const AssetId& assetId, const std::filesystem::path& nativePath) const {
    (void)assetId;
    if (!std::filesystem::exists(nativePath)) {
        return false;
    }
    std::string ext = nativePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".lmat" || ext == ".lumat" || ext == ".mat");
}

void MaterialThumbnailRenderer::GetPreferredSize(u32& outWidth, u32& outHeight) const {
    outWidth = 128;
    outHeight = 128;
}

bool MaterialThumbnailRenderer::ParseMaterialFile(const std::filesystem::path& path, MaterialThumbnailData& outMaterial) {
    outMaterial.name = path.stem().string();
    outMaterial.baseColor = Math::Vec3(0.8f, 0.8f, 0.8f);
    outMaterial.roughness = 0.5f;
    outMaterial.metallic = 0.0f;
    outMaterial.specular = 0.5f;
    outMaterial.emissive = Math::Vec3(0.0f, 0.0f, 0.0f);

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Try parsing as JSON first
    auto jsonVal = ParseJson(content);
    if (jsonVal && jsonVal->IsObject()) {
        if (auto* name = jsonVal->Find("name"); name && name->IsString()) {
            outMaterial.name = name->AsString();
        }
        // Read a Vec3 array (or leave the fallback value untouched).
        auto readVec3 = [&outMaterial](const SerialValue* v,
                                       Math::Vec3& dst) {
            if (v && v->IsArray() && v->Size() >= 3) {
                dst.x = static_cast<f32>(v->At(0).AsFloat(dst.x));
                dst.y = static_cast<f32>(v->At(1).AsFloat(dst.y));
                dst.z = static_cast<f32>(v->At(2).AsFloat(dst.z));
            }
        };
        // Real .lmat layout (MaterialSerializer): constant fallbacks live
        // under "constants". Material-level base color / metallic /
        // roughness / emissive drive the preview sphere.
        if (auto* constants = jsonVal->Find("constants");
            constants && constants->IsObject()) {
            readVec3(constants->Find("baseColor"), outMaterial.baseColor);
            readVec3(constants->Find("emissive"), outMaterial.emissive);
            if (auto* r = constants->Find("roughness"); r && r->IsNumber()) {
                outMaterial.roughness = static_cast<f32>(r->AsFloat(0.5));
            }
            if (auto* m = constants->Find("metallic"); m && m->IsNumber()) {
                outMaterial.metallic = static_cast<f32>(m->AsFloat(0.0));
            }
        }
        // Legacy / hand-written materials may keep these at the top level.
        readVec3(jsonVal->Find("baseColor"), outMaterial.baseColor);
        readVec3(jsonVal->Find("color"), outMaterial.baseColor);
        if (auto* r = jsonVal->Find("roughness"); r && r->IsNumber()) {
            outMaterial.roughness = static_cast<f32>(r->AsFloat(0.5));
        }
        if (auto* m = jsonVal->Find("metallic"); m && m->IsNumber()) {
            outMaterial.metallic = static_cast<f32>(m->AsFloat(0.0));
        }
        if (auto* s = jsonVal->Find("specular"); s && s->IsNumber()) {
            outMaterial.specular = static_cast<f32>(s->AsFloat(0.5));
        }
        if (auto* tex = jsonVal->Find("albedoTexture"); tex && tex->IsString()) {
            outMaterial.albedoTexturePath = tex->AsString();
        } else if (auto* tex2 = jsonVal->Find("texture"); tex2 && tex2->IsString()) {
            outMaterial.albedoTexturePath = tex2->AsString();
        }
    } else {
        // Line by line key=value fallback
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) eq = line.find(':');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            key.erase(0, key.find_first_not_of(" \t\r\n\""));
            key.erase(key.find_last_not_of(" \t\r\n\"") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n\""));
            val.erase(val.find_last_not_of(" \t\r\n\"") + 1);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            if (key == "roughness") {
                outMaterial.roughness = static_cast<f32>(std::atof(val.c_str()));
            } else if (key == "metallic") {
                outMaterial.metallic = static_cast<f32>(std::atof(val.c_str()));
            } else if (key == "specular") {
                outMaterial.specular = static_cast<f32>(std::atof(val.c_str()));
            } else if (key == "albedotexture" || key == "diffusetexture" || key == "texture") {
                outMaterial.albedoTexturePath = val;
            }
        }
    }

    // Try loading texture if path is provided
    auto dir = path.parent_path();
    if (!outMaterial.albedoTexturePath.empty()) {
        std::vector<std::filesystem::path> candidates = {
            dir / outMaterial.albedoTexturePath,
            dir / "Textures" / outMaterial.albedoTexturePath,
            dir / "textures" / outMaterial.albedoTexturePath,
            outMaterial.albedoTexturePath
        };
        for (const auto& c : candidates) {
            if (std::filesystem::exists(c)) {
                int w = 0, h = 0, ch = 0;
                unsigned char* data = stbi_load(c.string().c_str(), &w, &h, &ch, 4);
                if (data && w > 0 && h > 0) {
                    outMaterial.albedoTexture.width = static_cast<u32>(w);
                    outMaterial.albedoTexture.height = static_cast<u32>(h);
                    outMaterial.albedoTexture.pixels.assign(data, data + w * h * 4);
                    stbi_image_free(data);
                    break;
                }
            }
        }
    }

    return true;
}

void MaterialThumbnailRenderer::GenerateSphereMesh(LumaMeshData& outMesh, u32 segments, u32 rings) {
    outMesh.name = "MaterialPreviewSphere";
    outMesh.vertices.clear();
    outMesh.indices.clear();

    const f32 PI = 3.14159265359f;
    const f32 radius = 1.0f;

    for (u32 r = 0; r <= rings; ++r) {
        f32 theta = static_cast<f32>(r) * PI / static_cast<f32>(rings);
        f32 sinTheta = std::sin(theta);
        f32 cosTheta = std::cos(theta);

        for (u32 s = 0; s <= segments; ++s) {
            f32 phi = static_cast<f32>(s) * 2.0f * PI / static_cast<f32>(segments);
            f32 sinPhi = std::sin(phi);
            f32 cosPhi = std::cos(phi);

            Math::Vec3 normal(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta);
            Math::Vec3 position = normal * radius;
            Math::Vec2 uv(static_cast<f32>(s) / static_cast<f32>(segments),
                          static_cast<f32>(r) / static_cast<f32>(rings));

            LumaVertex vert{};
            vert.position = position;
            vert.normal = normal;
            vert.texCoord = uv;
            outMesh.vertices.push_back(vert);
        }
    }

    for (u32 r = 0; r < rings; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            u32 i0 = r * (segments + 1) + s;
            u32 i1 = (r + 1) * (segments + 1) + s;
            u32 i2 = (r + 1) * (segments + 1) + (s + 1);
            u32 i3 = r * (segments + 1) + (s + 1);

            outMesh.indices.push_back(i0);
            outMesh.indices.push_back(i1);
            outMesh.indices.push_back(i2);

            outMesh.indices.push_back(i0);
            outMesh.indices.push_back(i2);
            outMesh.indices.push_back(i3);
        }
    }

    outMesh.bounds.min = Math::Vec3(-1.0f, -1.0f, -1.0f);
    outMesh.bounds.max = Math::Vec3(1.0f, 1.0f, 1.0f);
    outMesh.bounds.center = Math::Vec3(0.0f, 0.0f, 0.0f);
    outMesh.bounds.radius = 1.0f;
}

bool MaterialThumbnailRenderer::RenderPreviewSphere(
    const MaterialThumbnailData& mat, std::vector<u8>& outPixels,
    u32 width, u32 height, f32 yawDeg, f32 pitchDeg, f32 distance,
    Math::Vec3 target) {
    LumaMeshData sphere;
    GenerateSphereMesh(sphere, 32, 16);
    return RenderMaterialSphere(sphere, mat, outPixels, width, height,
                                yawDeg, pitchDeg, distance, target);
}

bool MaterialThumbnailRenderer::LoadEnvironmentTexture(
    const std::filesystem::path& path, MaterialThumbnailData& outMaterial) {
    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels,
                              STBI_rgb);
    (void)channels;
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    outMaterial.environmentWidth = static_cast<u32>(width);
    outMaterial.environmentHeight = static_cast<u32>(height);
    outMaterial.environmentPixels.resize(
        static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < outMaterial.environmentPixels.size(); ++i) {
        outMaterial.environmentPixels[i] =
            Math::Vec3{pixels[i * 3 + 0], pixels[i * 3 + 1], pixels[i * 3 + 2]};
    }
    stbi_image_free(pixels);
    return true;
}

bool MaterialThumbnailRenderer::RenderMaterialSphere(
    const LumaMeshData& sphere, const MaterialThumbnailData& mat,
    std::vector<u8>& outPixels, u32 width, u32 height, f32 yawDeg,
    f32 pitchDeg, f32 distance, Math::Vec3 target) {
    outPixels.resize(width * height * 4);
    std::vector<f32> depthBuffer(width * height, 1e9f);

    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    constexpr f32 kPi = 3.14159265358979323846f;
    const f32 fovY = Math::Radians(55.0f);
    const f32 tanHalfFov = std::tan(fovY * 0.5f);
    const f32 yaw = Math::Radians(yawDeg);
    const f32 pitch = Math::Radians(std::clamp(pitchDeg, -82.0f, 82.0f));
    const f32 orbitDistance = std::clamp(distance, 1.2f, 12.0f);
    const f32 cosPitch = std::cos(pitch);

    Math::Vec3 eye{
        target.x + orbitDistance * cosPitch * std::sin(yaw),
        target.y + orbitDistance * std::sin(pitch),
        target.z + orbitDistance * cosPitch * std::cos(yaw)};
    Math::Vec3 up(0.0f, 1.0f, 0.0f);
    Math::Vec3 f = Math::Normalize(target - eye);
    Math::Vec3 s = Math::Normalize(Math::Cross(f, up));
    Math::Vec3 u = Math::Cross(s, f);

    auto toneMap = [](const Math::Vec3& hdr, f32 exposure) {
        Math::Vec3 mapped{1.0f - std::exp(-hdr.x * exposure),
                          1.0f - std::exp(-hdr.y * exposure),
                          1.0f - std::exp(-hdr.z * exposure)};
        mapped.x = std::pow(std::clamp(mapped.x, 0.0f, 1.0f), 1.0f / 2.2f);
        mapped.y = std::pow(std::clamp(mapped.y, 0.0f, 1.0f), 1.0f / 2.2f);
        mapped.z = std::pow(std::clamp(mapped.z, 0.0f, 1.0f), 1.0f / 2.2f);
        return mapped;
    };

    auto sampleEnvironment = [&](const Math::Vec3& direction) {
        if (mat.environmentWidth > 0 && mat.environmentHeight > 0 &&
            mat.environmentPixels.size() ==
                static_cast<size_t>(mat.environmentWidth) * mat.environmentHeight) {
            f32 envU = 0.5f + std::atan2(direction.z, direction.x) / (2.0f * kPi);
            f32 envV = 0.5f - std::asin(std::clamp(direction.y, -1.0f, 1.0f)) / kPi;
            envU -= std::floor(envU);
            envV = std::clamp(envV, 0.0f, 1.0f);
            u32 px = std::min(mat.environmentWidth - 1,
                              static_cast<u32>(envU * mat.environmentWidth));
            u32 py = std::min(mat.environmentHeight - 1,
                              static_cast<u32>(envV * mat.environmentHeight));
            return toneMap(mat.environmentPixels[
                static_cast<size_t>(py) * mat.environmentWidth + px], 1.2f);
        }

        const f32 t = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
        return Math::Vec3{0.08f + 0.12f * t, 0.09f + 0.13f * t,
                          0.11f + 0.16f * t};
    };

    auto writePixel = [&](u32 x, u32 y, const Math::Vec3& color) {
        const u32 idx = (y * width + x) * 4;
        outPixels[idx + 0] = static_cast<u8>(std::clamp(color.x * 255.0f, 0.0f, 255.0f));
        outPixels[idx + 1] = static_cast<u8>(std::clamp(color.y * 255.0f, 0.0f, 255.0f));
        outPixels[idx + 2] = static_cast<u8>(std::clamp(color.z * 255.0f, 0.0f, 255.0f));
        outPixels[idx + 3] = 255;
    };

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            f32 nx = (static_cast<f32>(x) + 0.5f) /
                         static_cast<f32>(width) * 2.0f - 1.0f;
            f32 ny = 1.0f - (static_cast<f32>(y) + 0.5f) /
                               static_cast<f32>(height) * 2.0f;
            Math::Vec3 ray = Math::Normalize(
                f + s * (nx * aspect * tanHalfFov) + u * (ny * tanHalfFov));
            Math::Vec3 color = sampleEnvironment(ray);

            if (ray.y < -0.0001f) {
                const f32 planeY = -1.0f;
                const f32 t = (planeY - eye.y) / ray.y;
                if (t > 0.0f) {
                    Math::Vec3 p = eye + ray * t;
                    const f32 lineX = std::abs(p.x - std::round(p.x));
                    const f32 lineZ = std::abs(p.z - std::round(p.z));
                    const f32 majorX = std::abs(p.x * 0.1f - std::round(p.x * 0.1f));
                    const f32 majorZ = std::abs(p.z * 0.1f - std::round(p.z * 0.1f));
                    const bool major = majorX < 0.004f || majorZ < 0.004f;
                    const bool minor = lineX < 0.012f || lineZ < 0.012f;
                    const f32 checker =
                        (static_cast<int>(std::floor(p.x)) +
                         static_cast<int>(std::floor(p.z))) & 1 ? 0.11f : 0.13f;
                    color = Math::Vec3{checker, checker + 0.01f, checker + 0.02f};
                    if (minor) color = Math::Vec3{0.24f, 0.26f, 0.28f};
                    if (major) color = Math::Vec3{0.35f, 0.37f, 0.40f};
                    const f32 fade = std::clamp(1.0f - (t / 18.0f), 0.0f, 1.0f);
                    Math::Vec3 env = sampleEnvironment(ray);
                    color = color * fade + env * (1.0f - fade);
                    depthBuffer[y * width + x] = t;
                }
            }
            writePixel(x, y, color);
        }
    }

    struct ScreenVert {
        f32 x, y, z;
        bool visible;
        Math::Vec3 world;
        Math::Vec3 normal;
        Math::Vec2 uv;
    };

    std::vector<ScreenVert> screenVerts(sphere.vertices.size());
    for (size_t i = 0; i < sphere.vertices.size(); ++i) {
        const auto& v = sphere.vertices[i];
        Math::Vec3 rel = v.position - eye;
        const f32 camZ = Math::Dot(rel, f);
        const f32 camX = Math::Dot(rel, s);
        const f32 camY = Math::Dot(rel, u);
        screenVerts[i].visible = camZ > 0.05f;
        const f32 ndcX = camX / (camZ * tanHalfFov * aspect);
        const f32 ndcY = camY / (camZ * tanHalfFov);
        screenVerts[i].x = (ndcX + 1.0f) * 0.5f * static_cast<f32>(width);
        screenVerts[i].y = (1.0f - ndcY) * 0.5f * static_cast<f32>(height);
        screenVerts[i].z = camZ;
        screenVerts[i].world = v.position;
        screenVerts[i].normal = v.normal;
        screenVerts[i].uv = v.texCoord;
    }

    Math::Vec3 lightDir1 = Math::Normalize(Math::Vec3(0.5f, 0.8f, 0.9f));
    Math::Vec3 lightDir2 = Math::Normalize(Math::Vec3(-0.7f, -0.2f, 0.4f));

    auto sampleTexture = [&](f32 u, f32 v) -> Math::Vec3 {
        if (!mat.albedoTexture.IsValid()) {
            return mat.baseColor;
        }
        u = u - std::floor(u);
        v = v - std::floor(v);
        f32 tx = u * static_cast<f32>(mat.albedoTexture.width - 1);
        f32 ty = (1.0f - v) * static_cast<f32>(mat.albedoTexture.height - 1);
        u32 px = std::clamp(static_cast<u32>(tx + 0.5f), 0u, mat.albedoTexture.width - 1);
        u32 py = std::clamp(static_cast<u32>(ty + 0.5f), 0u, mat.albedoTexture.height - 1);
        u32 idx = (py * mat.albedoTexture.width + px) * 4;
        return Math::Vec3{
            static_cast<f32>(mat.albedoTexture.pixels[idx + 0]) / 255.0f,
            static_cast<f32>(mat.albedoTexture.pixels[idx + 1]) / 255.0f,
            static_cast<f32>(mat.albedoTexture.pixels[idx + 2]) / 255.0f
        };
    };

    for (size_t i = 0; i < sphere.indices.size(); i += 3) {
        u32 i0 = sphere.indices[i];
        u32 i1 = sphere.indices[i + 1];
        u32 i2 = sphere.indices[i + 2];
        if (i0 >= screenVerts.size() || i1 >= screenVerts.size() || i2 >= screenVerts.size()) continue;

        const auto& v0 = screenVerts[i0];
        const auto& v1 = screenVerts[i1];
        const auto& v2 = screenVerts[i2];
        if (!v0.visible || !v1.visible || !v2.visible) continue;

        f32 area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
        if (std::abs(area) <= 0.0001f) continue;
        const f32 areaSign = area < 0.0f ? -1.0f : 1.0f;
        f32 invArea = 1.0f / area;

        i32 minX = std::max(0, static_cast<i32>(std::floor(std::min({v0.x, v1.x, v2.x}))));
        i32 maxX = std::min(static_cast<i32>(width) - 1, static_cast<i32>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
        i32 minY = std::max(0, static_cast<i32>(std::floor(std::min({v0.y, v1.y, v2.y}))));
        i32 maxY = std::min(static_cast<i32>(height) - 1, static_cast<i32>(std::ceil(std::max({v0.y, v1.y, v2.y}))));

        for (i32 py = minY; py <= maxY; ++py) {
            for (i32 px = minX; px <= maxX; ++px) {
                f32 fx = static_cast<f32>(px) + 0.5f;
                f32 fy = static_cast<f32>(py) + 0.5f;

                f32 w0 = ((v1.x - fx) * (v2.y - fy) - (v1.y - fy) * (v2.x - fx)) * invArea;
                f32 w1 = ((v2.x - fx) * (v0.y - fy) - (v2.y - fy) * (v0.x - fx)) * invArea;
                f32 w2 = 1.0f - w0 - w1;

                if (w0 * areaSign >= 0.0f && w1 * areaSign >= 0.0f &&
                    w2 * areaSign >= 0.0f) {
                    f32 z = w0 * v0.z + w1 * v1.z + w2 * v2.z;
                    u32 pidx = py * width + px;
                    if (z < depthBuffer[pidx]) {
                        depthBuffer[pidx] = z;

                        Math::Vec3 norm = Math::Normalize(v0.normal * w0 + v1.normal * w1 + v2.normal * w2);
                        Math::Vec2 uv = v0.uv * w0 + v1.uv * w1 + v2.uv * w2;
                        Math::Vec3 world = v0.world * w0 + v1.world * w1 + v2.world * w2;
                        Math::Vec3 albedo = sampleTexture(uv.x, uv.y);

                        // Diffuse lighting
                        f32 diff1 = std::max(0.0f, Math::Dot(norm, lightDir1));
                        f32 diff2 = std::max(0.0f, Math::Dot(norm, lightDir2)) * 0.30f;
                        f32 ambient = 0.25f;

                        // Specular highlight based on roughness
                        Math::Vec3 viewDir = Math::Normalize(eye - world);
                        Math::Vec3 halfDir = Math::Normalize(lightDir1 + viewDir);
                        f32 specPower = std::max(2.0f, (1.0f - mat.roughness) * 128.0f);
                        f32 spec = std::pow(std::max(0.0f, Math::Dot(norm, halfDir)), specPower) * (1.0f - mat.roughness);

                        // Rim reflection
                        f32 rim = std::pow(1.0f - std::max(0.0f, Math::Dot(norm, viewDir)), 3.0f) * 0.25f;

                        // Metal-aware preview: metals reflect the base color in
                        // the specular term and have little diffuse response.
                        f32 diffuseIntensity = diff1 * 0.70f + diff2 + ambient + rim;
                        Math::Vec3 specColor =
                            albedo * (spec * mat.metallic) +
                            Math::Vec3(spec, spec, spec) * (1.0f - mat.metallic);
                        Math::Vec3 finalColor =
                            albedo * diffuseIntensity * (1.0f - mat.metallic * 0.9f) +
                            specColor + mat.emissive;

                        writePixel(px, py, finalColor);
                    }
                }
            }
        }
    }

    return true;
}

bool MaterialThumbnailRenderer::RenderThumbnail(const AssetId& assetId,
                                                const std::filesystem::path& nativePath,
                                                const std::filesystem::path& outputPath,
                                                u32 width,
                                                u32 height) {
    (void)assetId;

    MaterialThumbnailData matData;
    if (!ParseMaterialFile(nativePath, matData)) {
        LUMA_LOG_ERROR("MaterialThumbnailRenderer", "Failed to parse material file: {}", nativePath.string());
        return false;
    }

    LumaMeshData sphere;
    GenerateSphereMesh(sphere, 32, 16);

    std::vector<u8> pixels;
    if (!RenderMaterialSphere(sphere, matData, pixels, width, height)) {
        LUMA_LOG_ERROR("MaterialThumbnailRenderer", "Failed to render material preview sphere");
        return false;
    }

    int success = stbi_write_png(outputPath.string().c_str(),
                                  static_cast<int>(width),
                                  static_cast<int>(height),
                                  4,
                                  pixels.data(),
                                  static_cast<int>(width * 4));

    if (!success) {
        LUMA_LOG_ERROR("MaterialThumbnailRenderer", "Failed to save thumbnail: {}", outputPath.string());
        return false;
    }

    LUMA_LOG_INFO("MaterialThumbnailRenderer", "Generated material thumbnail: {}", outputPath.string());
    return true;
}

} // namespace Luma
