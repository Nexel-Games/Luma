#include "Luma/Scene/SceneSystems.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <tuple>
#include "Luma/Core/Log.h"
#include "Luma/Scene/SceneSerializer.h"

namespace Luma {

void SceneSystemRegistry::Entry::End(Scene& scene) {
    if (!std::erase(sessions, &scene)) return;
    try {
        system->OnPlayEnd(scene);
    } catch (const std::exception& e) {
        LUMA_LOG_ERROR("Scene", "System '{}' failed to stop: {}", id, e.what());
    } catch (...) {
        LUMA_LOG_ERROR("Scene", "System '{}' failed to stop with an unknown exception", id);
    }
}

RegistrationToken SceneSystemRegistry::Register(std::unique_ptr<ISceneSystem> system, i32 order,
                                               std::string* error) {
    auto reject = [&](std::string why) {
        if (error) *error = why;
        LUMA_LOG_ERROR("Scene", "{}", why);
        return RegistrationToken{};
    };
    if (!system) return reject("Missing scene system");
    const std::string id(system->Id());
    if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos)
        return reject("Invalid scene system id: " + id);
    for (const auto& item : m_entries)
        if (item->id == id) return reject("Duplicate scene system: " + id);
    auto entry = std::make_shared<Entry>(Entry{std::move(system), id, order, {}});
    m_entries.push_back(entry);
    std::sort(m_entries.begin(), m_entries.end(), [](const auto& a, const auto& b) {
        return std::tie(a->order, a->id) < std::tie(b->order, b->id);
    });
    return RegistrationToken(m_alive, [this, weak = std::weak_ptr(entry)] {
        if (auto active = weak.lock()) {
            std::erase(m_entries, active);
            while (!active->sessions.empty()) active->End(*active->sessions.back());
        }
    });
}

bool SceneSystemRegistry::Contains(std::string_view id) const {
    for (const auto& item : m_entries) {
        if (item->id == id) return true;
    }
    return false;
}

SceneSystemRegistry& SceneSystems() {
    static SceneSystemRegistry registry;
    return registry;
}

bool SimulationSettings::Valid() const {
    return std::isfinite(fixedTimeStep) && fixedTimeStep >= 0.0001 && fixedTimeStep <= 1.0 &&
           maxSubsteps >= 1 && maxSubsteps <= 128;
}

bool SceneRunner::Begin(const Scene& authoring, SimulationSettings settings, std::string* error) {
    if (m_scene || !settings.Valid()) {
        if (error) *error = m_scene ? "Play is already running" : "Invalid simulation settings";
        return false;
    }
    auto copy = std::make_unique<Scene>();
    if (!SceneSerializer::Clone(authoring, *copy, error)) return false;
    m_scene = std::move(copy);
    m_settings = settings;
    m_accumulator = 0;
    m_paused = false;
    m_systems.assign(m_registry.m_entries.begin(), m_registry.m_entries.end());
    for (const auto& weak : m_systems) {
        if (auto entry = weak.lock()) {
            entry->sessions.push_back(m_scene.get());
            std::string failure;
            try {
                entry->system->OnPlayBegin(*m_scene);
            } catch (const std::exception& e) {
                failure = "System '" + entry->id + "' failed to start: " + e.what();
            } catch (...) {
                failure = "System '" + entry->id + "' failed to start with an unknown exception";
            }
            if (!failure.empty()) {
                if (error) *error = failure;
                LUMA_LOG_ERROR("Scene", "{}", failure);
                End();
                return false;
            }
        }
    }
    return true;
}

void SceneRunner::End() {
    if (!m_scene) return;
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it)
        if (auto entry = it->lock()) entry->End(*m_scene);
    m_systems.clear();
    m_scene.reset();
    m_paused = false;
}

bool SceneRunner::RunUpdate(f64 seconds, bool fixed) {
    for (const auto& weak : m_systems) {
        if (auto entry = weak.lock()) {
            try {
                if (fixed) entry->system->FixedUpdate(*m_scene, seconds);
                else entry->system->Update(*m_scene, seconds);
            } catch (const std::exception& e) {
                LUMA_USER_ERROR("Scene", "Play paused: system '{}' failed: {}", entry->id, e.what());
                m_paused = true;
                return false;
            } catch (...) {
                LUMA_USER_ERROR("Scene", "Play paused: system '{}' threw an unknown exception", entry->id);
                m_paused = true;
                return false;
            }
        }
    }
    return true;
}

void SceneRunner::Tick(f64 seconds) {
    if (!m_scene || m_paused || !std::isfinite(seconds) || seconds < 0) return;
    const f64 step = m_settings.fixedTimeStep;
    m_accumulator += seconds;
    for (i32 i = 0; i < m_settings.maxSubsteps && m_accumulator >= step; ++i) {
        if (!RunUpdate(step, true)) return;
        m_accumulator -= step;
    }
    if (m_accumulator >= step) m_accumulator = std::fmod(m_accumulator, step);
    RunUpdate(seconds, false);
}

void SceneRunner::SetPaused(bool paused) {
    if (m_scene) m_paused = paused;
}

void SceneRunner::Step() {
    if (m_scene && m_paused) {
        if (!RunUpdate(m_settings.fixedTimeStep, true)) return;
        RunUpdate(m_settings.fixedTimeStep, false);
    }
}

}  // namespace Luma
