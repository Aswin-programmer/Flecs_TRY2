// main.cpp
#include <iostream>
#include "FLECS/flecs.h"
#include <sol/sol.hpp>
#include <unordered_map>

// Component definition
struct Transform {
    float x = 0.0f;
    float y = 0.0f;

    Transform() = default;
    Transform(float x_, float y_) : x(x_), y(y_) {}
};


// Global Lua component registry
using LuaComponentAdder = std::function<void(flecs::entity, sol::object)>;
using LuaComponentGetter = std::function<sol::object(flecs::entity)>;
using LuaComponentRemover = std::function<void(flecs::entity)>;

struct LuaComponentBinding {
    LuaComponentAdder add;
    LuaComponentGetter get;
    LuaComponentRemover remove;
};

std::unordered_map<std::string, LuaComponentBinding> luaComponentRegistry;

// Helper function to register components for Lua
void registerTransform(sol::state& lua, flecs::world& world) {
    world.component<Transform>();

    // Expose Transform to Lua
    lua.new_usertype<Transform>("Transform",
        sol::call_constructor, sol::constructors<Transform(), Transform(float, float)>(),
        "x", &Transform::x,
        "y", &Transform::y
    );

    luaComponentRegistry["Transform"] = {
        // Add
        [&world](flecs::entity e, sol::object obj) {
            auto t = obj.as<Transform>();
            e.set<Transform>(t);
        },
        // Get
        [&world, &lua](flecs::entity e) -> sol::object {
            if (e.has<Transform>()) {
                return sol::make_object(lua, *e.get<Transform>());
            }
            return sol::nil;
        },
        // Remove
        [&world](flecs::entity e) {
            e.remove<Transform>();
        }
    };
}

// Binding flecs::entity for Lua
void bindEntity(sol::state& lua) {
    lua.new_usertype<flecs::entity>("Entity",
        "addComponent", [](flecs::entity self, const std::string& name, sol::object component) {
            auto it = luaComponentRegistry.find(name);
            if (it != luaComponentRegistry.end()) {
                it->second.add(self, component);
            }
        },
        "getComponent", [](flecs::entity self, const std::string& name) -> sol::object {
            auto it = luaComponentRegistry.find(name);
            if (it != luaComponentRegistry.end()) {
                return it->second.get(self);
            }
            return sol::nil;
        },
        "removeComponent", [](flecs::entity self, const std::string& name) {
            auto it = luaComponentRegistry.find(name);
            if (it != luaComponentRegistry.end()) {
                it->second.remove(self);
            }
        },
        "id", &flecs::entity::id
    );
}

int main() {
    flecs::world ecs;
    sol::state lua;
    lua.open_libraries(sol::lib::base);

    // Register components and bindings
    registerTransform(lua, ecs);
    bindEntity(lua);

    // Create entity and expose to Lua
    flecs::entity player = ecs.entity("Player");
    lua["player"] = player;

    // Run Lua script
    lua.script(R"(
        print("Entity ID:", player:id())
        
        -- Add Transform
        player:addComponent("Transform", Transform(5, 10))
        
        -- Get and update Transform
        local t = player:getComponent("Transform")
        t.x = 42
        t.y = 128
        print("Transform X:", t.x, "Y:", t.y)

        -- Remove Transform
        player:removeComponent("Transform")
        print("Removed Transform.")
    )");

    return 0;
}
