// main.cpp
#include <iostream>
#include "FLECS/flecs.h"
#include <sol/sol.hpp>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>

#include "TransformTester.h"

struct Transform {

    std::shared_ptr<TransformTester> transformTester{ nullptr };

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


//Original Error
//The TransformTester destructor was called at program exit rather than when removing the component because :
//
//Component Storage : When you added the Transform component to an entity, Flecs stored a copy of the Transform struct in its internal storage.
//
//Shared Pointer Copies : The Lua getComponent function was creating a copy of the Transform struct (and its shared_ptr).This increased the reference count of the shared_ptr to 2 (one in Flecs storage, one in Lua).
//
//Premature Retention : When you removed the component from Flecs, the entity's copy was destroyed (reference count decreased to 1), but Lua still held its copy, keeping the TransformTester alive.
//
//The Fix
//The key change was modifying how components are retrieved :
//
//Before(Problematic) :
//
//    cpp
//    // Returned a COPY of the component
//    return sol::make_object(lua, *e.get<T>());
//After(Fixed) :
//
//    cpp
//    // Returned a POINTER to the component's data
//    T* ptr = e.get_mut<T>();
//return sol::make_object(lua, ptr);
//Why This Worked :
//
//By returning a pointer to the component stored in Flecs(instead of a copy), Lua now references the exact same shared_ptr inside the Flecs component.
//
//When the component is removed :
//
//Flecs destroys its storage of Transform, dropping the shared_ptr reference count to 0.
//
//This immediately triggers the TransformTester destructor(no lingering copies in Lua).
//



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
        // Get (returns a pointer to the component)
        [&lua](flecs::entity e) -> sol::object {
            if (e.has<T>()) {
                T* ptr = e.get_mut<T>(); //<= Explanation for this line is explained above.
                return sol::make_object(lua, ptr); //<= Explanation for this line is explained above.
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
                return Transform{
                    .transformTester = std::make_shared<TransformTester>(x, y)
                };
            }
        ),
        "AddTwoNumber", [](Transform& self) {
            return self.transformTester->AddTwoNumbers();
        }
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
        "removeComponent", [&lua](flecs::entity self, const std::string& name) {
            auto it = luaComponentRegistry.find(name);
            if (it != luaComponentRegistry.end()) {
                it->second.remove(self);
                // Force Lua garbage collection to clean up any references
                lua.collect_garbage();
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
        print(t:AddTwoNumber())

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
