#include "TinyEngine/Core.h"

#include <cmath>
#include <exception>
#include <iostream>

void runRendererTests();
#include <stdexcept>
#include <string>
#include <vector>

void runEngineIntegrationTests();

namespace {

int s_Failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++s_Failures;
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

void testInput() {
    using namespace TinyEngine;
    InputState input;
    CHECK(!input.IsKeyDown(Key::Space));
    CHECK(!input.WasKeyPressed(Key::Space));
    CHECK(!input.WasKeyReleased(Key::Space));
    input.BeginFrame();
    CHECK(input.SetKey(Key::Space, true));
    CHECK(input.SetKey(Key::Space, true));
    CHECK(input.IsKeyDown(Key::Space) && input.WasKeyPressed(Key::Space));
    CHECK(!input.WasKeyReleased(Key::Space));
    input.BeginFrame();
    CHECK(input.SetKey(Key::Space, true));
    CHECK(input.IsKeyDown(Key::Space) && !input.WasKeyPressed(Key::Space));
    input.ReleaseAll();
    CHECK(!input.IsKeyDown(Key::Space) && input.WasKeyReleased(Key::Space));
    input.ReleaseAll();
    CHECK(input.WasKeyReleased(Key::Space));
    input.BeginFrame();
    CHECK(!input.WasKeyReleased(Key::Space));

    constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> names{
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "Left", "Right", "Up", "Down", "Space", "Enter", "Escape", "Tab", "Backspace",
        "LeftShift", "RightShift", "LeftControl", "RightControl"
    };
    for (std::size_t index = 0; index < names.size(); ++index) {
        Key key = Key::Count;
        CHECK(InputState::KeyFromName(names[index], key));
        CHECK(key == static_cast<Key>(index));
        CHECK(input.SetKey(key, true));
        CHECK(input.WasKeyPressed(key));
    }
    input.BeginFrame();
    input.ReleaseAll();
    for (std::size_t index = 0; index < names.size(); ++index)
        CHECK(input.WasKeyReleased(static_cast<Key>(index)));
    for (const Key invalid : {Key::Count, static_cast<Key>(255)}) {
        CHECK(!input.SetKey(invalid, true));
        CHECK(!input.IsKeyDown(invalid));
        CHECK(!input.WasKeyPressed(invalid));
        CHECK(!input.WasKeyReleased(invalid));
    }
    Key destination = Key::Space;
    for (const std::string_view invalid : {"", "space", "Digit0", "LeftAlt", " Space"}) {
        CHECK(!InputState::KeyFromName(invalid, destination));
        CHECK(destination == Key::Space);
    }
}

void testRegistry() {
    TinyEngine::Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    CHECK(first.id != second.id);
    CHECK(registry.valid(first));
    CHECK(registry.size() == 2);
    CHECK(registry.tryGet<int>(first) == nullptr);
    registry.emplace<int>(first, 42);
    CHECK(registry.has<int>(first));
    CHECK(registry.get<int>(first) == 42);
    auto& script = registry.emplace<TinyEngine::ScriptComponent>(first, std::string("actor.lua"));
    CHECK(script.Module == "actor.lua");
    CHECK(script.Enabled);
    auto& values = registry.emplace<std::vector<int>>(first, 3, 7);
    CHECK(values.size() == 3);
    CHECK(values.front() == 7 && values.back() == 7);
    CHECK(registry.remove<int>(first));
    CHECK(!registry.remove<int>(first));
    registry.emplace<int>(second, 7);
    CHECK(registry.destroy(second));
    CHECK(!registry.destroy(second));
    CHECK(registry.tryGet<int>(second) == nullptr);
    CHECK(registry.size() == 1);
    bool caught = false;
    try { registry.emplace<int>(second, 2); } catch (const std::invalid_argument&) { caught = true; }
    CHECK(caught);
    registry.clear();
    CHECK(registry.size() == 0);
    CHECK(!registry.valid(first));
    CHECK(registry.create().id > second.id);
}

void testMath() {
    using namespace TinyEngine;
    CHECK(nearlyEqual(cross({1, 0, 0}, {0, 1, 0}), {0, 0, 1}));
    CHECK(std::fabs(dot({1, 2, 3}, {4, 5, 6}) - 32.0f) < 1e-5f);
    CHECK(nearlyEqual(Vec3{}.normalized(), {}));
    const Quat quarterTurn = Quat::fromEulerRadians({0, 0, 1.57079632679f});
    CHECK(nearlyEqual(quarterTurn.rotate({1, 0, 0}), {0, 1, 0}));
    const Mat4 matrix = Mat4::trs({1, 2, 3}, quarterTurn, {2, 2, 2});
    CHECK(nearlyEqual(matrix.transformPoint({1, 0, 0}), {1, 4, 3}));
}

void testSceneRoundTrip() {
    using namespace TinyEngine;
    Scene scene{"Scene \"quoted\""};
    EnvironmentMap environment;
    environment.EquirectangularPath = "environments/studio.hdr";
    environment.Intensity = 2.5f;
    environment.RotationRadians = 0.75f;
    CHECK(scene.SetEnvironment(environment));
    RenderSettings rendering;
    rendering.Exposure = 1.75f;
    rendering.Tonemapper = ToneMapper::Reinhard;
    rendering.EnableSsao = false;
    CHECK(scene.SetRendering(rendering));
    const auto deleted = scene.createEntity("Deleted");
    scene.destroyEntity(deleted);
    const auto entity = scene.createEntity("Main camera");
    auto& transform = scene.registry().get<TransformComponent>(entity);
    transform.Translation = {1.25f, -2.5f, 3.75f};
    transform.Rotation = Quat::fromEulerRadians({0.2f, 0.3f, -0.1f});
    transform.Scale = {2, 3, 4};
    scene.registry().emplace<CameraComponent>(entity).Primary = true;
    scene.registry().emplace<ScriptComponent>(entity).Module = "CameraController.lua";
    auto& body = scene.registry().emplace<RigidBodyComponent>(entity);
    body.Type = BodyType::Kinematic;
    body.Mass = 3.0f;
    body.Velocity = {3.0f, 2.0f, 1.0f};
    auto& collider = scene.registry().emplace<BoxColliderComponent>(entity);
    collider.HalfExtents = {0.25f, 0.5f, 0.75f};
    collider.Restitution = 0.2f;
    scene.registry().emplace<MeshRendererComponent>(entity).Mesh = "cube.glb";
    scene.registry().emplace<LightComponent>(entity).Type = LightType::Spot;
    scene.registry().emplace<AudioSourceComponent>(entity).Clip = "click.wav";
    const std::string serialized = scene.serialize();
    Scene restored = Scene::deserialize(serialized);
    CHECK(restored.name() == scene.name());
    CHECK(restored.Environment() == environment);
    CHECK(restored.Rendering() == rendering);
    CHECK(restored.registry().size() == 1);
    CHECK(restored.valid(entity));
    CHECK(restored.registry().get<TagComponent>(entity).Tag == "Main camera");
    CHECK(nearlyEqual(restored.registry().get<TransformComponent>(entity).Translation, transform.Translation));
    CHECK(nearlyEqual(restored.registry().get<TransformComponent>(entity).Rotation, transform.Rotation));
    CHECK(restored.registry().get<CameraComponent>(entity).Primary);
    CHECK(restored.registry().get<ScriptComponent>(entity).Module == "CameraController.lua");
    CHECK(restored.registry().get<RigidBodyComponent>(entity).Type == BodyType::Kinematic);
    CHECK(nearlyEqual(restored.registry().get<RigidBodyComponent>(entity).Velocity, {3, 2, 1}));
    CHECK(nearlyEqual(restored.registry().get<BoxColliderComponent>(entity).HalfExtents, {0.25f, 0.5f, 0.75f}));
    CHECK(restored.registry().get<MeshRendererComponent>(entity).Mesh == "cube.glb");
    CHECK(restored.registry().get<LightComponent>(entity).Type == LightType::Spot);
    CHECK(restored.registry().get<AudioSourceComponent>(entity).Clip == "click.wav");
    CHECK(restored.serialize() == serialized);
    CHECK(Scene::deserialize("not a scene").registry().size() == 0);
    Scene destination{"unchanged"};
    const auto existing = destination.createEntity("Existing");
    std::string error;
    CHECK(!Scene::tryDeserialize("TINYENGINE_SCENE 1\n\"broken\"\nENTITY 4 \\\"X\\\" T nope\n", destination, error));
    CHECK(!error.empty());
    CHECK(destination.name() == "unchanged");
    CHECK(destination.valid(existing));
    CHECK(Scene::tryDeserialize("TINYENGINE_SCENE 1\n\"Legacy\"\n", destination, error));
    CHECK(destination.Rendering() == RenderSettings{});
    CHECK(destination.Environment() == EnvironmentMap{});
    const auto beforeInvalid = destination.serialize();
    for (const auto* invalid : {
        "ENVIRONMENT \"../escape.hdr\" 1 0 64 16 6\n",
        "ENVIRONMENT \"a.hdr\" -1 0 64 16 6\n",
        "ENVIRONMENT \"a.hdr\" 1 0 64 16 10\n",
        "ENVIRONMENT \"\" 1 0 64 16 6\nENVIRONMENT \"\" 1 0 64 16 6\n",
        "RENDER 1 1 1 0 2048 16 0.5 0.025 1 9 32 0.001\n",
        "RENDER 1 1 1 0 2048 16 0.5 0.025 1 0 65 0.001\n",
        "ENTITY 1 \"Zero rotation\" T 0 0 0 0 0 0 0 1 1 1\n"}) {
        CHECK(!Scene::tryDeserialize(std::string("TINYENGINE_SCENE 2\n\"Invalid\"\n") + invalid, destination, error));
        CHECK(destination.serialize() == beforeInvalid);
    }
    CHECK(!Scene::tryDeserialize("TINYENGINE_SCENE 1\n\"duplicate\"\nENTITY 1 \"X\" M \"a.glb\" \"a\" 1 1 1 M \"b.glb\" \"b\" 1 1 1\n", destination, error));
    CHECK(!Scene::tryDeserialize("TINYENGINE_SCENE 1\n\"duplicate\"\nENTITY 1 \"X\" L 0 1 1 1 1 1 0.1 0.2 1 L 1 1 1 1 1 1 0.1 0.2 1\n", destination, error));
    CHECK(!Scene::tryDeserialize("TINYENGINE_SCENE 1\n\"duplicate\"\nENTITY 1 \"X\" A \"a.wav\" 1 1 0 0 A \"b.wav\" 1 1 0 0\n", destination, error));
}

void testPrefab() {
    using namespace TinyEngine;
    Scene source{"Prefab"};
    const auto entity = source.createEntity("Cube");
    source.registry().get<TransformComponent>(entity).Translation = {2, 4, 6};
    const Prefab prefab = Prefab::fromScene(source);
    Scene destination;
    destination.createEntity("Existing");
    const auto instances = prefab.instantiate(destination);
    CHECK(instances.size() == 1);
    CHECK(destination.registry().size() == 2);
    CHECK(destination.registry().get<TagComponent>(instances[0]).Tag == "Cube");
    CHECK(nearlyEqual(destination.registry().get<TransformComponent>(instances[0]).Translation, {2, 4, 6}));
}

void testScripting() {
    using namespace TinyEngine;
    Scene scene;
    const auto entity = scene.createEntity("Actor");
    NativeScriptRuntime scripts;
    int count = 0;
    scripts.registerCallback("Movement", [&](ScriptContext& context, float delta) {
        ++count;
        context.scene().registry().get<TransformComponent>(entity).Translation.x += delta;
    });
    CHECK(scripts.attach(scene, entity, "Movement"));
    CHECK(!scripts.attach(scene, Entity{999}, "Movement"));
    CHECK(!scripts.attach(scene, entity, "Missing"));
    scripts.update(scene, 0.5f);
    CHECK(count == 1);
    CHECK(scene.registry().get<TransformComponent>(entity).Translation.x == 0.5f);
    scene.registry().get<ScriptComponent>(entity).Enabled = false;
    scripts.update(scene, 0.5f);
    CHECK(count == 1);
    CHECK(scripts.unloadModule("Movement"));

    Entity created;
    scripts.registerCallback("Mutation", [&](ScriptContext& context, float) {
        const auto originalSize = context.scene().registry().size();
        auto& transform = context.scene().registry().get<TransformComponent>(entity);
        created = context.createEntity("Queued actor");
        CHECK(context.scene().valid(created));
        CHECK(context.scene().registry().size() == originalSize);
        CHECK(context.scene().registry().entities().size() == originalSize);
        CHECK(context.destroyEntity(entity));
        CHECK(!context.destroyEntity(entity));
        CHECK(context.scene().valid(entity));
        transform.Translation.y = 4; // Component remains alive through the callback.
    });
    CHECK(scripts.attach(scene, entity, "Mutation"));
    scripts.update(scene, 0.1f);
    CHECK(!scene.valid(entity));
    CHECK(scene.valid(created));
    CHECK(scene.registry().size() == 1);
    CHECK(!scene.valid(Entity{created.id, created.generation + 1}));
}

void testPhysics() {
    using namespace TinyEngine;
    Scene scene;
    const auto entity = scene.createEntity("Body");
    auto& body = scene.registry().emplace<RigidBodyComponent>(entity);
    body.Velocity = {1, 0, 0};
    PhysicsWorld world;
    CHECK(world.addBody(entity, body));
    world.step(scene, 0.5f);
    CHECK(nearlyEqual(body.Velocity, {1, -4.905f, 0}));
    CHECK(nearlyEqual(scene.registry().get<TransformComponent>(entity).Translation, {0.5f, -2.4525f, 0}));
    body.Type = BodyType::Kinematic;
    body.Velocity = {2, 0, 0};
    world.step(scene, 0.25f);
    CHECK(nearlyEqual(scene.registry().get<TransformComponent>(entity).Translation, {1, -2.4525f, 0}));
    body.Type = BodyType::Static;
    world.step(scene, 0.25f);
    CHECK(nearlyEqual(scene.registry().get<TransformComponent>(entity).Translation, {1, -2.4525f, 0}));
    const auto savedPosition = scene.registry().get<TransformComponent>(entity).Translation;
    body.Mass = -1;
    CHECK(!world.addBody(entity, body));
    world.step(scene, 0.25f);
    CHECK(nearlyEqual(scene.registry().get<TransformComponent>(entity).Translation, savedPosition));
    body.Mass = 1;
    CHECK(world.removeBody(entity));
    CHECK(!world.removeBody(entity));
    if (world.hasCollisionBackend()) {
        const auto ground = scene.createEntity("Ground");
        auto& groundTransform = scene.registry().get<TransformComponent>(ground);
        groundTransform.Translation = {0, -1, 0};
        groundTransform.Scale = {10, 1, 10};
        auto& groundBody = scene.registry().emplace<RigidBodyComponent>(ground);
        groundBody.Type = BodyType::Static;
        CHECK(world.addBody(ground, groundBody));
        body.Type = BodyType::Dynamic; body.Velocity = {};
        scene.registry().get<TransformComponent>(entity).Translation = {0, 3, 0};
        CHECK(world.addBody(entity, body));
        for (int frame = 0; frame < 240; ++frame) world.step(scene, 1.0f / 60.0f);
        const float restingY = scene.registry().get<TransformComponent>(entity).Translation.y;
        CHECK(std::fabs(restingY) < 0.05f);
        CHECK(std::fabs(body.Velocity.y) < 0.05f);
    }
}

} // namespace

int main() {
    testInput();
    testRegistry();
    testMath();
    testSceneRoundTrip();
    testPrefab();
    testScripting();
    testPhysics();
    runEngineIntegrationTests();
    runRendererTests();
    if (s_Failures != 0) {
        std::cerr << s_Failures << " checks failed\n";
        return 1;
    }
    std::cout << "All core engine checks passed\n";
    return 0;
}
