#include "TinyEngine/Audio/Audio.h"
#include "TinyEngine/Scene.h"
#include "TinyEngine/Renderer/Components.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(TINYENGINE_WITH_MINIAUDIO)
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif

namespace TinyEngine {
namespace {
bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

#if defined(TINYENGINE_WITH_MINIAUDIO)
bool resolveClip(const std::filesystem::path& root, const std::string& clip,
                 std::filesystem::path& result, std::string* error) {
    const std::filesystem::path relative{clip};
    if (relative.empty() || relative.has_root_path()) return fail(error, "Audio clip must be relative to the asset root");
    for (const auto& part : relative) if (part == "..") return fail(error, "Audio clip is outside the asset root");
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return fail(error, "Could not resolve the audio asset root: " + ec.message());
    result = std::filesystem::weakly_canonical(canonicalRoot / relative, ec);
    if (ec) return fail(error, "Could not resolve audio clip: " + clip);
    const auto contained = result.lexically_relative(canonicalRoot);
    if (contained.empty()) return fail(error, "Audio clip is outside the asset root");
    for (const auto& part : contained) if (part == "..") return fail(error, "Audio clip is outside the asset root");
    if (!std::filesystem::is_regular_file(result, ec) || ec) return fail(error, "Audio clip does not exist: " + clip);
    return true;
}
#endif
} // namespace

struct AudioSystem::Impl {
    AudioConfig Config;
    bool Initialized = false;
#if defined(TINYENGINE_WITH_MINIAUDIO)
    struct Voice {
        ma_sound Sound{};
        bool Initialized = false;
        std::filesystem::path Path;
        bool PlayOnStart = false;
        ~Voice() { if (Initialized) ma_sound_uninit(&Sound); }
    };
    ma_engine Engine{};
    std::unordered_map<Entity, std::unique_ptr<Voice>, EntityHash> Sources;
    std::vector<std::unique_ptr<Voice>> OneShots;

    std::unique_ptr<Voice> Load(const std::filesystem::path& path, std::string* error) {
        auto voice = std::make_unique<Voice>();
        const auto result = ma_sound_init_from_file(&Engine, path.string().c_str(), MA_SOUND_FLAG_DECODE,
                                                    nullptr, nullptr, &voice->Sound);
        if (result != MA_SUCCESS) {
            fail(error, "Unable to load audio file (" + std::to_string(static_cast<int>(result)) + "): " + path.string());
            return nullptr;
        }
        voice->Initialized = true;
        voice->Path = path;
        return voice;
    }
    void Collect() {
        std::erase_if(OneShots, [](const auto& voice) { return ma_sound_at_end(&voice->Sound) == MA_TRUE; });
    }
#endif
};

AudioSystem::AudioSystem() : m_Impl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::IsAvailable() {
#if defined(TINYENGINE_WITH_MINIAUDIO)
    return true;
#else
    return false;
#endif
}

bool AudioSystem::initialize(std::string* error) { return Initialize({}, error); }

bool AudioSystem::Initialize(const AudioConfig& config, std::string* error) {
    if (error) error->clear();
    if (config.SampleRate < 8000 || config.SampleRate > 192000 || config.Channels == 0 || config.Channels > 8)
        return fail(error, "Audio sample rate must be 8000..192000 Hz and channels must be 1..8");
    if (initialized()) {
        const auto& previous = m_Impl->Config;
        if (config.Offline != previous.Offline || config.SampleRate != previous.SampleRate || config.Channels != previous.Channels)
            return fail(error, "Shut down audio before changing its configuration");
        return true;
    }
#if defined(TINYENGINE_WITH_MINIAUDIO)
    auto engineConfig = ma_engine_config_init();
    engineConfig.noDevice = config.Offline ? MA_TRUE : MA_FALSE;
    engineConfig.sampleRate = config.SampleRate;
    engineConfig.channels = config.Channels;
    const ma_result result = ma_engine_init(&engineConfig, &m_Impl->Engine);
    if (result != MA_SUCCESS)
        return fail(error, "miniaudio engine initialization failed (" + std::to_string(static_cast<int>(result)) + ")");
    m_Impl->Config = config;
    m_Impl->Initialized = true;
    return true;
#else
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

void AudioSystem::shutdown() {
    StopAll();
#if defined(TINYENGINE_WITH_MINIAUDIO)
    if (m_Impl && m_Impl->Initialized) ma_engine_uninit(&m_Impl->Engine);
#endif
    if (m_Impl) m_Impl->Initialized = false;
}

bool AudioSystem::initialized() const { return m_Impl && m_Impl->Initialized; }
const AudioConfig& AudioSystem::Config() const { return m_Impl->Config; }

bool AudioSystem::playOneShot(const std::string& path, float volume, std::string* error) {
    if (error) error->clear();
    if (path.empty() || !std::isfinite(volume) || volume < 0.0f)
        return fail(error, "Audio path must be non-empty and volume must be finite and non-negative");
    if (!initialized()) return fail(error, "AudioSystem is not initialized");
#if defined(TINYENGINE_WITH_MINIAUDIO)
    m_Impl->Collect();
    auto voice = m_Impl->Load(path, error);
    if (!voice) return false;
    ma_sound_set_spatialization_enabled(&voice->Sound, MA_FALSE);
    ma_sound_set_volume(&voice->Sound, volume);
    if (ma_sound_start(&voice->Sound) != MA_SUCCESS) return fail(error, "Unable to start audio playback");
    m_Impl->OneShots.push_back(std::move(voice));
    return true;
#else
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

bool AudioSystem::Synchronize(Scene& scene, const std::filesystem::path& assetRoot, std::string* error) {
    if (error) error->clear();
    if (!initialized()) return fail(error, "AudioSystem is not initialized");
#if defined(TINYENGINE_WITH_MINIAUDIO)
    m_Impl->Collect();
    std::unordered_set<Entity, EntityHash> present;
    auto& registry = scene.registry();
    const auto entities = registry.entities();
    for (const auto entity : entities) {
        const auto* source = registry.tryGet<AudioSourceComponent>(entity);
        if (source && !source->Clip.empty()) present.insert(entity);
    }
    std::erase_if(m_Impl->Sources, [&present](const auto& item) { return !present.contains(item.first); });
    for (const auto entity : entities) {
        const auto* source = registry.tryGet<AudioSourceComponent>(entity);
        if (!source || source->Clip.empty()) continue;
        if (!std::isfinite(source->Volume) || source->Volume < 0 || !std::isfinite(source->Pitch) || source->Pitch <= 0)
            return fail(error, "Audio volume must be finite and non-negative; pitch must be finite and positive");
        std::filesystem::path path;
        if (!resolveClip(assetRoot, source->Clip, path, error)) return false;
        auto found = m_Impl->Sources.find(entity);
        bool start = false;
        if (found == m_Impl->Sources.end() || found->second->Path != path) {
            auto voice = m_Impl->Load(path, error);
            if (!voice) return false;
            start = source->PlayOnStart;
            m_Impl->Sources[entity] = std::move(voice);
            found = m_Impl->Sources.find(entity);
        } else if (source->PlayOnStart && !found->second->PlayOnStart) {
            start = true;
        }
        auto& voice = *found->second;
        voice.PlayOnStart = source->PlayOnStart;
        ma_sound_set_volume(&voice.Sound, source->Volume);
        ma_sound_set_pitch(&voice.Sound, source->Pitch);
        ma_sound_set_looping(&voice.Sound, source->Loop ? MA_TRUE : MA_FALSE);
        if (const auto* transform = registry.tryGet<TransformComponent>(entity)) {
            const auto& p = transform->Translation;
            ma_sound_set_position(&voice.Sound, p.x, p.y, p.z);
        }
        if (start && !Play(entity, error)) return false;
    }
    Vec3 position{}, forward{0, 0, -1}, up{0, 1, 0};
    for (const auto entity : entities) {
        const auto* camera = registry.tryGet<CameraComponent>(entity);
        const auto* transform = registry.tryGet<TransformComponent>(entity);
        if (!camera || !camera->Primary || !transform) continue;
        position = transform->Translation;
        const auto rotation = transform->Rotation.normalized();
        forward = rotation.rotate(forward);
        up = rotation.rotate(up);
        break;
    }
    ma_engine_listener_set_position(&m_Impl->Engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_Impl->Engine, 0, up.x, up.y, up.z);
    return true;
#else
    (void)scene; (void)assetRoot;
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

bool AudioSystem::Play(Entity entity, std::string* error) {
    if (error) error->clear();
    if (!initialized()) return fail(error, "AudioSystem is not initialized");
#if defined(TINYENGINE_WITH_MINIAUDIO)
    const auto found = m_Impl->Sources.find(entity);
    if (found == m_Impl->Sources.end()) return fail(error, "Entity has no synchronized audio clip");
    auto& sound = found->second->Sound;
    if (ma_sound_seek_to_pcm_frame(&sound, 0) != MA_SUCCESS || ma_sound_start(&sound) != MA_SUCCESS)
        return fail(error, "Could not start entity audio source");
    return true;
#else
    (void)entity;
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

bool AudioSystem::Stop(Entity entity, std::string* error) {
    if (error) error->clear();
    if (!initialized()) return fail(error, "AudioSystem is not initialized");
#if defined(TINYENGINE_WITH_MINIAUDIO)
    const auto found = m_Impl->Sources.find(entity);
    if (found == m_Impl->Sources.end()) return fail(error, "Entity has no synchronized audio clip");
    if (ma_sound_stop(&found->second->Sound) != MA_SUCCESS) return fail(error, "Could not stop entity audio source");
    return true;
#else
    (void)entity;
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

void AudioSystem::StopAll() {
#if defined(TINYENGINE_WITH_MINIAUDIO)
    m_Impl->Sources.clear();
    m_Impl->OneShots.clear();
#endif
}

bool AudioSystem::Mix(std::span<float> samples, std::string* error) {
    if (error) error->clear();
    if (!initialized() || !m_Impl->Config.Offline) return fail(error, "PCM mixing requires initialized offline audio");
    if (samples.size() % m_Impl->Config.Channels != 0) return fail(error, "PCM output must contain whole audio frames");
#if defined(TINYENGINE_WITH_MINIAUDIO)
    if (samples.empty()) return true;
    ma_uint64 framesRead = 0;
    const auto frames = samples.size() / m_Impl->Config.Channels;
    const auto result = ma_engine_read_pcm_frames(&m_Impl->Engine, samples.data(), frames, &framesRead);
    // The node graph returns zero/partial frames and pads with silence when
    // no source is active. This is normal, including after a one-shot ends.
    if (result != MA_SUCCESS)
        return fail(error, "Could not mix audio frames (" + std::to_string(result) + ", " + std::to_string(framesRead) + "/" + std::to_string(frames) + ")");
    m_Impl->Collect();
    return true;
#else
    return fail(error, "TinyEngine was built without miniaudio support");
#endif
}

std::size_t AudioSystem::ActiveVoiceCount() const {
#if defined(TINYENGINE_WITH_MINIAUDIO)
    std::size_t count = 0;
    for (const auto& [entity, voice] : m_Impl->Sources) if (ma_sound_is_playing(&voice->Sound)) ++count;
    for (const auto& voice : m_Impl->OneShots) if (ma_sound_is_playing(&voice->Sound)) ++count;
    return count;
#else
    return 0;
#endif
}

} // namespace TinyEngine
