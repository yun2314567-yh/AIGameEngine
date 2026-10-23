#include "TinyEngine/Audio/Audio.h"
#include "TinyEngine/Scene.h"
#include "TinyEngine/Renderer/Components.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace {
float energy(const std::vector<float>& samples) {
    float sum = 0;
    for (const auto sample : samples) {
        assert(std::isfinite(sample));
        sum += sample * sample;
    }
    return sum;
}
}

int main() {
    using namespace TinyEngine;
    const auto root = std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets";
    const auto clip = (root / "audio/player-loop.wav").string();
    AudioSystem audio;
    std::string error;
    std::vector<float> samples(4096);
    assert(!audio.Mix(samples, &error));
    assert(!error.empty());
    assert(!audio.playOneShot(clip));
    assert(!audio.Initialize({true, 0, 2}, &error));
    if (!AudioSystem::IsAvailable()) {
        assert(!audio.Initialize({true}, &error));
        assert(error.find("without miniaudio") != std::string::npos);
        std::cout << "Audio unavailable-backend contract passed\n";
        return 0;
    }
    assert(audio.Initialize({true}, &error));
    assert(error.empty());
    assert(!audio.Initialize({true, 44100, 2}, &error));
    const auto silentMix = audio.Mix(samples, &error);
    if (!silentMix) std::cerr << error << '\n';
    assert(silentMix);
    assert(energy(samples) == 0);
    assert(!audio.playOneShot("missing.wav", 1, &error));
    assert(!audio.playOneShot(clip, -1, &error));
    assert(!audio.playOneShot(clip, std::numeric_limits<float>::quiet_NaN(), &error));
    assert(!audio.Mix(std::span<float>(samples.data(), 3), &error));

    assert(audio.playOneShot(clip, 1, &error));
    assert(audio.ActiveVoiceCount() == 1);
    assert(audio.Mix(samples, &error));
    const auto fullVolume = energy(samples);
    assert(fullVolume > 0.001f);
    // This second read detects sounds destroyed immediately after start.
    assert(audio.Mix(samples, &error));
    assert(energy(samples) > 0.001f);
    samples.resize(192000);
    assert(audio.Mix(samples, &error));
    assert(audio.ActiveVoiceCount() == 0);

    audio.shutdown();
    assert(audio.Initialize({true}, &error));
    assert(audio.playOneShot(clip, 0.5f, &error));
    samples.resize(4096);
    assert(audio.Mix(samples, &error));
    assert(std::abs(energy(samples) / fullVolume - 0.25f) < 0.01f);
    audio.StopAll();

    Scene scene;
    const auto entity = scene.createEntity("Emitter");
    auto& source = scene.registry().emplace<AudioSourceComponent>(entity);
    source.Clip = "audio/player-loop.wav";
    source.Loop = true;
    source.PlayOnStart = true;
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 1);
    samples.resize(192000);
    assert(audio.Mix(samples, &error));
    assert(energy(samples) > 0.01f);
    assert(audio.ActiveVoiceCount() == 1);
    assert(audio.Stop(entity, &error));
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 0); // Synchronizing must not restart stopped sources.
    source.Volume = 0;
    source.Pitch = 2;
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.Play(entity, &error));
    assert(audio.Mix(samples, &error));
    assert(energy(samples) == 0);
    source.Clip = "../outside.wav";
    assert(!audio.Synchronize(scene, root, &error));
    source.Clip = "audio/player-loop.wav";
    source.Volume = 1;
    source.Pitch = 0;
    assert(!audio.Synchronize(scene, root, &error));
    source.Pitch = 1;
    scene.destroyEntity(entity);
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 0);
    assert(!audio.Play(entity, &error));
    assert(!audio.Stop(entity, &error));

    const auto once = scene.createEntity("Once");
    auto& onceSource = scene.registry().emplace<AudioSourceComponent>(once);
    onceSource.Clip = "audio/prop.wav";
    onceSource.PlayOnStart = true;
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.Mix(samples, &error));
    assert(audio.ActiveVoiceCount() == 0);
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 0);
    onceSource.PlayOnStart = false;
    assert(audio.Synchronize(scene, root, &error));
    onceSource.PlayOnStart = true;
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 1);
    scene.registry().remove<AudioSourceComponent>(once);
    assert(audio.Synchronize(scene, root, &error));
    assert(audio.ActiveVoiceCount() == 0);
    audio.shutdown();
    audio.shutdown();
    assert(!audio.initialized());
    std::cout << "Offline PCM playback, volume, looping, lifecycle and validation passed\n";
}
