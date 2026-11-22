#include "Luma/Editor/SettingsPageRegistry.h"

#include <algorithm>

#include "Luma/Core/Log.h"

namespace Luma::Editor {

bool IsValidConfigFileName(std::string_view name) {
    if (name.size() <= 5 || name.substr(name.size() - 5) != ".json") return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return name.find("..") == std::string_view::npos && name.front() != '.';
}

SettingsPageRegistry::SettingsPageRegistry() : m_alive(std::make_shared<int>(0)) {}

RegistrationToken SettingsPageRegistry::Register(SettingsPageDesc page, std::string* error) {
    auto fail = [error](std::string message) {
        LUMA_LOG_ERROR("Editor", "{}", message);
        if (error) *error = std::move(message);
        return RegistrationToken{};
    };
    if (page.id.empty()) return fail("settings page rejected: the id is empty");
    const std::string who = "settings page '" + page.id + "'";
    if (page.owner.empty()) return fail(who + " rejected: owner is empty");
    if (page.category.empty() || page.title.empty()) {
        return fail(who + " rejected: category and title are required");
    }
    if (!IsValidConfigFileName(page.configFile)) {
        return fail(who + " rejected: configFile '" + page.configFile +
                    "' must be a plain .json file name under Config/");
    }
    if (!page.load || !page.save || !page.draw) {
        return fail(who + " rejected: load, save and draw callbacks are required");
    }
    if (const SettingsPageDesc* existing = Find(page.id)) {
        return fail(who + " is already registered (by '" + existing->owner + "')");
    }
    const u64 handle = m_nextHandle++;
    m_entries.push_back({handle, std::make_unique<SettingsPageDesc>(std::move(page))});
    ++m_revision;
    return RegistrationToken(m_alive, [this, handle] {
        auto it = std::find_if(m_entries.begin(), m_entries.end(),
                               [handle](const Entry& e) { return e.handle == handle; });
        if (it == m_entries.end()) return;
        m_entries.erase(it);
        ++m_revision;
    });
}

const SettingsPageDesc* SettingsPageRegistry::Find(std::string_view id) const {
    for (const Entry& e : m_entries) {
        if (e.desc->id == id) return e.desc.get();
    }
    return nullptr;
}

std::vector<const SettingsPageDesc*> SettingsPageRegistry::All() const {
    std::vector<const SettingsPageDesc*> out;
    for (const Entry& e : m_entries) out.push_back(e.desc.get());
    return out;
}

std::vector<std::string> SettingsPageRegistry::Categories() const {
    std::vector<std::string> out;
    for (const Entry& e : m_entries) {
        if (std::find(out.begin(), out.end(), e.desc->category) == out.end()) {
            out.push_back(e.desc->category);
        }
    }
    return out;
}

std::vector<const SettingsPageDesc*> SettingsPageRegistry::PagesIn(std::string_view category) const {
    std::vector<const SettingsPageDesc*> out;
    for (const Entry& e : m_entries) {
        if (e.desc->category == category) out.push_back(e.desc.get());
    }
    return out;
}

SettingsPageRegistry& SettingsPages() {
    static SettingsPageRegistry registry;
    return registry;
}

}  // namespace Luma::Editor
