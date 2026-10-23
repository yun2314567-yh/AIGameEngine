# Agent workflow and Lua

The editor provides graphical authoring and a line command protocol.
Agents can create scenes, attach components, position assets, configure
rendering, save prefabs and export without interacting with the GUI.

## Commands

Run `TinyEngineEditor --commands commands.txt --asset-root assets`, or pipe
commands with `--commands -`. `--scene <path>` optionally loads an initial
scene. Command mode opens no window. Strings use C++ quoted-string escaping;
booleans are 0 or 1. Failures report line numbers and cause nonzero exit status;
later commands still run. A file is not a transaction across all lines.

```text
create "Player"
rename 1 "Hero"
transform 1 0 1 0 0 0 0 1 1 1 1
add 1 camera 60 0.1 1000 1
add 1 mesh-renderer "meshes/showcase.gltf" "materials/player.pbr" 1 1 1
add 1 rigid-body dynamic 1 0 0 0 1
add 1 box-collider 0.5 0.5 0.5 0.15 0.6
add 1 script "scripts/showcase.lua"
add 1 audio-source "audio/player-loop.wav" 0.75 1 1 1
environment "environments/studio_small_03_1k.hdr" 1 0 64 16 6
render-settings 1 1 1 0 2048 16 0.5 0.025 1 0 32 0.001
save-prefab 1 "out/player.tiny"
spawn-prefab "out/player.tiny"
save "out/game.tiny"
inspect
```

`create` reports its assigned ID.

| Command | Arguments |
| --- | --- |
| create / rename | "name" / id "name" |
| destroy | id |
| transform | id tx ty tz qx qy qz qw sx sy sz |
| add / remove | id component [component fields for add] |
| save / load | "scenePath" |
| save-prefab | id "outputPath" |
| spawn-prefab | "prefabPath" |
| import-gltf | "sourcePath" (validates/decodes; does not copy) |
| export-runtime | "runtimeBinary" "outputDirectory" ["sceneFileName"] |
| inspect | no arguments |
| environment | "HDRPath" intensity rotation prefilterResolution irradianceResolution mipLevels |
| render-settings | shadows ssao ibl compute resolution pcf radius ssaoBias exposure mapper samples shadowBias |

Components: camera, script, rigid-body, box-collider, mesh-renderer, light,
audio-source. Fields follow [SCENE_FORMAT.md](SCENE_FORMAT.md), except body types
use static/dynamic/kinematic and lights use directional/point/spot.
Remove additionally accepts tag and transform.

Use the C++ `EditorAutomation` API for structured results and export options
such as explicit shader directories and private runtime dependencies.
The text export command uses default shader discovery and no explicit libraries.

## Lua behavior

Enable Lua at build time. Modules are UTF-8 files under AssetRoot (1 MiB limit).
Each returns a table:

```lua
return {
    on_attach = function(entity)
        local x, y, z = engine.get_translation(entity)
        engine.set_translation(entity, x, y + 1, z)
    end,
    update = function(entity, delta_seconds)
        local vx, vy, vz = engine.get_velocity(entity)
        engine.set_velocity(entity, vx, vy, vz)
    end,
}
```

The entire engine API:

| Function | Result / behavior |
| --- | --- |
| create_entity(name) | New entity ID |
| destroy_entity(id) | Queues deletion; boolean success |
| get_translation(id) | x,y,z; requires Transform |
| set_translation(id,x,y,z) | Sets finite position; requires Transform |
| get_velocity(id) | x,y,z; requires RigidBody |
| set_velocity(id,x,y,z) | Sets finite velocity; requires RigidBody |
| spawn_prefab(serializedScene) | First spawned ID, or nil on invalid input |
| key_down(name) | Boolean; held during the current input frame |
| key_pressed(name) | Boolean; transitioned from released to held |
| key_released(name) | Boolean; transitioned from held to released |

Keyboard names are case sensitive: `A` through `Z`, `0` through `9`, `Left`,
`Right`, `Up`, `Down`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`,
`LeftShift`, `RightShift`, `LeftControl` and `RightControl`. Unknown names raise
a Lua argument error. The runtime captures the keyboard once per frame; losing
window focus releases held keys. Editor play captures keys while ImGui does
not request keyboard input. The showcase uses arrows to move and Space to jump.
Headless applications may inject keys through `Engine::Input()`: call
`BeginFrame()` once, then `SetKey()` before `Engine::update()`. Headless execution
without injection reports all keys released. Engine services use the owning thread.

Creation/destruction is safe during iteration. New script attachments run on a
later update. Stale IDs are rejected. Replacing a scene resets attachment
lifecycle, so restarting play invokes on_attach again.
Errors are reported by runtime and stop editor play.
Lua opens standard libraries and is intended for trusted game code, not
untrusted scripts.

The Feature Showcase exercises every function, both lifecycle callbacks,
prefab spawning and deferred deletion. Native callbacks support CPU-only tests.

## Automated game-building loop

Write Lua/assets into the game's asset root. Author/save through editor commands.
Validate updates with
`TinyEngineRuntime --scene out/game.tiny --asset-root assets --headless --frames 120`.
Use graphical execution and GPU captures for visual acceptance. Export through
`export-runtime`, then launch the packaged executable from another directory.

The protocol authors scenes/packages; it does not inject commands into an
already running GUI. GUI play/stop restores the authoring snapshot. Agent jobs
can validate the same scene in bounded runtime processes.
