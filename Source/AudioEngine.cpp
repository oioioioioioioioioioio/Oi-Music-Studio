#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace oi
{
namespace
{
constexpr size_t maximumUndoStates = 64;
constexpr int maximumRenderChannels = AudioEngine::maximumOutputChannels;

float interpolatedSample (const juce::AudioBuffer<float>& buffer, int channel,
                          double samplePosition) noexcept
{
    if (buffer.getNumSamples() == 0 || channel >= buffer.getNumChannels())
        return 0.0f;

    const auto baseIndex = juce::jlimit (0, buffer.getNumSamples() - 1,
                                         static_cast<int> (samplePosition));
    const auto nextIndex = juce::jmin (baseIndex + 1, buffer.getNumSamples() - 1);
    const auto fraction = static_cast<float> (samplePosition - std::floor (samplePosition));
    return juce::jmap (fraction, buffer.getSample (channel, baseIndex),
                       buffer.getSample (channel, nextIndex));
}

float wrappedDegrees (float degrees) noexcept
{
    auto result = std::fmod (degrees + 180.0f, 360.0f);
    if (result < 0.0f)
        result += 360.0f;
    return result - 180.0f;
}

float spatialDistanceGain (const SpatialParameters& parameters) noexcept
{
    const auto distance = juce::jlimit (0.5f, 12.0f, parameters.distance);
    constexpr auto referenceDistance = 1.0f;
    switch (parameters.attenuation)
    {
        case SpatialAttenuation::linear:
            return distance <= referenceDistance
                       ? 1.0f
                       : juce::jmap (distance, referenceDistance, 12.0f, 1.0f, 0.12f);
        case SpatialAttenuation::customCurve:
            return distance <= referenceDistance
                       ? 1.0f
                       : 1.0f / (1.0f + 0.18f * (distance - referenceDistance));
        case SpatialAttenuation::inverseSquare:
        default:
            // Acoustic intensity is inverse-square; pressure amplitude is inverse-distance.
            return referenceDistance / juce::jmax (referenceDistance, distance);
    }
}

struct StereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

struct SpeakerPosition
{
    float azimuth = 0.0f;
    float elevation = 0.0f;
    bool active = false;
    bool lowFrequency = false;
};

void makeSpeakerLayout (int channelCount,
                        std::array<SpeakerPosition, maximumRenderChannels>& speakers) noexcept
{
    speakers.fill ({ });
    if (channelCount <= 2)
        return;

    // Common JUCE/DAW channel ordering: L, R, C, LFE, surrounds, then heights.
    const auto setSpeaker = [&speakers] (int index, float azimuth, float elevation = 0.0f,
                                         bool lowFrequency = false)
    {
        speakers[static_cast<size_t> (index)] = { azimuth, elevation, true, lowFrequency };
    };

    if (channelCount >= 12) // 7.1.4
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, 0.0f, 0.0f, true);
        setSpeaker (4, -150.0f); setSpeaker (5, 150.0f);
        setSpeaker (6, -110.0f); setSpeaker (7, 110.0f);
        setSpeaker (8, -45.0f, 45.0f); setSpeaker (9, 45.0f, 45.0f);
        setSpeaker (10, -135.0f, 45.0f); setSpeaker (11, 135.0f, 45.0f);
    }
    else if (channelCount >= 10) // 5.1.4
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, 0.0f, 0.0f, true);
        setSpeaker (4, -110.0f); setSpeaker (5, 110.0f);
        setSpeaker (6, -45.0f, 45.0f); setSpeaker (7, 45.0f, 45.0f);
        setSpeaker (8, -135.0f, 45.0f); setSpeaker (9, 135.0f, 45.0f);
    }
    else if (channelCount >= 8) // 7.1
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, 0.0f, 0.0f, true);
        setSpeaker (4, -150.0f); setSpeaker (5, 150.0f);
        setSpeaker (6, -110.0f); setSpeaker (7, 110.0f);
    }
    else if (channelCount == 7) // 6.1
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, 0.0f, 0.0f, true);
        setSpeaker (4, -110.0f); setSpeaker (5, 110.0f); setSpeaker (6, 180.0f);
    }
    else if (channelCount == 6) // 5.1
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, 0.0f, 0.0f, true);
        setSpeaker (4, -110.0f); setSpeaker (5, 110.0f);
    }
    else if (channelCount == 5) // 5.0
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
        setSpeaker (3, -110.0f); setSpeaker (4, 110.0f);
    }
    else if (channelCount == 4) // quad
    {
        setSpeaker (0, -45.0f); setSpeaker (1, 45.0f);
        setSpeaker (2, -135.0f); setSpeaker (3, 135.0f);
    }
    else // 3.0
    {
        setSpeaker (0, -30.0f); setSpeaker (1, 30.0f); setSpeaker (2, 0.0f);
    }
}

float readFractionalDelay (
    const std::array<float, AudioEngine::maximumBinauralDelaySamples>& buffer,
    int writeIndex, float delaySamples) noexcept
{
    constexpr auto bufferSize = AudioEngine::maximumBinauralDelaySamples;
    auto readPosition = static_cast<float> (writeIndex) - delaySamples;
    while (readPosition < 0.0f)
        readPosition += static_cast<float> (bufferSize);

    const auto first = static_cast<int> (readPosition) % bufferSize;
    const auto second = (first + 1) % bufferSize;
    const auto fraction = readPosition - std::floor (readPosition);
    return juce::jmap (fraction, buffer[static_cast<size_t> (first)],
                       buffer[static_cast<size_t> (second)]);
}

StereoFrame renderSpatialStereo (
    float inputLeft, float inputRight, const SpatialParameters& parameters,
    double timelineTime, double sampleRate,
    std::array<float, maximumRenderChannels + 2>& filterState,
    std::array<bool, 2>& filterFlags,
    std::array<float, AudioEngine::maximumBinauralDelaySamples>& delayBuffer,
    int& delayWriteIndex) noexcept
{
    const auto azimuth = wrappedDegrees (parameters.azimuth
                                         + parameters.orbitSpeed * static_cast<float> (timelineTime));
    const auto azimuthRadians = juce::degreesToRadians (azimuth);
    const auto lateral = std::sin (azimuthRadians);
    const auto rearAmount = juce::jlimit (0.0f, 1.0f, -std::cos (azimuthRadians));
    const auto elevationDegrees = juce::jlimit (-90.0f, 90.0f, parameters.elevation);
    const auto elevation = std::abs (elevationDegrees) / 90.0f;
    const auto elevationGain = 1.0f - 0.08f * elevation
                             - (elevationDegrees < 0.0f ? 0.04f * elevation : 0.0f);
    // A fixed-radius orbit must retain its energy around the circle. Front/rear is
    // conveyed by spectral shape below rather than a false change in distance.
    constexpr auto rearGain = 1.0f;
    const auto spread = juce::jlimit (0.0f, 1.0f, parameters.spread / 100.0f);
    const auto width = juce::jlimit (0.0f, 1.0f, parameters.spread / 100.0f)
                     * (1.0f - 0.55f * elevation) * (1.0f - 0.45f * std::abs (lateral));
    const auto distanceGain = spatialDistanceGain (parameters);
    const auto mono = (inputLeft + inputRight) * 0.5f * distanceGain;
    const auto side = (inputLeft - inputRight) * 0.5f * distanceGain * width;

    // Approximate the two strongest binaural cues for headphone/stereo monitoring:
    // interaural time difference and frequency-dependent head shadow. This remains a
    // deterministic pseudo-binaural renderer; a measured HRTF/SOFA set is still needed
    // for listener-specific front/back and height localisation.
    if (! filterFlags[1])
    {
        delayBuffer.fill (0.0f);
        delayWriteIndex = 0;
        filterState[maximumRenderChannels] = 0.0f;
        filterState[maximumRenderChannels + 1] = 0.0f;
        filterFlags[1] = true;
    }

    delayWriteIndex = juce::jlimit (0, AudioEngine::maximumBinauralDelaySamples - 1,
                                    delayWriteIndex);
    delayBuffer[static_cast<size_t> (delayWriteIndex)] = mono;
    constexpr auto maximumItdSeconds = 0.00065f;
    const auto maximumDelay = juce::jmin (
        static_cast<float> (AudioEngine::maximumBinauralDelaySamples - 2),
        maximumItdSeconds * static_cast<float> (sampleRate));
    const auto delayedLeft = readFractionalDelay (delayBuffer, delayWriteIndex,
                                                   juce::jmax (0.0f, lateral) * maximumDelay);
    const auto delayedRight = readFractionalDelay (delayBuffer, delayWriteIndex,
                                                    juce::jmax (0.0f, -lateral) * maximumDelay);
    delayWriteIndex = (delayWriteIndex + 1) % AudioEngine::maximumBinauralDelaySamples;

    const auto directivity = juce::jlimit (0.0f, 1.0f, parameters.directivity / 100.0f);
    const auto shadowDepth = juce::jmap (directivity, 0.42f, 0.70f) * (1.0f - 0.55f * spread);
    auto leftGain = 1.0f - shadowDepth * juce::jmax (0.0f, lateral);
    auto rightGain = 1.0f - shadowDepth * juce::jmax (0.0f, -lateral);
    const auto gainEnergy = std::sqrt (leftGain * leftGain + rightGain * rightGain);
    if (gainEnergy > 0.000001f)
    {
        leftGain /= gainEnergy;
        rightGain /= gainEnergy;
    }

    const auto baseCutoff = juce::jmap (rearAmount, 18000.0f, 4800.0f)
                          * (1.0f - 0.18f * elevation
                             - (elevationDegrees < 0.0f ? 0.14f * elevation : 0.0f));
    const auto leftCutoff = baseCutoff * (1.0f - 0.72f * juce::jmax (0.0f, lateral));
    const auto rightCutoff = baseCutoff * (1.0f - 0.72f * juce::jmax (0.0f, -lateral));
    const auto processLowPass = [sampleRate] (float input, float cutoff, float& state)
    {
        const auto safeCutoff = juce::jlimit (800.0f,
                                              static_cast<float> (sampleRate * 0.45), cutoff);
        const auto coefficient = std::exp (-juce::MathConstants<float>::twoPi * safeCutoff
                                           / static_cast<float> (sampleRate));
        state = (1.0f - coefficient) * input + coefficient * state;
        return state;
    };

    auto left = processLowPass (delayedLeft, leftCutoff,
                                filterState[maximumRenderChannels]);
    auto right = processLowPass (delayedRight, rightCutoff,
                                 filterState[maximumRenderChannels + 1]);
    left = (left * leftGain + side) * rearGain * elevationGain;
    right = (right * rightGain - side) * rearGain * elevationGain;
    return { left, right };
}

void renderSpatialChannels (
    float inputLeft, float inputRight, const SpatialParameters& parameters,
    double timelineTime, double sampleRate, int channelCount,
    std::array<float, maximumRenderChannels>& rendered,
    std::array<float, maximumRenderChannels + 2>& filterState,
    std::array<bool, 2>& filterFlags,
    std::array<float, AudioEngine::maximumBinauralDelaySamples>& delayBuffer,
    int& delayWriteIndex) noexcept
{
    rendered.fill (0.0f);
    if (channelCount <= 2)
    {
        const auto stereo = renderSpatialStereo (inputLeft, inputRight, parameters, timelineTime,
                                                  sampleRate, filterState, filterFlags,
                                                  delayBuffer, delayWriteIndex);
        if (channelCount > 0) rendered[0] = stereo.left;
        if (channelCount > 1) rendered[1] = stereo.right;
        return;
    }

    filterFlags[1] = false;

    const auto azimuth = wrappedDegrees (parameters.azimuth
                                         + parameters.orbitSpeed * static_cast<float> (timelineTime));
    const auto elevation = juce::degreesToRadians (juce::jlimit (-90.0f, 90.0f, parameters.elevation));
    const auto azimuthRadians = juce::degreesToRadians (azimuth);
    const auto distanceGain = spatialDistanceGain (parameters)
                            * (1.0f - 0.08f * std::abs (elevation) / juce::MathConstants<float>::halfPi);
    const auto spread = juce::jlimit (0.0f, 1.0f, parameters.spread / 100.0f);
    const auto mid = (inputLeft + inputRight) * 0.5f * distanceGain;
    const auto directivity = 1.0f + juce::jlimit (0.0f, 100.0f, parameters.directivity)
                             / 100.0f * 3.0f * (1.0f - spread);

    std::array<SpeakerPosition, maximumRenderChannels> speakers;
    makeSpeakerLayout (juce::jmin (channelCount, maximumRenderChannels), speakers);
    std::array<float, maximumRenderChannels> weights {};
    float weightEnergy = 0.0f;
    for (int channel = 0; channel < juce::jmin (channelCount, maximumRenderChannels); ++channel)
    {
        const auto& speaker = speakers[static_cast<size_t> (channel)];
        if (! speaker.active || speaker.lowFrequency)
            continue;

        const auto speakerElevation = juce::degreesToRadians (speaker.elevation);
        const auto deltaAzimuth = juce::degreesToRadians (speaker.azimuth) - azimuthRadians;
        const auto dot = std::cos (elevation) * std::cos (speakerElevation) * std::cos (deltaAzimuth)
                       + std::sin (elevation) * std::sin (speakerElevation);
        const auto weight = std::pow (juce::jmax (0.0f, dot), directivity);
        weights[static_cast<size_t> (channel)] = weight;
        weightEnergy += weight * weight;
    }

    if (weightEnergy <= 0.000001f)
    {
        // At extreme elevations there may be no positive dot product. Route to the nearest
        // full-range speaker so the object remains audible instead of disappearing.
        auto nearest = 0;
        auto nearestDot = -2.0f;
        for (int channel = 0; channel < juce::jmin (channelCount, maximumRenderChannels); ++channel)
        {
            const auto& speaker = speakers[static_cast<size_t> (channel)];
            if (! speaker.active || speaker.lowFrequency) continue;
            const auto speakerElevation = juce::degreesToRadians (speaker.elevation);
            const auto deltaAzimuth = juce::degreesToRadians (speaker.azimuth) - azimuthRadians;
            const auto dot = std::cos (elevation) * std::cos (speakerElevation) * std::cos (deltaAzimuth)
                           + std::sin (elevation) * std::sin (speakerElevation);
            if (dot > nearestDot) { nearestDot = dot; nearest = channel; }
        }
        weights[static_cast<size_t> (nearest)] = 1.0f;
        weightEnergy = 1.0f;
    }

    const auto normalise = 1.0f / std::sqrt (weightEnergy);
    for (int channel = 0; channel < juce::jmin (channelCount, maximumRenderChannels); ++channel)
    {
        const auto& speaker = speakers[static_cast<size_t> (channel)];
        if (! speaker.active || speaker.lowFrequency)
            continue;
        const auto positional = weights[static_cast<size_t> (channel)] * normalise;
        rendered[static_cast<size_t> (channel)] = mid * positional;
    }
}

float airAbsorptionCutoff (float distance) noexcept
{
    return juce::jmap (juce::jlimit (0.5f, 12.0f, distance),
                       0.5f, 12.0f, 20000.0f, 6000.0f);
}

float spatialRegionBlend (const AudioEngine::SpatialRegion& region,
                          double clipTime) noexcept
{
    const auto localTime = clipTime - region.startOffset;
    if (localTime < 0.0 || localTime >= region.duration)
        return 0.0f;

    const auto transition = juce::jmin (region.transitionSeconds,
                                         region.duration * 0.5);
    if (transition <= 0.000001)
        return 1.0f;

    const auto smoothStep = [] (double value)
    {
        const auto normal = static_cast<float> (juce::jlimit (0.0, 1.0, value));
        return normal * normal * (3.0f - 2.0f * normal);
    };
    if (localTime < transition)
        return smoothStep (localTime / transition);

    const auto remaining = region.duration - localTime;
    if (remaining < transition)
        return smoothStep (remaining / transition);

    return 1.0f;
}

juce::AudioChannelSet exportChannelLayout (int channelCount)
{
    // RIFF/WAVE orders 7.1 as back L/R followed by side L/R. JUCE's standard
    // 7.1 sets use SurroundRear channel types that its own WAV writer rejects.
    const auto wave7point1 = []
    {
        juce::Array<juce::AudioChannelSet::ChannelType> channels {
            juce::AudioChannelSet::left, juce::AudioChannelSet::right,
            juce::AudioChannelSet::centre, juce::AudioChannelSet::LFE,
            juce::AudioChannelSet::leftSurround, juce::AudioChannelSet::rightSurround,
            juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::rightSurroundSide
        };
        return juce::AudioChannelSet::channelSetWithChannels (channels);
    };

    switch (channelCount)
    {
        case 2:  return juce::AudioChannelSet::stereo();
        case 6:  return juce::AudioChannelSet::create5point1();
        case 8:  return wave7point1();
        case 10: return juce::AudioChannelSet::create5point1point4();
        case 12:
        {
            auto layout = wave7point1();
            layout.addChannel (juce::AudioChannelSet::topFrontLeft);
            layout.addChannel (juce::AudioChannelSet::topFrontRight);
            layout.addChannel (juce::AudioChannelSet::topRearLeft);
            layout.addChannel (juce::AudioChannelSet::topRearRight);
            return layout;
        }
        default: return {};
    }
}
}

AudioEngine::AudioEngine (bool initialiseAudioDevice)
{
    formatManager.registerBasicFormats();
    auto initialProject = std::make_shared<ProjectState>();
    const std::array<const char*, trackCount> defaultNames {
        "Lead Vocal", "Synth", "Drums", "Atmosphere", "FX Return"
    };
    for (int index = 0; index < trackCount; ++index)
        initialProject->tracks[static_cast<size_t> (index)].name = defaultNames[static_cast<size_t> (index)];
    std::atomic_store (&projectState, ProjectPtr (std::move (initialProject)));
    spatialVoices = std::make_unique<SpatialVoiceStates>();

    for (auto& meter : trackMeters)
        meter.store (0.0f);

    sourcePlayer.setSource (this);
    if (initialiseAudioDevice)
        juce::ignoreUnused (this->initialiseAudioDevice());
}

AudioEngine::~AudioEngine()
{
    playing.store (false);
    if (deviceInitialised)
        deviceManager.removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
}

juce::String AudioEngine::initialiseAudioDevice()
{
    if (deviceInitialised)
        return {};

   #if JUCE_ANDROID
    // Android creates the JUCE application while its Activity is still starting.
    // Call this method from a later message-loop turn rather than the constructor.
    constexpr auto requestedOutputs = 2;
   #else
    constexpr auto requestedOutputs = maximumOutputChannels;
   #endif

    const auto error = deviceManager.initialise (0, requestedOutputs, nullptr, true);
    if (error.isNotEmpty())
        return error;

    deviceManager.addAudioCallback (&sourcePlayer);
    deviceInitialised = true;
    return {};
}

std::optional<uint64_t> AudioEngine::addFileToTrack (const juce::File& file,
                                                     int trackIndex,
                                                     double timelineStart)
{
    if (! validTrack (trackIndex))
        return std::nullopt;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
        return std::nullopt;

    const auto channelCount = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
    const auto sampleCount = static_cast<int> (reader->lengthInSamples);
    auto samples = std::make_shared<juce::AudioBuffer<float>> (channelCount, sampleCount);
    samples->clear();
    if (! reader->read (samples.get(), 0, sampleCount, 0, true, true))
        return std::nullopt;

    auto thumbnail = std::make_shared<juce::AudioThumbnail> (512, formatManager, thumbnailCache);
    thumbnail->setSource (new juce::FileInputSource (file));

    auto source = std::make_shared<ClipSource>();
    source->file = file;
    source->name = file.getFileNameWithoutExtension();
    source->sampleRate = reader->sampleRate;
    source->duration = static_cast<double> (sampleCount) / reader->sampleRate;
    source->samples = std::move (samples);
    source->thumbnail = std::move (thumbnail);

    Clip clip;
    clip.id = nextClipId.fetch_add (1);
    clip.source = std::move (source);
    clip.timelineStart = juce::jmax (0.0, timelineStart);
    clip.duration = clip.source->duration;

    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].clips.push_back (clip);
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    std::sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b)
    {
        return a.timelineStart < b.timelineStart;
    });
    publishProject (std::move (next), true);
    return clip.id;
}

std::optional<uint64_t> AudioEngine::splitClipAt (int trackIndex, uint64_t clipId,
                                                  double timelineSeconds)
{
    if (! validTrack (trackIndex))
        return std::nullopt;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(), [=] (const Clip& clip)
    {
        const auto matchesId = clipId == 0 || clip.id == clipId;
        return matchesId && timelineSeconds > clip.timelineStart + 0.001
            && timelineSeconds < clip.timelineStart + clip.duration - 0.001;
    });

    if (iterator == clips.end())
        return std::nullopt;

    const auto leftDuration = timelineSeconds - iterator->timelineStart;
    Clip right = *iterator;
    right.id = nextClipId.fetch_add (1);
    right.timelineStart = timelineSeconds;
    right.sourceOffset += leftDuration;
    right.duration -= leftDuration;
    right.spatialRegions.clear();

    std::vector<SpatialRegion> leftRegions;
    for (const auto& region : iterator->spatialRegions)
    {
        const auto regionEnd = region.startOffset + region.duration;
        if (region.startOffset < leftDuration)
        {
            auto leftRegion = region;
            leftRegion.duration = juce::jmin (regionEnd, leftDuration) - region.startOffset;
            if (leftRegion.duration > 0.000001)
                leftRegions.push_back (leftRegion);
        }
        if (regionEnd > leftDuration)
        {
            auto rightRegion = region;
            rightRegion.id = nextSpatialRegionId.fetch_add (1);
            rightRegion.startOffset = juce::jmax (0.0, region.startOffset - leftDuration);
            rightRegion.duration = regionEnd - juce::jmax (region.startOffset, leftDuration);
            if (rightRegion.duration > 0.000001)
                right.spatialRegions.push_back (rightRegion);
        }
    }
    iterator->spatialRegions = std::move (leftRegions);
    iterator->duration = leftDuration;
    clips.insert (std::next (iterator), right);
    publishProject (std::move (next), true);
    return right.id;
}

bool AudioEngine::moveClip (int sourceTrackIndex, uint64_t clipId,
                            int destinationTrackIndex, double timelineStart)
{
    if (! validTrack (sourceTrackIndex) || ! validTrack (destinationTrackIndex) || clipId == 0)
        return false;

    auto next = editableProject();
    auto& sourceClips = next->tracks[static_cast<size_t> (sourceTrackIndex)].clips;
    const auto iterator = std::find_if (sourceClips.begin(), sourceClips.end(),
                                        [clipId] (const Clip& clip) { return clip.id == clipId; });
    if (iterator == sourceClips.end())
        return false;

    Clip moved = *iterator;
    moved.timelineStart = juce::jmax (0.0, timelineStart);
    sourceClips.erase (iterator);

    auto& destinationClips = next->tracks[static_cast<size_t> (destinationTrackIndex)].clips;
    destinationClips.push_back (std::move (moved));
    std::stable_sort (destinationClips.begin(), destinationClips.end(), [] (const Clip& a, const Clip& b)
    {
        return a.timelineStart < b.timelineStart;
    });
    publishProject (std::move (next), true);
    return true;
}

std::optional<uint64_t> AudioEngine::duplicateClip (int sourceTrackIndex, uint64_t clipId,
                                                    int destinationTrackIndex, double timelineStart)
{
    if (! validTrack (sourceTrackIndex) || ! validTrack (destinationTrackIndex) || clipId == 0)
        return std::nullopt;

    auto next = editableProject();
    const auto& sourceClips = next->tracks[static_cast<size_t> (sourceTrackIndex)].clips;
    const auto iterator = std::find_if (sourceClips.begin(), sourceClips.end(),
                                        [clipId] (const Clip& clip) { return clip.id == clipId; });
    if (iterator == sourceClips.end())
        return std::nullopt;

    Clip copy = *iterator;
    copy.id = nextClipId.fetch_add (1);
    for (auto& region : copy.spatialRegions)
        region.id = nextSpatialRegionId.fetch_add (1);
    copy.timelineStart = juce::jmax (0.0, timelineStart);
    const auto newId = copy.id;
    auto& destinationClips = next->tracks[static_cast<size_t> (destinationTrackIndex)].clips;
    destinationClips.push_back (std::move (copy));
    std::stable_sort (destinationClips.begin(), destinationClips.end(), [] (const Clip& a, const Clip& b)
    {
        return a.timelineStart < b.timelineStart;
    });
    publishProject (std::move (next), true);
    return newId;
}

bool AudioEngine::removeClip (int trackIndex, uint64_t clipId)
{
    if (! validTrack (trackIndex) || clipId == 0)
        return false;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(),
                                        [clipId] (const Clip& clip) { return clip.id == clipId; });
    if (iterator == clips.end())
        return false;

    clips.erase (iterator);
    publishProject (std::move (next), true);
    return true;
}

bool AudioEngine::addTrack (const juce::String& requestedName)
{
    auto next = editableProject();
    if (next->activeTrackCount >= maximumTrackCount)
        return false;

    const auto index = next->activeTrackCount++;
    auto name = requestedName.trim();
    if (name.isEmpty())
        name = "Track " + juce::String (index + 1);
    next->tracks[static_cast<size_t> (index)] = {};
    next->tracks[static_cast<size_t> (index)].name = name;
    publishProject (std::move (next), true);
    return true;
}

bool AudioEngine::renameTrack (int trackIndex, const juce::String& requestedName)
{
    if (! validTrack (trackIndex))
        return false;

    auto name = requestedName.trim();
    if (name.isEmpty())
        return false;

    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].name = name;
    publishProject (std::move (next), true);
    return true;
}

int AudioEngine::getTrackCount() const noexcept
{
    const auto snapshot = std::atomic_load (&projectState);
    return snapshot != nullptr ? juce::jlimit (0, maximumTrackCount, snapshot->activeTrackCount)
                               : 0;
}

void AudioEngine::clear()
{
    const juce::ScopedLock lock (editLock);
    playing.store (false);
    positionSeconds.store (0.0);
    undoHistory.clear();
    redoHistory.clear();
    auto initialProject = std::make_shared<ProjectState>();
    const std::array<const char*, trackCount> defaultNames {
        "Lead Vocal", "Synth", "Drums", "Atmosphere", "FX Return"
    };
    for (int index = 0; index < trackCount; ++index)
        initialProject->tracks[static_cast<size_t> (index)].name = defaultNames[static_cast<size_t> (index)];
    std::atomic_store (&projectState, ProjectPtr (std::move (initialProject)));
    sendChangeMessage();
}

bool AudioEngine::undo()
{
    const juce::ScopedLock lock (editLock);
    if (undoHistory.empty())
        return false;

    redoHistory.push_back (std::atomic_load (&projectState));
    std::atomic_store (&projectState, undoHistory.back());
    undoHistory.pop_back();
    setPosition (juce::jmin (getPosition(), getLength()));
    sendChangeMessage();
    return true;
}

bool AudioEngine::redo()
{
    const juce::ScopedLock lock (editLock);
    if (redoHistory.empty())
        return false;

    undoHistory.push_back (std::atomic_load (&projectState));
    std::atomic_store (&projectState, redoHistory.back());
    redoHistory.pop_back();
    setPosition (juce::jmin (getPosition(), getLength()));
    sendChangeMessage();
    return true;
}

void AudioEngine::play()
{
    if (! hasFile())
        return;

    if (getPosition() >= getLength())
        positionSeconds.store (0.0);
    playing.store (true);
    sendChangeMessage();
}

void AudioEngine::stop()
{
    playing.store (false);
    sendChangeMessage();
}

void AudioEngine::setPosition (double seconds)
{
    positionSeconds.store (juce::jlimit (0.0, getLength(), seconds));
}

void AudioEngine::setLooping (bool shouldLoop)
{
    looping.store (shouldLoop);
}

void AudioEngine::setPlaybackRate (double rate)
{
    playbackRate.store (juce::jlimit (0.25, 4.0, rate));
}

void AudioEngine::setTrackGainDb (int trackIndex, float gainDb)
{
    if (! validTrack (trackIndex)) return;
    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].gainDb = juce::jlimit (-60.0f, 12.0f, gainDb);
    publishProject (std::move (next), false);
}

void AudioEngine::setTrackPan (int trackIndex, float pan)
{
    if (! validTrack (trackIndex)) return;
    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].pan = juce::jlimit (-1.0f, 1.0f, pan);
    publishProject (std::move (next), false);
}

void AudioEngine::setTrackMuted (int trackIndex, bool muted)
{
    if (! validTrack (trackIndex)) return;
    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].muted = muted;
    publishProject (std::move (next), false);
}

void AudioEngine::setTrackSolo (int trackIndex, bool solo)
{
    if (! validTrack (trackIndex)) return;
    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].solo = solo;
    publishProject (std::move (next), false);
}

void AudioEngine::setTrackSpatialParameters (int trackIndex,
                                              const SpatialParameters& parameters)
{
    if (! validTrack (trackIndex)) return;
    auto next = editableProject();
    next->tracks[static_cast<size_t> (trackIndex)].spatial = sanitiseSpatialParameters (parameters);
    publishProject (std::move (next), false);
}

std::optional<uint64_t> AudioEngine::createClipSpatialRegion (
    int trackIndex, uint64_t clipId, double timelineStart, double timelineEnd,
    const SpatialParameters& parameters)
{
    if (! validTrack (trackIndex) || clipId == 0)
        return std::nullopt;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto clipIterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (clipIterator == clips.end())
        return std::nullopt;

    const auto selectionStart = juce::jlimit (clipIterator->timelineStart,
                                               clipIterator->timelineStart + clipIterator->duration,
                                               juce::jmin (timelineStart, timelineEnd));
    const auto selectionEnd = juce::jlimit (clipIterator->timelineStart,
                                             clipIterator->timelineStart + clipIterator->duration,
                                             juce::jmax (timelineStart, timelineEnd));
    if (selectionEnd - selectionStart < 0.001)
        return std::nullopt;

    const auto newStart = selectionStart - clipIterator->timelineStart;
    const auto newEnd = selectionEnd - clipIterator->timelineStart;
    std::vector<SpatialRegion> retained;
    retained.reserve (clipIterator->spatialRegions.size() + 2);
    for (const auto& existing : clipIterator->spatialRegions)
    {
        const auto existingEnd = existing.startOffset + existing.duration;
        if (existingEnd <= newStart || existing.startOffset >= newEnd)
        {
            retained.push_back (existing);
            continue;
        }

        if (existing.startOffset < newStart)
        {
            auto left = existing;
            left.duration = newStart - existing.startOffset;
            if (left.duration > 0.000001)
                retained.push_back (left);
        }
        if (existingEnd > newEnd)
        {
            auto right = existing;
            right.id = nextSpatialRegionId.fetch_add (1);
            right.startOffset = newEnd;
            right.duration = existingEnd - newEnd;
            if (right.duration > 0.000001)
                retained.push_back (right);
        }
    }

    SpatialRegion region;
    region.id = nextSpatialRegionId.fetch_add (1);
    region.startOffset = newStart;
    region.duration = newEnd - newStart;
    region.spatial = sanitiseSpatialParameters (parameters);
    region.spatial.enabled = true;
    const auto regionId = region.id;
    retained.push_back (region);
    std::sort (retained.begin(), retained.end(), [] (const SpatialRegion& a, const SpatialRegion& b)
    {
        return a.startOffset < b.startOffset;
    });
    clipIterator->spatialRegions = std::move (retained);
    publishProject (std::move (next), true);
    return regionId;
}

bool AudioEngine::setClipSpatialRegionParameters (int trackIndex, uint64_t clipId,
                                                  uint64_t regionId,
                                                  const SpatialParameters& parameters)
{
    if (! validTrack (trackIndex) || clipId == 0 || regionId == 0)
        return false;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto clipIterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (clipIterator == clips.end())
        return false;
    const auto regionIterator = std::find_if (
        clipIterator->spatialRegions.begin(), clipIterator->spatialRegions.end(),
        [regionId] (const SpatialRegion& region) { return region.id == regionId; });
    if (regionIterator == clipIterator->spatialRegions.end())
        return false;

    regionIterator->spatial = sanitiseSpatialParameters (parameters);
    publishProject (std::move (next), false);
    return true;
}

bool AudioEngine::setClipSpatialRegionEnvelope (int trackIndex, uint64_t clipId,
                                                uint64_t regionId, float gainDb,
                                                double transitionSeconds)
{
    if (! validTrack (trackIndex) || clipId == 0 || regionId == 0)
        return false;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto clipIterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (clipIterator == clips.end())
        return false;
    const auto regionIterator = std::find_if (
        clipIterator->spatialRegions.begin(), clipIterator->spatialRegions.end(),
        [regionId] (const SpatialRegion& region) { return region.id == regionId; });
    if (regionIterator == clipIterator->spatialRegions.end())
        return false;

    regionIterator->gainDb = juce::jlimit (-24.0f, 12.0f, gainDb);
    regionIterator->transitionSeconds = juce::jlimit (0.01, 5.0, transitionSeconds);
    publishProject (std::move (next), false);
    return true;
}

bool AudioEngine::removeClipSpatialRegion (int trackIndex, uint64_t clipId, uint64_t regionId)
{
    if (! validTrack (trackIndex) || clipId == 0 || regionId == 0)
        return false;

    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto clipIterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (clipIterator == clips.end())
        return false;
    auto& regions = clipIterator->spatialRegions;
    const auto regionIterator = std::find_if (regions.begin(), regions.end(), [regionId] (const SpatialRegion& region)
    {
        return region.id == regionId;
    });
    if (regionIterator == regions.end())
        return false;

    regions.erase (regionIterator);
    publishProject (std::move (next), true);
    return true;
}

void AudioEngine::setClipGainDb (int trackIndex, uint64_t clipId, float gainDb)
{
    if (! validTrack (trackIndex) || clipId == 0) return;
    auto next = editableProject();
    auto& clips = next->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (iterator == clips.end()) return;
    iterator->gainDb = juce::jlimit (-24.0f, 12.0f, gainDb);
    publishProject (std::move (next), false);
}

void AudioEngine::setMasterGainDb (float gainDb)
{
    auto next = editableProject();
    next->masterGainDb = juce::jlimit (-60.0f, 12.0f, gainDb);
    publishProject (std::move (next), false);
}

juce::Result AudioEngine::exportWav (const juce::File& destination) const
{
    return exportWav (destination, ExportSettings {});
}

juce::Result AudioEngine::exportWav (const juce::File& destination,
                                     const ExportSettings& settings) const
{
    const auto snapshot = std::atomic_load (&projectState);
    return exportProjectWav (snapshot, destination, settings);
}

juce::Result AudioEngine::exportProjectWav (std::shared_ptr<const ProjectState> snapshot,
                                            const juce::File& destination)
{
    return exportProjectWav (std::move (snapshot), destination, ExportSettings {});
}

juce::Result AudioEngine::exportProjectWav (std::shared_ptr<const ProjectState> snapshot,
                                            const juce::File& destination,
                                            const ExportSettings& settings)
{
    if (snapshot == nullptr || snapshot->length <= 0.0)
        return juce::Result::fail ("The project has no audio to export.");
    if (destination == juce::File())
        return juce::Result::fail ("No export destination was selected.");
    if (! isSupportedExportChannelCount (settings.channelCount))
        return juce::Result::fail ("The requested WAV channel layout is not supported.");
    if (settings.sampleRate < 8000.0 || settings.sampleRate > 384000.0)
        return juce::Result::fail ("The requested WAV sample rate is not supported.");

    const auto supportedDepth = settings.bitsPerSample == 16
                             || settings.bitsPerSample == 24
                             || settings.bitsPerSample == 32;
    if (! supportedDepth)
        return juce::Result::fail ("The requested WAV bit depth is not supported.");

    const auto parent = destination.getParentDirectory();
    if (parent == juce::File() || (! parent.isDirectory() && ! parent.createDirectory()))
        return juce::Result::fail ("The export folder could not be created.");

    const auto totalSamples = static_cast<juce::int64> (
        std::ceil (snapshot->length * settings.sampleRate));
    if (totalSamples <= 0)
        return juce::Result::fail ("The project duration is invalid.");

    juce::TemporaryFile temporary (destination);
    std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail ("The temporary WAV file could not be opened.");

    juce::WavAudioFormat format;
    auto writerOptions = juce::AudioFormatWriterOptions()
                             .withSampleRate (settings.sampleRate)
                             .withChannelLayout (exportChannelLayout (settings.channelCount))
                             .withBitsPerSample (settings.bitsPerSample)
                             .withMetadata (juce::WavAudioFormat::riffInfoSoftware, "0i Studio");
    auto writer = format.createWriterFor (stream, writerOptions);
    if (writer == nullptr)
        return juce::Result::fail ("The WAV writer could not use the selected settings.");

    const auto blockSize = juce::jlimit (64, 65536, settings.samplesPerBlock);
    juce::AudioBuffer<float> renderBuffer (settings.channelCount, blockSize);
    auto spatialVoiceStates = std::make_unique<SpatialVoiceStates>();
    juce::int64 renderedSamples = 0;
    bool writeSucceeded = true;

    while (renderedSamples < totalSamples)
    {
        const auto samplesThisBlock = static_cast<int> (
            std::min<juce::int64> (blockSize, totalSamples - renderedSamples));
        renderBuffer.clear();
        renderProjectBlock (*snapshot, renderBuffer, 0, samplesThisBlock,
                            static_cast<double> (renderedSamples) / settings.sampleRate,
                            settings.sampleRate, 1.0, false, *spatialVoiceStates,
                            nullptr, nullptr);
        if (! writer->writeFromAudioSampleBuffer (renderBuffer, 0, samplesThisBlock))
        {
            writeSucceeded = false;
            break;
        }
        renderedSamples += samplesThisBlock;
    }

    writer.reset();
    stream.reset();
    if (! writeSucceeded)
        return juce::Result::fail ("The WAV file could not be written completely.");
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("The completed WAV file could not replace the destination.");

    return juce::Result::ok();
}

bool AudioEngine::hasFile() const noexcept
{
    return getClipCount() > 0;
}

bool AudioEngine::isPlaying() const noexcept
{
    return playing.load();
}

bool AudioEngine::isLooping() const noexcept
{
    return looping.load();
}

bool AudioEngine::canUndo() const
{
    const juce::ScopedLock lock (editLock);
    return ! undoHistory.empty();
}

bool AudioEngine::canRedo() const
{
    const juce::ScopedLock lock (editLock);
    return ! redoHistory.empty();
}

double AudioEngine::getPosition() const noexcept
{
    return positionSeconds.load();
}

double AudioEngine::getLength() const noexcept
{
    return std::atomic_load (&projectState)->length;
}

double AudioEngine::getPlaybackRate() const noexcept
{
    return playbackRate.load();
}

int AudioEngine::getClipCount() const noexcept
{
    auto snapshot = std::atomic_load (&projectState);
    int count = 0;
    for (const auto& track : snapshot->tracks)
        count += static_cast<int> (track.clips.size());
    return count;
}

float AudioEngine::getTrackMeter (int trackIndex) const noexcept
{
    return validTrack (trackIndex) ? trackMeters[static_cast<size_t> (trackIndex)].load() : 0.0f;
}

float AudioEngine::getMasterMeter() const noexcept
{
    return masterMeter.load();
}

SpatialParameters AudioEngine::getTrackSpatialParameters (int trackIndex) const noexcept
{
    if (! validTrack (trackIndex))
        return {};
    return std::atomic_load (&projectState)->tracks[static_cast<size_t> (trackIndex)].spatial;
}

std::optional<AudioEngine::SpatialRegion> AudioEngine::getClipSpatialRegion (
    int trackIndex, uint64_t clipId, uint64_t regionId) const noexcept
{
    if (! validTrack (trackIndex) || clipId == 0 || regionId == 0)
        return std::nullopt;
    const auto snapshot = std::atomic_load (&projectState);
    const auto& clips = snapshot->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto clipIterator = std::find_if (clips.begin(), clips.end(), [clipId] (const Clip& clip)
    {
        return clip.id == clipId;
    });
    if (clipIterator == clips.end())
        return std::nullopt;
    const auto regionIterator = std::find_if (
        clipIterator->spatialRegions.begin(), clipIterator->spatialRegions.end(),
        [regionId] (const SpatialRegion& region) { return region.id == regionId; });
    return regionIterator != clipIterator->spatialRegions.end()
               ? std::optional<SpatialRegion> (*regionIterator)
               : std::nullopt;
}

int AudioEngine::getOutputChannelCount() const noexcept
{
    if (const auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveOutputChannels().countNumberOfSetBits();
    return 2;
}

std::shared_ptr<const AudioEngine::ProjectState> AudioEngine::getProjectSnapshot() const noexcept
{
    return std::atomic_load (&projectState);
}

void AudioEngine::prepareToPlay (int, double sampleRate)
{
    outputSampleRate.store (sampleRate > 0.0 ? sampleRate : 44100.0);
    spatialVoices->fill ({ });
}

void AudioEngine::releaseResources()
{
    spatialVoices->fill ({ });
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& output)
{
    if (output.buffer == nullptr || output.numSamples <= 0)
        return;

    output.clearActiveBufferRegion();
    const auto snapshot = std::atomic_load (&projectState);
    const auto length = snapshot->length;
    if (! playing.load() || length <= 0.0)
    {
        for (auto& meter : trackMeters)
            meter.store (meter.load() * 0.86f);
        masterMeter.store (masterMeter.load() * 0.86f);
        return;
    }

    const auto sampleRate = juce::jmax (1.0, outputSampleRate.load());
    const auto rate = playbackRate.load();
    const auto startPosition = positionSeconds.load();
    const auto shouldLoop = looping.load();
    std::array<float, maximumTrackCount> blockPeaks {};
    float masterPeak = 0.0f;
    renderProjectBlock (*snapshot, *output.buffer, output.startSample, output.numSamples,
                        startPosition, sampleRate, rate, shouldLoop,
                        *spatialVoices,
                        &blockPeaks, &masterPeak);

    for (int trackIndex = 0; trackIndex < snapshot->activeTrackCount; ++trackIndex)
    {
        auto& meter = trackMeters[static_cast<size_t> (trackIndex)];
        meter.store (juce::jmax (blockPeaks[static_cast<size_t> (trackIndex)], meter.load() * 0.82f));
    }
    masterMeter.store (juce::jmax (masterPeak, masterMeter.load() * 0.82f));

    auto nextPosition = startPosition + static_cast<double> (output.numSamples) * rate / sampleRate;
    if (shouldLoop)
        nextPosition = std::fmod (nextPosition, length);
    else if (nextPosition >= length)
    {
        nextPosition = length;
        playing.store (false);
    }
    positionSeconds.store (nextPosition);
}

void AudioEngine::renderProjectBlock (const ProjectState& project,
                                      juce::AudioBuffer<float>& output,
                                      int startSample, int numSamples,
                                      double startPosition, double sampleRate,
                                      double playbackRate, bool looping,
                                      SpatialVoiceStates& voiceStates,
                                      std::array<float, maximumTrackCount>* blockPeaks,
                                      float* masterPeak)
{
    if (numSamples <= 0 || project.length <= 0.0)
        return;

    const auto activeTrackCount = juce::jlimit (0, maximumTrackCount, project.activeTrackCount);
    const auto anySolo = std::any_of (project.tracks.begin(),
                                      project.tracks.begin() + activeTrackCount,
                                      [] (const Track& track) { return track.solo; });
    const auto renderChannels = juce::jmin (output.getNumChannels(), maximumRenderChannels);
    const auto masterGain = juce::Decibels::decibelsToGain (project.masterGainDb);
    const auto voiceFor = [&voiceStates] (int trackIndex, uint64_t clipId) -> SpatialVoiceState&
    {
        for (auto& voice : voiceStates)
            if (voice.clipId == clipId && voice.trackIndex == trackIndex)
                return voice;

        for (auto& voice : voiceStates)
        {
            if (voice.clipId == 0)
            {
                voice.clipId = clipId;
                voice.trackIndex = trackIndex;
                return voice;
            }
        }

        auto& recycled = voiceStates[static_cast<size_t> (
            (clipId + static_cast<uint64_t> (trackIndex * 31)) % voiceStates.size())];
        recycled = {};
        recycled.clipId = clipId;
        recycled.trackIndex = trackIndex;
        return recycled;
    };
    const auto renderPath = [sampleRate, renderChannels] (
        float inputLeft, float inputRight, float gain, float leftPan, float rightPan,
        const SpatialParameters& spatial, double spatialTime,
        auto& filters, auto& filterFlags, auto& delayBuffer, int& delayWriteIndex,
        std::array<float, maximumOutputChannels>& rendered)
    {
        rendered.fill (0.0f);
        if (spatial.enabled)
        {
            renderSpatialChannels (inputLeft * gain, inputRight * gain,
                                   spatial, spatialTime, sampleRate, renderChannels,
                                   rendered, filters, filterFlags,
                                   delayBuffer, delayWriteIndex);
            if (spatial.airAbsorption)
            {
                const auto cutoff = airAbsorptionCutoff (spatial.distance);
                const auto alpha = std::exp (-juce::MathConstants<float>::twoPi * cutoff
                                             / static_cast<float> (sampleRate));
                auto& initialised = filterFlags[0];
                for (int channel = 0; channel < renderChannels; ++channel)
                {
                    if (! initialised)
                        filters[static_cast<size_t> (channel)]
                            = rendered[static_cast<size_t> (channel)];
                    else
                        filters[static_cast<size_t> (channel)]
                            = (1.0f - alpha) * rendered[static_cast<size_t> (channel)]
                            + alpha * filters[static_cast<size_t> (channel)];
                    rendered[static_cast<size_t> (channel)]
                        = filters[static_cast<size_t> (channel)];
                }
                initialised = true;
            }
            else
            {
                filterFlags[0] = false;
            }

            if (renderChannels > 0) rendered[0] *= leftPan;
            if (renderChannels > 1) rendered[1] *= rightPan;
            return;
        }

        filterFlags.fill (false);
        if (renderChannels > 0) rendered[0] = inputLeft * gain * leftPan;
        if (renderChannels > 1) rendered[1] = inputRight * gain * rightPan;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto timelineTime = startPosition
                          + static_cast<double> (sample) * playbackRate / sampleRate;
        if (looping)
            timelineTime = std::fmod (timelineTime, project.length);
        else if (timelineTime >= project.length)
            break;

        std::array<float, maximumOutputChannels> mixedChannels {};
        for (int trackIndex = 0; trackIndex < activeTrackCount; ++trackIndex)
        {
            const auto& track = project.tracks[static_cast<size_t> (trackIndex)];
            if (track.muted || (anySolo && ! track.solo))
                continue;

            float trackPeakLeft = 0.0f;
            float trackPeakRight = 0.0f;
            const auto trackGain = juce::Decibels::decibelsToGain (track.gainDb);
            const auto leftPan = track.pan > 0.0f ? 1.0f - track.pan : 1.0f;
            const auto rightPan = track.pan < 0.0f ? 1.0f + track.pan : 1.0f;
            for (const auto& clip : track.clips)
            {
                if (timelineTime < clip.timelineStart
                    || timelineTime >= clip.timelineStart + clip.duration
                    || clip.source == nullptr || clip.source->samples == nullptr)
                    continue;

                const auto sourceSeconds = clip.sourceOffset + timelineTime - clip.timelineStart;
                const auto sourceSample = sourceSeconds * clip.source->sampleRate;
                const auto& source = *clip.source->samples;
                if (sourceSample < 0.0 || sourceSample >= source.getNumSamples())
                    continue;

                const auto clipGain = juce::Decibels::decibelsToGain (clip.gainDb);
                const auto rightChannel = source.getNumChannels() > 1 ? 1 : 0;
                auto clipLeft = interpolatedSample (source, 0, sourceSample) * clipGain;
                auto clipRight = interpolatedSample (source, rightChannel, sourceSample) * clipGain;

                const auto clipTime = timelineTime - clip.timelineStart;
                const auto regionIterator = std::find_if (
                    clip.spatialRegions.begin(), clip.spatialRegions.end(),
                    [clipTime] (const SpatialRegion& region)
                    {
                        return clipTime >= region.startOffset
                            && clipTime < region.startOffset + region.duration;
                    });
                auto& voice = voiceFor (trackIndex, clip.id);
                std::array<float, maximumOutputChannels> baseChannels {};
                renderPath (clipLeft, clipRight, trackGain, leftPan, rightPan,
                            track.spatial, timelineTime,
                            voice.filters, voice.filterFlags,
                            voice.delayBuffer, voice.delayWriteIndex, baseChannels);

                std::array<float, maximumOutputChannels> renderedChannels = baseChannels;
                if (regionIterator != clip.spatialRegions.end())
                {
                    if (voice.activeRegionId != regionIterator->id)
                    {
                        voice.activeRegionId = regionIterator->id;
                        voice.regionFilters.fill (0.0f);
                        voice.regionFilterFlags.fill (false);
                        voice.regionDelayBuffer.fill (0.0f);
                        voice.regionDelayWriteIndex = 0;
                    }

                    std::array<float, maximumOutputChannels> regionChannels {};
                    const auto regionGain = juce::Decibels::decibelsToGain (
                        regionIterator->gainDb);
                    renderPath (clipLeft, clipRight, trackGain * regionGain,
                                leftPan, rightPan, regionIterator->spatial,
                                timelineTime,
                                voice.regionFilters, voice.regionFilterFlags,
                                voice.regionDelayBuffer, voice.regionDelayWriteIndex,
                                regionChannels);
                    const auto blend = spatialRegionBlend (*regionIterator, clipTime);
                    for (int channel = 0; channel < renderChannels; ++channel)
                        renderedChannels[static_cast<size_t> (channel)] = juce::jmap (
                            blend, baseChannels[static_cast<size_t> (channel)],
                            regionChannels[static_cast<size_t> (channel)]);
                }
                else
                {
                    voice.activeRegionId = 0;
                }

                clipLeft = renderChannels > 0 ? renderedChannels[0] : 0.0f;
                clipRight = renderChannels > 1 ? renderedChannels[1] : 0.0f;
                for (int channel = 0; channel < renderChannels; ++channel)
                    mixedChannels[static_cast<size_t> (channel)]
                        += renderedChannels[static_cast<size_t> (channel)];

                trackPeakLeft = juce::jmax (trackPeakLeft, std::abs (clipLeft));
                trackPeakRight = juce::jmax (trackPeakRight, std::abs (clipRight));
            }

            if (blockPeaks != nullptr)
                (*blockPeaks)[static_cast<size_t> (trackIndex)] = juce::jmax (
                    (*blockPeaks)[static_cast<size_t> (trackIndex)],
                    juce::jmax (trackPeakLeft, trackPeakRight));
        }

        const auto destinationSample = startSample + sample;
        for (int channel = 0; channel < renderChannels; ++channel)
        {
            const auto rendered = std::tanh (mixedChannels[static_cast<size_t> (channel)]
                                             * masterGain);
            if (masterPeak != nullptr)
                *masterPeak = juce::jmax (*masterPeak, std::abs (rendered));
            output.setSample (channel, destinationSample, rendered);
        }
    }
}

bool AudioEngine::isSupportedExportChannelCount (int channelCount) noexcept
{
    return channelCount == 2 || channelCount == 6 || channelCount == 8
        || channelCount == 10 || channelCount == 12;
}

std::shared_ptr<AudioEngine::ProjectState> AudioEngine::editableProject() const
{
    return std::make_shared<ProjectState> (*std::atomic_load (&projectState));
}

void AudioEngine::publishProject (std::shared_ptr<ProjectState> next, bool addUndoPoint)
{
    const juce::ScopedLock lock (editLock);
    updateProjectLength (*next);
    positionSeconds.store (juce::jlimit (0.0, next->length, positionSeconds.load()));
    if (next->length <= 0.0)
        playing.store (false);

    if (addUndoPoint)
    {
        undoHistory.push_back (std::atomic_load (&projectState));
        if (undoHistory.size() > maximumUndoStates)
            undoHistory.erase (undoHistory.begin());
        redoHistory.clear();
    }
    std::atomic_store (&projectState, ProjectPtr (std::move (next)));
    sendChangeMessage();
}

void AudioEngine::updateProjectLength (ProjectState& state)
{
    state.length = 0.0;
    const auto activeTrackCount = juce::jlimit (0, maximumTrackCount, state.activeTrackCount);
    for (int trackIndex = 0; trackIndex < activeTrackCount; ++trackIndex)
        for (const auto& clip : state.tracks[static_cast<size_t> (trackIndex)].clips)
            state.length = juce::jmax (state.length, clip.timelineStart + clip.duration);
}

SpatialParameters AudioEngine::sanitiseSpatialParameters (SpatialParameters parameters) noexcept
{
    parameters.azimuth = wrappedDegrees (parameters.azimuth);
    parameters.elevation = juce::jlimit (-90.0f, 90.0f, parameters.elevation);
    parameters.distance = juce::jlimit (0.5f, 12.0f, parameters.distance);
    parameters.orbitSpeed = juce::jlimit (-360.0f, 360.0f, parameters.orbitSpeed);
    parameters.spread = juce::jlimit (0.0f, 100.0f, parameters.spread);
    parameters.directivity = juce::jlimit (0.0f, 100.0f, parameters.directivity);
    if (static_cast<unsigned int> (parameters.attenuation)
        > static_cast<unsigned int> (SpatialAttenuation::customCurve))
        parameters.attenuation = SpatialAttenuation::inverseSquare;
    return parameters;
}

bool AudioEngine::validTrack (int trackIndex) const noexcept
{
    return trackIndex >= 0 && trackIndex < getTrackCount();
}
} // namespace oi
