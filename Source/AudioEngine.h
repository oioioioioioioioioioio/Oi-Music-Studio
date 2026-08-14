#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "SpatialParameters.h"

namespace oi
{
class AudioEngine final : public juce::AudioSource,
                          public juce::ChangeBroadcaster
{
public:
    // The first five tracks are created for a new project. More tracks can be
    // added up to this limit without changing the real-time render buffers.
    static constexpr int trackCount = 5;
    static constexpr int maximumTrackCount = 12;
    static constexpr int maximumOutputChannels = 12;
    static constexpr int maximumBinauralDelaySamples = 512;
    static constexpr double defaultSpatialRegionTransitionSeconds = 0.35;

    struct ClipSource
    {
        juce::File file;
        juce::String name;
        double sampleRate = 44100.0;
        double duration = 0.0;
        std::shared_ptr<const juce::AudioBuffer<float>> samples;
        std::shared_ptr<juce::AudioThumbnail> thumbnail;
    };

    struct SpatialRegion
    {
        uint64_t id = 0;
        double startOffset = 0.0;
        double duration = 0.0;
        SpatialParameters spatial;
        float gainDb = 0.0f;
        double transitionSeconds = defaultSpatialRegionTransitionSeconds;
    };

    struct Clip
    {
        uint64_t id = 0;
        std::shared_ptr<const ClipSource> source;
        double timelineStart = 0.0;
        double sourceOffset = 0.0;
        double duration = 0.0;
        float gainDb = 0.0f;
        std::vector<SpatialRegion> spatialRegions;
    };

    struct Track
    {
        juce::String name;
        std::vector<Clip> clips;
        float gainDb = 0.0f;
        float pan = 0.0f;
        bool muted = false;
        bool solo = false;
        SpatialParameters spatial;
    };

    struct ProjectState
    {
        std::array<Track, maximumTrackCount> tracks;
        int activeTrackCount = trackCount;
        double length = 0.0;
        float masterGainDb = 0.0f;
    };

    struct ExportSettings
    {
        double sampleRate = 48000.0;
        int bitsPerSample = 24;
        int channelCount = 2;
        int samplesPerBlock = 2048;
    };

    explicit AudioEngine (bool initialiseAudioDevice = true);
    ~AudioEngine() override;

    [[nodiscard]] juce::String initialiseAudioDevice();

    std::optional<uint64_t> addFileToTrack (const juce::File&, int trackIndex,
                                            double timelineStart);
    std::optional<uint64_t> splitClipAt (int trackIndex, uint64_t clipId,
                                         double timelineSeconds);
    bool moveClip (int sourceTrackIndex, uint64_t clipId, int destinationTrackIndex,
                   double timelineStart);
    std::optional<uint64_t> duplicateClip (int sourceTrackIndex, uint64_t clipId,
                                           int destinationTrackIndex, double timelineStart);
    bool removeClip (int trackIndex, uint64_t clipId);
    bool addTrack (const juce::String& name = {});
    bool renameTrack (int trackIndex, const juce::String& name);
    [[nodiscard]] int getTrackCount() const noexcept;
    void clear();
    bool undo();
    bool redo();

    void play();
    void stop();
    void setPosition (double seconds);
    void setLooping (bool shouldLoop);
    void setPlaybackRate (double rate);

    void setTrackGainDb (int trackIndex, float gainDb);
    void setTrackPan (int trackIndex, float pan);
    void setTrackMuted (int trackIndex, bool muted);
    void setTrackSolo (int trackIndex, bool solo);
    void setTrackSpatialParameters (int trackIndex, const SpatialParameters& parameters);
    std::optional<uint64_t> createClipSpatialRegion (int trackIndex, uint64_t clipId,
                                                     double timelineStart, double timelineEnd,
                                                     const SpatialParameters& parameters);
    bool setClipSpatialRegionParameters (int trackIndex, uint64_t clipId, uint64_t regionId,
                                         const SpatialParameters& parameters);
    bool setClipSpatialRegionEnvelope (int trackIndex, uint64_t clipId, uint64_t regionId,
                                       float gainDb, double transitionSeconds);
    bool removeClipSpatialRegion (int trackIndex, uint64_t clipId, uint64_t regionId);
    void setClipGainDb (int trackIndex, uint64_t clipId, float gainDb);
    void setMasterGainDb (float gainDb);

    [[nodiscard]] juce::Result exportWav (const juce::File& destination) const;
    [[nodiscard]] juce::Result exportWav (const juce::File& destination,
                                          const ExportSettings& settings) const;
    [[nodiscard]] static juce::Result exportProjectWav (
        std::shared_ptr<const ProjectState>, const juce::File& destination);
    [[nodiscard]] static juce::Result exportProjectWav (
        std::shared_ptr<const ProjectState>, const juce::File& destination,
        const ExportSettings& settings);

    [[nodiscard]] bool hasFile() const noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] bool isLooping() const noexcept;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] double getPosition() const noexcept;
    [[nodiscard]] double getLength() const noexcept;
    [[nodiscard]] double getPlaybackRate() const noexcept;
    [[nodiscard]] int getClipCount() const noexcept;
    [[nodiscard]] float getTrackMeter (int trackIndex) const noexcept;
    [[nodiscard]] float getMasterMeter() const noexcept;
    [[nodiscard]] SpatialParameters getTrackSpatialParameters (int trackIndex) const noexcept;
    [[nodiscard]] std::optional<SpatialRegion> getClipSpatialRegion (
        int trackIndex, uint64_t clipId, uint64_t regionId) const noexcept;
    [[nodiscard]] int getOutputChannelCount() const noexcept;
    [[nodiscard]] std::shared_ptr<const ProjectState> getProjectSnapshot() const noexcept;
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

private:
    using ProjectPtr = std::shared_ptr<const ProjectState>;
    static constexpr int maximumSpatialVoices = 128;

    struct SpatialVoiceState
    {
        uint64_t clipId = 0;
        int trackIndex = -1;
        std::array<float, maximumOutputChannels + 2> filters {};
        std::array<bool, 2> filterFlags {};
        std::array<float, maximumBinauralDelaySamples> delayBuffer {};
        int delayWriteIndex = 0;
        uint64_t activeRegionId = 0;
        std::array<float, maximumOutputChannels + 2> regionFilters {};
        std::array<bool, 2> regionFilterFlags {};
        std::array<float, maximumBinauralDelaySamples> regionDelayBuffer {};
        int regionDelayWriteIndex = 0;
    };

    using SpatialVoiceStates = std::array<SpatialVoiceState, maximumSpatialVoices>;

    [[nodiscard]] std::shared_ptr<ProjectState> editableProject() const;
    void publishProject (std::shared_ptr<ProjectState>, bool addUndoPoint);
    static void updateProjectLength (ProjectState&);
    static void renderProjectBlock (const ProjectState&, juce::AudioBuffer<float>&,
                                    int startSample, int numSamples, double startPosition,
                                    double sampleRate, double playbackRate, bool looping,
                                    SpatialVoiceStates&,
                                    std::array<float, maximumTrackCount>* blockPeaks,
                                    float* masterPeak);
    [[nodiscard]] static bool isSupportedExportChannelCount (int channelCount) noexcept;
    [[nodiscard]] static SpatialParameters sanitiseSpatialParameters (SpatialParameters parameters) noexcept;
    [[nodiscard]] bool validTrack (int trackIndex) const noexcept;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 32 };
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;

    // libc++ on some Android NDK releases does not provide the C++20
    // atomic<shared_ptr> specialization. The standard free functions
    // atomic_load/atomic_store provide the same lock-free implementation
    // where available and remain portable to those releases.
    ProjectPtr projectState;
    mutable juce::CriticalSection editLock;
    std::vector<ProjectPtr> undoHistory;
    std::vector<ProjectPtr> redoHistory;
    std::atomic<uint64_t> nextClipId { 1 };
    std::atomic<uint64_t> nextSpatialRegionId { 1 };

    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<double> positionSeconds { 0.0 };
    std::atomic<double> playbackRate { 1.0 };
    std::atomic<double> outputSampleRate { 44100.0 };
    std::array<std::atomic<float>, maximumTrackCount> trackMeters {};
    std::unique_ptr<SpatialVoiceStates> spatialVoices;
    std::atomic<float> masterMeter { 0.0f };
    bool deviceInitialised = false;
};
} // namespace oi
