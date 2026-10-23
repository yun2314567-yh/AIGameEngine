#pragma once

#include <memory>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include "TinyEngine/ECS.h"

namespace TinyEngine {

class Scene;

struct AudioConfig {
    bool Offline = false;
    std::uint32_t SampleRate = 48000;
    std::uint32_t Channels = 2;
};

// Owned by the main thread. All methods, including offline mixing, must be
// called on that thread; miniaudio manages the device callback internally.
// Fallible methods return false and populate error without throwing for
// invalid files, settings or an unavailable backend.
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool initialize(std::string* error = nullptr);
    void shutdown();
    [[nodiscard]] bool initialized() const;
    bool playOneShot(const std::string& path, float volume = 1.0f, std::string* error = nullptr);

    [[nodiscard]] static bool IsAvailable();
    bool Initialize(const AudioConfig& config, std::string* error = nullptr);
    // Newly observed sources play once when PlayOnStart is true; finished
    // non-looping sources stay stopped. Paths must stay inside assetRoot.
    // Removed sources stop immediately. Failure may leave earlier sources updated.
    bool Synchronize(Scene& scene, const std::filesystem::path& assetRoot, std::string* error = nullptr);
    bool Play(Entity entity, std::string* error = nullptr);
    bool Stop(Entity entity, std::string* error = nullptr);
    void StopAll();
    // Fills interleaved float PCM. Requires Offline=true and whole frames at
    // the configured channel count. On failure output may be partially written.
    bool Mix(std::span<float> samples, std::string* error = nullptr);
    [[nodiscard]] std::size_t ActiveVoiceCount() const;
    [[nodiscard]] const AudioConfig& Config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace TinyEngine
