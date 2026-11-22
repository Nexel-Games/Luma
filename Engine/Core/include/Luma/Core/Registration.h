#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Luma/Core/Types.h"

// RAII handle for anything registered into a runtime registry (components, editor
// panels, menu entries, settings pages, asset types, scene systems, ...).
//
// Every registry returns a RegistrationToken from Register(). Destroying the token,
// or calling Reset(), unregisters the entry, so a plugin that shuts down (or fails
// to start) leaves no dangling entry behind. A token observes its registry through
// a weak lifetime anchor: if the registry is destroyed first, Reset() is a no-op.
//
// Registries are main-thread only; so are their tokens.

namespace Luma {

class RegistrationToken {
public:
    RegistrationToken() = default;
    // `owner` is the registry's lifetime anchor; `unregister` removes the entry.
    RegistrationToken(std::weak_ptr<void> owner, std::function<void()> unregister)
        : m_owner(std::move(owner)), m_unregister(std::move(unregister)) {}
    ~RegistrationToken() { Reset(); }

    RegistrationToken(const RegistrationToken&) = delete;
    RegistrationToken& operator=(const RegistrationToken&) = delete;
    RegistrationToken(RegistrationToken&& other) noexcept
        : m_owner(std::move(other.m_owner)), m_unregister(std::move(other.m_unregister)) {
        other.m_unregister = nullptr;
        other.m_owner.reset();
    }
    RegistrationToken& operator=(RegistrationToken&& other) noexcept {
        if (this != &other) {
            Reset();
            m_owner = std::move(other.m_owner);
            m_unregister = std::move(other.m_unregister);
            other.m_unregister = nullptr;
            other.m_owner.reset();
        }
        return *this;
    }

    // Unregisters the entry (once). Safe to call repeatedly and after the
    // registry has been destroyed.
    void Reset() {
        std::function<void()> fn = std::move(m_unregister);
        m_unregister = nullptr;
        if (fn && !m_owner.expired()) fn();
        m_owner.reset();
    }

    // True while the registration is live (registered and the registry exists).
    bool IsActive() const { return m_unregister != nullptr && !m_owner.expired(); }
    explicit operator bool() const { return IsActive(); }

private:
    std::weak_ptr<void> m_owner;
    std::function<void()> m_unregister;
};

// A group of registrations released together, in reverse order of registration
// (later registrations may depend on earlier ones). Used for the built-ins and by
// the plugin framework to track everything one plugin registered.
class RegistrationSet {
public:
    RegistrationSet() = default;
    ~RegistrationSet() { Reset(); }
    RegistrationSet(const RegistrationSet&) = delete;
    RegistrationSet& operator=(const RegistrationSet&) = delete;
    RegistrationSet(RegistrationSet&&) noexcept = default;
    RegistrationSet& operator=(RegistrationSet&& other) noexcept {
        if (this != &other) {
            Reset();
            m_tokens = std::move(other.m_tokens);
        }
        return *this;
    }

    // Takes ownership of an active token; inactive (failed) tokens are dropped.
    void Add(RegistrationToken token) {
        if (token.IsActive()) m_tokens.push_back(std::move(token));
    }

    void Reset() {
        while (!m_tokens.empty()) {
            m_tokens.back().Reset();
            m_tokens.pop_back();
        }
    }

    usize Size() const { return m_tokens.size(); }
    bool Empty() const { return m_tokens.empty(); }

private:
    std::vector<RegistrationToken> m_tokens;
};

}  // namespace Luma
