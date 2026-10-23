-- TinyEngine scripting API showcase. The runtime loads this module when the
-- Player entity is attached to the FeatureShowcase scene.
local probes = {}
local prefab = [[TINYENGINE_SCENE 1
"Showcase Prefab"
ENTITY 1 "Spawned Showcase Probe" T 4 2 0 0 0 0 1 1 1 1 R 1 1 0 0 0 0
]]

return {
    on_attach = function(entity)
        local x, y, z = engine.get_translation(entity)
        engine.set_translation(entity, x, y, z)
        local created = engine.create_entity("Created Showcase Probe")
        engine.set_translation(created, x + 2, y, z)
        local spawned = assert(engine.spawn_prefab(prefab), "prefab spawning failed")
        engine.set_velocity(spawned, 0.25, 0, 0)
        probes[entity] = { created, spawned }
    end,

    update = function(entity, delta_seconds)
        local vx, vy, vz = engine.get_velocity(entity)
        if engine.key_down("Left") then vx = -2 end
        if engine.key_down("Right") then vx = 2 end
        if engine.key_pressed("Space") then vy = 5 end
        if engine.key_released("Left") or engine.key_released("Right") then vx = 0 end
        engine.set_velocity(entity, vx, vy, vz)
        local pending = probes[entity]
        if pending then
            for _, probe in ipairs(pending) do
                local x, y, z = engine.get_translation(probe)
                engine.set_translation(probe, x + delta_seconds, y, z)
                assert(engine.destroy_entity(probe), "entity destruction failed")
            end
            probes[entity] = nil
        end
    end,
}
