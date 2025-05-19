// main.cpp
#include <iostream>
#include "FLECS/flecs.h"
#include <sol/sol.hpp>
#include <unordered_map>
#include <string>
#include <functional>

// Component definition
struct Transform {
    float x = 0;
    float y = 0;

    Transform() = default;
    Transform(float x_, float y_) : x(x_), y(y_) {}
};

struct Velocity {
    float vx = 0;
    float vy = 0;

    Velocity() = default;
    Velocity(float vx_, float vy_) : vx(vx_), vy(vy_) {}
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
template<typename T>
void registerComponent(sol::state& lua, flecs::world& world, const std::string& name) {
    world.component<T>();

    luaComponentRegistry[name] = {
        // Add
        [&world](flecs::entity e, sol::object obj) {
            auto t = obj.as<T>();
            e.set<T>(t);
        },
        // Get
        [&lua](flecs::entity e) -> sol::object {
            if (e.has<T>()) {
                return sol::make_object(lua, *e.get<T>());
            }
            return sol::nil;
        },
        // Remove
        [](flecs::entity e) {
            e.remove<T>();
        }
    };
}

void CreateUserTypeTransformComponent(sol::state& lua)
{
    lua.new_usertype<Transform>(
        "Transform",
        sol::call_constructor,
        sol::factories(
            [](float x, float y) {
                return Transform(
                    x,
                    y
                );
            }
        ),
        "x", &Transform::x,
        "y", &Transform::y
    );
}

void CreateUserTypeVelocityComponent(sol::state& lua)
{
    lua.new_usertype<Velocity>(
        "Velocity",
        sol::call_constructor,
        sol::factories(
            [](float x, float y) {
                return Velocity(
                    x,
                    y
                );
            }
        ),
        "vx", &Velocity::vx,
        "vy", &Velocity::vy
    );
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

//#######################################################################################################
//#######################TRYING SOME DYNAMIC RUNTIME REFLECTION STUFF####################################
//#######################################################################################################

// Dynamic Component Wrapper
struct DynamicComponent
{
    std::unordered_map<std::string, sol::object> fields;
};

// Register Dynamic Component
void registerDynamicComponent(flecs::world& world, sol::state& lua)
{
    world.component<DynamicComponent>();

    luaComponentRegistry["DynamicComponent"] = {
        // ADD
        [&world](flecs::entity e, sol::object obj)
        {
            sol::table tbl = obj.as<sol::table>();
            DynamicComponent dc;
            for (const auto& kv : tbl)
            {
                std::string key = kv.first.as<std::string>();
                dc.fields[key] = kv.second;
            }
            e.set<DynamicComponent>(dc);
        },

        // GET
        [&lua](flecs::entity e) -> sol::object 
        {
            if (!e.has<DynamicComponent>())
            {
                return sol::nil;
            }
            sol::table tbl = lua.create_table();
            const auto& dc = *e.get<DynamicComponent>();
            for (const auto& [key, value] : dc.fields)
            {
                tbl[key] = value;
            }
            return tbl;
        },

        // REMOVE
        [](flecs::entity e)
        {
            e.remove<DynamicComponent>();
        }
    };
}


int main() {
    flecs::world ecs;
    sol::state lua;
    lua.open_libraries(sol::lib::base);

    // Register components and bindings
    // Register all components with one line each
    registerComponent<Transform>(lua, ecs, "Transform");
    registerComponent<Velocity>(lua, ecs, "Velocity");
    registerDynamicComponent(ecs, lua);

    bindEntity(lua);

    // Register of all the components
    CreateUserTypeTransformComponent(lua);
    CreateUserTypeVelocityComponent(lua);

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

        local n = 0
        while n < 20 do
            t.x = t.x + 1
            t.y = t.y + 1
            print("Transform X:", t.x, "Y:", t.y)
            n = n + 1
        end

        -- Remove Transform
        player:removeComponent("Transform")
        print("Removed Transform.")

        player:addComponent("Velocity", Velocity(1.5, -0.3))
        local v = player:getComponent("Velocity")
        print("VX:", v.vx, "VY:", v.vy)
        
        
        -- Add a dynamic component at runtime!
        player:addComponent("DynamicComponent", {
            health = 100,
            name = "Hero",
            isAlive = true,
            speed = 5.75
        })

        local dc = player:getComponent("DynamicComponent")
        print("Name:", dc.name)
        print("Health:", dc.health)
        print("Speed:", dc.speed)
        print("Alive?", dc.isAlive)

        -- Modify dynamic fields
        dc.health = dc.health - 25
        print("Updated Health:", dc.health)
        
        player:removeComponent("DynamicComponent")
    )");

    return 0;
}
