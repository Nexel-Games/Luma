#include "Luma/AssetImport/ImportScene.h"

namespace Luma::Import {
namespace {

NodeId Remap(NodeId id, NodeId base) {
    return id == kNone ? kNone : id + base;
}

void RemapNode(ImportNode& node, NodeId base) {
    node.parent = Remap(node.parent, base);
    for (NodeId& material : node.meshMaterials) {
        material = Remap(material, base);
    }
    TextureRef* refs[] = {
        &node.material.baseColorTex,        &node.material.normalTex,
        &node.material.metallicRoughnessTex, &node.material.occlusionTex,
        &node.material.emissiveTex,         &node.material.opacityTex,
    };
    for (TextureRef* ref : refs) {
        ref->texture = Remap(ref->texture, base);
    }
}

}  // namespace

NodeId ImportScene::AddNode(ImportNode n) {
    const NodeId id = static_cast<NodeId>(nodes.size());
    nodes.push_back(std::move(n));
    return id;
}

std::vector<NodeId> ImportScene::Children(NodeId file) const {
    std::vector<NodeId> ids;
    for (NodeId i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent == file) {
            ids.push_back(i);
        }
    }
    return ids;
}

std::vector<NodeId> ImportScene::OfKind(NodeKind k) const {
    std::vector<NodeId> ids;
    for (NodeId i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind == k) {
            ids.push_back(i);
        }
    }
    return ids;
}

void ImportScene::Append(const ImportScene& other) {
    const NodeId base = static_cast<NodeId>(nodes.size());
    for (const ImportNode& src : other.nodes) {
        ImportNode copy = src;
        RemapNode(copy, base);
        nodes.push_back(std::move(copy));
    }
}

}  // namespace Luma::Import
