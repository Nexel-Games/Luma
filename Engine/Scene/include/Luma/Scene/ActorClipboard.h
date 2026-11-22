#pragma once

#include <vector>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"

namespace Luma {

// One actor and its descendants, detached from the live scene. Asset paths
// inside components are copied by value; there are no entity pointers.
struct ActorClipNode {
    std::string name;
    TransformComponent transform{};
    bool hasMesh = false;
    MeshRendererComponent mesh{};
    bool hasLight = false;
    LightComponent light{};
    bool hasCamera = false;
    CameraComponent camera{};
    bool hasPost = false;
    PostProcessComponent post{};
    std::vector<ActorClipNode> children;
};

// Editor actor clipboard. A cut remembers the source entities but does not
// destroy them until the caller pastes successfully and then deletes them.
class ActorClipboard {
public:
    bool Empty() const { return m_roots.empty(); }
    bool IsCut() const { return m_cut; }
    const std::vector<Entity>& CutSources() const { return m_cutSources; }

    // False when every root is invalid or is an Environment (one per scene).
    bool Copy(const Scene& scene, const std::vector<Entity>& roots);
    bool Cut(const Scene& scene, const std::vector<Entity>& roots);

    // New entities under `parent` (kNullEntity = scene root). Names skip
    // `ignore`, so a cut can reuse the source name after those actors go away.
    // Does not destroy cut sources.
    std::vector<Entity> Paste(Scene& scene, Entity parent, const std::vector<Entity>& ignore) const;

    void Clear();

private:
    std::vector<ActorClipNode> m_roots;
    std::vector<Entity> m_cutSources;
    bool m_cut = false;
};

}  // namespace Luma
