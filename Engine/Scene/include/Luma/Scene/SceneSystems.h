#pragma once

#include <string_view>
#include "Luma/Core/Registration.h"
#include "Luma/Scene/Scene.h"

namespace Luma {

class ISceneSystem {
public:
    virtual ~ISceneSystem() = default;
    virtual std::string_view Id() const = 0;
    virtual void OnPlayBegin(Scene&) {}
    virtual void FixedUpdate(Scene&, f64) {}
    virtual void Update(Scene&, f64) {}
    virtual void OnPlayEnd(Scene&) {}
};

// Registrations and simulation callbacks run on the main thread. A new registration
// joins the next play session. Removing one ends its active sessions immediately.
class SceneSystemRegistry {
public:
    SceneSystemRegistry() = default;
    SceneSystemRegistry(const SceneSystemRegistry&) = delete;
    RegistrationToken Register(std::unique_ptr<ISceneSystem> system, i32 order, std::string* error = nullptr);
private:
    friend class SceneRunner;
    struct Entry {
        std::unique_ptr<ISceneSystem> system;
        std::string id;
        i32 order;
        std::vector<Scene*> sessions;
        void End(Scene& scene);
    };
    std::vector<std::shared_ptr<Entry>> m_entries;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

SceneSystemRegistry& SceneSystems();

struct SimulationSettings {
    f64 fixedTimeStep = 1.0 / 60.0;
    i32 maxSubsteps = 8;
    bool Valid() const;
};

class SceneRunner {
public:
    explicit SceneRunner(SceneSystemRegistry& registry = SceneSystems()) : m_registry(registry) {}
    ~SceneRunner() { End(); }
    SceneRunner(const SceneRunner&) = delete;
    bool Begin(const Scene& authoring, SimulationSettings settings = {}, std::string* error = nullptr);
    void End();
    void Tick(f64 seconds);
    void SetPaused(bool paused);
    bool Paused() const { return m_paused; }
    void Step();
    Scene* PlayScene() const { return m_scene.get(); }
private:
    bool RunUpdate(f64 seconds, bool fixed);
    SceneSystemRegistry& m_registry;
    std::unique_ptr<Scene> m_scene;
    std::vector<std::weak_ptr<SceneSystemRegistry::Entry>> m_systems;
    SimulationSettings m_settings;
    f64 m_accumulator = 0;
    bool m_paused = false;
};

}  // namespace Luma
