// ImportFixtureGen <in.gltf> <out.fbx>
// Loads a Y-up, metre glTF and exports an FBX in centimetres, Z-up:
// every root node is scaled by 100 and rotated +90 degrees about X, and the
// scene metadata carries the FBX global-settings axis and unit keys.

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr float kHalfPi = 1.5707963267948966f;

void ConvertRootToCentimetreZUp(aiNode* node) {
    aiMatrix4x4 scale;
    aiMatrix4x4::Scaling(aiVector3D(100.f, 100.f, 100.f), scale);
    aiMatrix4x4 rot;
    aiMatrix4x4::RotationX(kHalfPi, rot);
    // Column vectors: scale the existing transform, then rotate Y-up to Z-up.
    node->mTransformation = (rot * scale) * node->mTransformation;
}

void SetFbxGlobalSettings(aiScene* scene) {
    if (scene->mMetaData == nullptr) {
        scene->mMetaData = new aiMetadata();
    }
    auto& meta = *scene->mMetaData;
    const auto putInt = [&](const char* key, int32_t value) {
        if (!meta.Set(key, value)) {
            meta.Add(key, value);
        }
    };
    putInt("UpAxis", 2);
    putInt("UpAxisSign", 1);
    putInt("FrontAxis", 1);
    putInt("FrontAxisSign", -1);
    putInt("CoordAxis", 0);
    putInt("CoordAxisSign", 1);
    const double unitCm = 1.0;
    if (!meta.Set("UnitScaleFactor", unitCm)) {
        meta.Add("UnitScaleFactor", unitCm);
    }
}

void PrintReload(const char* path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 0);
    if (scene == nullptr) {
        std::cerr << "reload failed: " << importer.GetErrorString() << "\n";
        return;
    }
    std::cout << "meshes=" << scene->mNumMeshes << "\n";
    std::cout << "materials=";
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        aiString name;
        scene->mMaterials[i]->Get(AI_MATKEY_NAME, name);
        if (i != 0) {
            std::cout << ",";
        }
        std::cout << name.C_Str();
    }
    std::cout << "\n";
    const auto walk = [](auto&& self, const aiNode* node, int depth) -> void {
        std::cout << "node " << node->mName.C_Str() << " depth=" << depth << "\n";
        for (unsigned i = 0; i < node->mNumChildren; ++i) {
            self(self, node->mChildren[i], depth + 1);
        }
    };
    if (scene->mRootNode != nullptr) {
        walk(walk, scene->mRootNode, 0);
    }
    if (scene->mMetaData == nullptr) {
        std::cout << "metadata=<none>\n";
        return;
    }
    std::cout << "metadata_count=" << scene->mMetaData->mNumProperties << "\n";
    for (unsigned i = 0; i < scene->mMetaData->mNumProperties; ++i) {
        const aiString* key = nullptr;
        const aiMetadataEntry* entry = nullptr;
        if (!scene->mMetaData->Get(i, key, entry) || key == nullptr || entry == nullptr) {
            continue;
        }
        std::cout << "meta " << key->C_Str() << " type=" << static_cast<int>(entry->mType);
        if (entry->mType == AI_INT32 && entry->mData != nullptr) {
            std::cout << " value=" << *static_cast<int32_t*>(entry->mData);
        } else if (entry->mType == AI_DOUBLE && entry->mData != nullptr) {
            std::cout << " value=" << *static_cast<double*>(entry->mData);
        } else if (entry->mType == AI_FLOAT && entry->mData != nullptr) {
            std::cout << " value=" << *static_cast<float*>(entry->mData);
        }
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: ImportFixtureGen <in.gltf> <out.fbx>\n";
        return 2;
    }
    const std::string in = argv[1];
    const std::string out = argv[2];

    Assimp::Importer importer;
    if (importer.ReadFile(in, 0) == nullptr) {
        std::cerr << "import failed: " << importer.GetErrorString() << "\n";
        return 1;
    }
    aiScene* scene = importer.GetOrphanedScene();
    if (scene == nullptr || scene->mRootNode == nullptr) {
        std::cerr << "import produced no scene\n";
        delete scene;
        return 1;
    }

    if (scene->mRootNode->mNumChildren == 0) {
        ConvertRootToCentimetreZUp(scene->mRootNode);
    } else {
        for (unsigned i = 0; i < scene->mRootNode->mNumChildren; ++i) {
            ConvertRootToCentimetreZUp(scene->mRootNode->mChildren[i]);
        }
    }
    SetFbxGlobalSettings(scene);

    Assimp::Exporter exporter;
    const aiReturn result = exporter.Export(scene, "fbx", out);
    delete scene;
    if (result != aiReturn_SUCCESS) {
        std::cerr << "export failed: " << exporter.GetErrorString() << "\n";
        return 1;
    }
    std::cout << "wrote " << out << "\n";
    PrintReload(out.c_str());
    return 0;
}
