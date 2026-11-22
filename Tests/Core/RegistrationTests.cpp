#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

#include "Luma/Core/Registration.h"

using Luma::RegistrationSet;
using Luma::RegistrationToken;

namespace {

// A registry stand-in: counts unregistrations and owns a lifetime anchor.
struct FakeRegistry {
    std::shared_ptr<void> alive = std::make_shared<int>(0);
    int removed = 0;
    std::vector<int> order;
    RegistrationToken Add(int id) {
        return RegistrationToken(alive, [this, id] {
            ++removed;
            order.push_back(id);
        });
    }
};

}  // namespace

TEST_CASE("RegistrationToken unregisters on Reset and only once", "[core][registration]") {
    FakeRegistry reg;
    RegistrationToken t = reg.Add(1);
    REQUIRE(t.IsActive());
    REQUIRE(static_cast<bool>(t));
    t.Reset();
    REQUIRE(reg.removed == 1);
    REQUIRE_FALSE(t.IsActive());
    t.Reset();
    REQUIRE(reg.removed == 1);
}

TEST_CASE("RegistrationToken unregisters on destruction", "[core][registration]") {
    FakeRegistry reg;
    { RegistrationToken t = reg.Add(1); }
    REQUIRE(reg.removed == 1);
}

TEST_CASE("RegistrationToken is move-only and moves ownership", "[core][registration]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<RegistrationToken>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<RegistrationToken>);
    FakeRegistry reg;
    RegistrationToken a = reg.Add(1);
    RegistrationToken b = std::move(a);
    REQUIRE_FALSE(a.IsActive());  // NOLINT(bugprone-use-after-move)
    REQUIRE(b.IsActive());
    REQUIRE(reg.removed == 0);

    // Move-assigning over an active token releases the old registration first.
    RegistrationToken c = reg.Add(2);
    c = std::move(b);
    REQUIRE(reg.removed == 1);
    REQUIRE(reg.order == std::vector<int>{2});
    c.Reset();
    REQUIRE(reg.order == std::vector<int>{2, 1});
}

TEST_CASE("RegistrationToken does nothing once its registry is gone", "[core][registration]") {
    int calls = 0;
    RegistrationToken t;
    {
        auto alive = std::make_shared<int>(0);
        t = RegistrationToken(alive, [&calls] { ++calls; });
        REQUIRE(t.IsActive());
    }
    REQUIRE_FALSE(t.IsActive());
    t.Reset();
    REQUIRE(calls == 0);
}

TEST_CASE("A default token is inactive", "[core][registration]") {
    RegistrationToken t;
    REQUIRE_FALSE(t.IsActive());
    t.Reset();
}

TEST_CASE("RegistrationSet releases in reverse registration order", "[core][registration]") {
    FakeRegistry reg;
    RegistrationSet set;
    set.Add(reg.Add(1));
    set.Add(reg.Add(2));
    set.Add(reg.Add(3));
    set.Add(RegistrationToken{});  // inactive tokens are ignored
    REQUIRE(set.Size() == 3);
    set.Reset();
    REQUIRE(reg.order == std::vector<int>{3, 2, 1});
    REQUIRE(set.Size() == 0);
}
