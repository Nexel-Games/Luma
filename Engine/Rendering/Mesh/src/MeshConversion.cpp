#include "Luma/Mesh/MeshConversion.h"

#include "Luma/Mesh/Tangents.h"

namespace Luma {

MeshData ConvertLumaMesh(const LumaMeshData& source) {
    MeshData out;
    out.vertices.reserve(source.vertices.size());
    for (const LumaVertex& v : source.vertices) {
        MeshVertex mv;
        mv.position = v.position;
        mv.normal = v.normal;
        mv.uv0 = v.texCoord;
        out.vertices.push_back(mv);
    }
    out.indices = source.indices;
    for (const LumaSubmesh& sm : source.submeshes) {
        out.sections.push_back({sm.indexOffset, sm.indexCount, sm.materialName});
    }
    if (out.sections.empty()) {
        out.sections.push_back({0, static_cast<u32>(out.indices.size()), {}});
    }
    ComputeTangents(out);
    return out;
}

}  // namespace Luma
