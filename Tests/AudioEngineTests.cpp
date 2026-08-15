#include <JuceHeader.h>

#include "../Source/AudioEngine.h"
#include "../Source/UpdateService.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace
{
bool near (float actual, float expected, float tolerance = 0.015f)
{
    return std::abs (actual - expected) <= tolerance;
}

bool writeConstantWave (const juce::File& file, float value, double sampleRate,
                        int sampleCount)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                          .withNumChannels (2)
                                                          .withBitsPerSample (32)
                                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer (2, sampleCount);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        std::fill_n (buffer.getWritePointer (channel), sampleCount, value);
    return writer->writeFromAudioSampleBuffer (buffer, 0, sampleCount);
}

bool writeSineWave (const juce::File& file, float frequency, float gain,
                    double sampleRate, int sampleCount)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                          .withNumChannels (2)
                                                          .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer (2, sampleCount);
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const auto envelope = juce::jmin (1.0f, static_cast<float> (sample) / 480.0f,
                                          static_cast<float> (sampleCount - sample) / 480.0f);
        const auto value = std::sin (juce::MathConstants<double>::twoPi * frequency
                                     * static_cast<double> (sample) / sampleRate)
                         * gain * envelope;
        buffer.setSample (0, sample, static_cast<float> (value));
        buffer.setSample (1, sample, static_cast<float> (value * 0.92));
    }
    return writer->writeFromAudioSampleBuffer (buffer, 0, sampleCount);
}

bool renderBlock (oi::AudioEngine& engine, juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    juce::AudioSourceChannelInfo info (&buffer, 0, buffer.getNumSamples());
    engine.getNextAudioBlock (info);
    return true;
}

juce::AudioChannelSet wave7point1Layout (bool includeHeight)
{
    juce::Array<juce::AudioChannelSet::ChannelType> channels {
        juce::AudioChannelSet::left, juce::AudioChannelSet::right,
        juce::AudioChannelSet::centre, juce::AudioChannelSet::LFE,
        juce::AudioChannelSet::leftSurround, juce::AudioChannelSet::rightSurround,
        juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::rightSurroundSide
    };
    if (includeHeight)
    {
        channels.add (juce::AudioChannelSet::topFrontLeft);
        channels.add (juce::AudioChannelSet::topFrontRight);
        channels.add (juce::AudioChannelSet::topRearLeft);
        channels.add (juce::AudioChannelSet::topRearRight);
    }
    return juce::AudioChannelSet::channelSetWithChannels (channels);
}

class AndroidStyleInputStream final : public juce::InputStream
{
public:
    AndroidStyleInputStream (const juce::String& contents,
                             bool reportsExhaustedAtEnd,
                             int failAfterBytes = -1)
        : data (contents.toRawUTF8(), contents.getNumBytesAsUTF8()),
          exhaustedAtEnd (reportsExhaustedAtEnd),
          failurePosition (failAfterBytes)
    {
    }

    juce::int64 getTotalLength() override
    {
        return static_cast<juce::int64> (data.getSize());
    }

    juce::int64 getPosition() override { return position; }

    bool setPosition (juce::int64 newPosition) override
    {
        if (newPosition < 0 || newPosition > getTotalLength())
            return false;

        position = newPosition;
        exhausted = false;
        return true;
    }

    bool isExhausted() override { return exhausted; }

    int read (void* destination, int maximumBytesToRead) override
    {
        if (failurePosition >= 0 && position >= failurePosition)
        {
            exhausted = false;
            return -1;
        }

        if (position >= getTotalLength())
        {
            exhausted = exhaustedAtEnd;
            return -1;
        }

        auto available = static_cast<int> (getTotalLength() - position);
        if (failurePosition >= 0)
            available = juce::jmin (available,
                                    static_cast<int> (failurePosition - position));

        const auto bytesToRead = juce::jmin (available, maximumBytesToRead);
        if (bytesToRead <= 0)
        {
            exhausted = false;
            return -1;
        }

        std::memcpy (destination,
                     static_cast<const char*> (data.getData()) + position,
                     static_cast<size_t> (bytesToRead));
        position += bytesToRead;
        return bytesToRead;
    }

private:
    juce::MemoryBlock data;
    const bool exhaustedAtEnd;
    const int failurePosition;
    juce::int64 position = 0;
    bool exhausted = false;
};
}

int main (int argc, char* argv[])
{
    constexpr double sampleRate = 48000.0;
    constexpr int sourceSamples = 4800;

    if (argc == 3 && juce::String (argv[1]) == "--write-fixtures")
    {
        const juce::File directory { juce::String (argv[2]) };
        if (! directory.createDirectory())
            return 1;
        const auto first = directory.getChildFile ("lead-tone.wav");
        const auto second = directory.getChildFile ("ambient-tone.wav");
        if (! writeSineWave (first, 220.0f, 0.24f, sampleRate, 144000)
            || ! writeSineWave (second, 330.0f, 0.18f, sampleRate, 192000))
            return 1;
        std::cout << first.getFullPathName() << '\n' << second.getFullPathName() << '\n';
        return 0;
    }

    juce::TemporaryFile firstFile (".wav");
    juce::TemporaryFile secondFile (".wav");
    juce::TemporaryFile spatialFile (".wav");
    juce::TemporaryFile spatialToneFile (".wav");
    juce::TemporaryFile regionExportFile (".wav");
    juce::TemporaryFile stereoExportFile (".wav");
    juce::TemporaryFile surroundExportFile (".wav");
    juce::TemporaryFile sevenOneExportFile (".wav");
    juce::TemporaryFile immersiveExportFile (".wav");

    auto fail = [] (const char* message)
    {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    };

    if (! oi::UpdateService::isVersionNewer ("v0.1.6", "0.1.5")
        || ! oi::UpdateService::isVersionNewer ("1.0.0", "0.9.99")
        || oi::UpdateService::isVersionNewer ("v0.1.5", "0.1.5")
        || oi::UpdateService::isVersionNewer ("0.1.4", "0.1.5"))
        return fail ("online update version comparison is incorrect");

    const juce::String updatePayload = "complete Android update payload";
    const auto updatePayloadSize = static_cast<juce::int64> (updatePayload.getNumBytesAsUTF8());

    AndroidStyleInputStream normalEofStream (updatePayload, true);
    juce::MemoryOutputStream normalEofOutput;
    const auto normalEofResult = oi::update_detail::copyDownloadStream (
        normalEofStream, normalEofOutput, updatePayloadSize, 1024);
    if (! normalEofResult.succeeded()
        || normalEofResult.bytesWritten != updatePayloadSize
        || normalEofOutput.toString() != updatePayload)
        return fail ("Android -1 EOF was treated as an interrupted update download");

    AndroidStyleInputStream expectedSizeStream (updatePayload, false);
    juce::MemoryOutputStream expectedSizeOutput;
    const auto expectedSizeResult = oi::update_detail::copyDownloadStream (
        expectedSizeStream, expectedSizeOutput, updatePayloadSize, 1024);
    if (! expectedSizeResult.succeeded()
        || expectedSizeResult.bytesWritten != updatePayloadSize)
        return fail ("a complete update download did not use the release asset size fallback");

    AndroidStyleInputStream interruptedStream (updatePayload, false, 8);
    juce::MemoryOutputStream interruptedOutput;
    const auto interruptedResult = oi::update_detail::copyDownloadStream (
        interruptedStream, interruptedOutput, updatePayloadSize, 1024);
    if (interruptedResult.succeeded()
        || interruptedResult.error != "The update download was interrupted")
        return fail ("an interrupted update download was accepted as complete");

    if (! writeConstantWave (firstFile.getFile(), 0.20f, sampleRate, sourceSamples)
        || ! writeConstantWave (secondFile.getFile(), 0.30f, sampleRate, sourceSamples)
        || ! writeConstantWave (spatialFile.getFile(), 0.20f, sampleRate,
                                static_cast<int> (sampleRate))
        || ! writeSineWave (spatialToneFile.getFile(), 8000.0f, 0.20f, sampleRate,
                            static_cast<int> (sampleRate)))
        return fail ("could not create source wave files");

    oi::AudioEngine engine (false);
    engine.prepareToPlay (256, sampleRate);
    for (int index = engine.getTrackCount(); index < 4; ++index)
        if (! engine.addTrack())
            return fail ("could not create tracks for multitrack tests");
    const auto firstId = engine.addFileToTrack (firstFile.getFile(), 0, 0.0);
    const auto secondId = engine.addFileToTrack (secondFile.getFile(), 1, 0.0);
    if (! firstId.has_value() || ! secondId.has_value())
        return fail ("could not import both tracks");
    if (engine.getClipCount() != 2 || std::abs (engine.getLength() - 0.1) > 0.0001)
        return fail ("project length or clip count is incorrect");

    juce::AudioBuffer<float> output (2, 256);
    engine.play();
    renderBlock (engine, output);
    if (! near (output.getSample (0, 0), std::tanh (0.5f)))
        return fail ("two-track mix sample is incorrect");

    engine.setTrackMuted (1, true);
    engine.setPosition (0.0);
    renderBlock (engine, output);
    if (! near (output.getSample (0, 0), std::tanh (0.2f)))
        return fail ("track mute did not affect rendering");

    engine.setTrackMuted (1, false);
    engine.setTrackPan (0, 1.0f);
    engine.setTrackMuted (1, true);
    engine.setPosition (0.0);
    renderBlock (engine, output);
    if (! near (output.getSample (0, 0), 0.0f) || ! near (output.getSample (1, 0), std::tanh (0.2f)))
        return fail ("track pan did not route the signal");

    engine.setTrackPan (0, 0.0f);
    engine.setTrackMuted (1, false);
    const auto rightId = engine.splitClipAt (0, *firstId, 0.05);
    if (! rightId.has_value() || engine.getClipCount() != 3)
        return fail ("clip split did not create a right-hand clip");
    engine.setPosition (0.0499);
    renderBlock (engine, output);
    if (! near (output.getSample (0, 0), std::tanh (0.5f)))
        return fail ("split changed audio continuity");

    if (! engine.undo() || engine.getClipCount() != 2)
        return fail ("undo did not restore the pre-split project");
    if (! engine.redo() || engine.getClipCount() != 3)
        return fail ("redo did not restore the split project");

    if (! engine.moveClip (0, *rightId, 2, 0.20))
        return fail ("clip move across tracks failed");
    auto editedProject = engine.getProjectSnapshot();
    if (editedProject->tracks[0].clips.size() != 1
        || editedProject->tracks[2].clips.size() != 1
        || editedProject->tracks[2].clips.front().id != *rightId
        || std::abs (editedProject->tracks[2].clips.front().timelineStart - 0.20) > 0.0001)
        return fail ("clip move did not preserve identity and placement");

    const auto duplicateId = engine.duplicateClip (2, *rightId, 3, 0.30);
    if (! duplicateId.has_value() || *duplicateId == *rightId || engine.getClipCount() != 4)
        return fail ("clip duplication failed");
    editedProject = engine.getProjectSnapshot();
    if (editedProject->tracks[3].clips.size() != 1
        || editedProject->tracks[3].clips.front().source != editedProject->tracks[2].clips.front().source
        || std::abs (editedProject->tracks[3].clips.front().timelineStart - 0.30) > 0.0001)
        return fail ("clip duplicate did not preserve its source and placement");

    engine.setPosition (0.34);
    if (! engine.removeClip (3, *duplicateId) || engine.getClipCount() != 3)
        return fail ("clip deletion failed");
    if (std::abs (engine.getLength() - 0.25) > 0.0001
        || std::abs (engine.getPosition() - engine.getLength()) > 0.0001)
        return fail ("clip deletion did not clamp the playhead to the shortened timeline");
    if (! engine.undo() || engine.getClipCount() != 4)
        return fail ("undo did not restore a deleted clip");
    if (! engine.redo() || engine.getClipCount() != 3)
        return fail ("redo did not remove the restored clip");

    engine.setPosition (0.0);
    engine.setPlaybackRate (2.0);
    engine.play();
    renderBlock (engine, output);
    const auto expectedPosition = 256.0 * 2.0 / sampleRate;
    if (std::abs (engine.getPosition() - expectedPosition) > 0.000001)
        return fail ("playback speed did not advance the playhead correctly");

    oi::AudioEngine regionEngine (false);
    regionEngine.prepareToPlay (256, sampleRate);
    for (int index = regionEngine.getTrackCount(); index < 3; ++index)
        if (! regionEngine.addTrack())
            return fail ("could not create tracks for spatial-region tests");
    const auto regionClipId = regionEngine.addFileToTrack (spatialFile.getFile(), 0, 0.0);
    if (! regionClipId.has_value())
        return fail ("could not import the spatial-region test source");

    oi::SpatialParameters baseRegionParameters;
    baseRegionParameters.enabled = true;
    baseRegionParameters.azimuth = 0.0f;
    baseRegionParameters.distance = 1.0f;
    baseRegionParameters.spread = 0.0f;
    regionEngine.setTrackSpatialParameters (0, baseRegionParameters);

    oi::SpatialParameters regionParameters;
    regionParameters.enabled = true;
    regionParameters.azimuth = 90.0f;
    regionParameters.distance = 1.0f;
    regionParameters.spread = 0.0f;
    const auto regionId = regionEngine.createClipSpatialRegion (
        0, *regionClipId, 0.20, 0.40, regionParameters);
    if (! regionId.has_value())
        return fail ("could not create a clip spatial region");
    auto storedRegion = regionEngine.getClipSpatialRegion (0, *regionClipId, *regionId);
    if (! storedRegion.has_value() || std::abs (storedRegion->startOffset - 0.20) > 0.0001
        || std::abs (storedRegion->duration - 0.20) > 0.0001
        || ! storedRegion->spatial.enabled || ! near (storedRegion->spatial.azimuth, 90.0f, 0.001f)
        || std::abs (storedRegion->gainDb) > 0.0001f
        || std::abs (storedRegion->transitionSeconds
                     - oi::AudioEngine::defaultSpatialRegionTransitionSeconds) > 0.0001)
        return fail ("clip spatial region data was not stored correctly");

    constexpr auto automatedGainDb = -6.0f;
    constexpr auto transitionSeconds = 0.03;
    if (! regionEngine.setClipSpatialRegionEnvelope (
            0, *regionClipId, *regionId, automatedGainDb, transitionSeconds))
        return fail ("could not update clip spatial region gain and transition");
    storedRegion = regionEngine.getClipSpatialRegion (0, *regionClipId, *regionId);
    if (! storedRegion.has_value()
        || std::abs (storedRegion->gainDb - automatedGainDb) > 0.0001f
        || std::abs (storedRegion->transitionSeconds - transitionSeconds) > 0.0001)
        return fail ("clip spatial region envelope was not stored correctly");

    juce::AudioBuffer<float> regionOutput (2, 256);
    auto probeRegion = [&] (double position)
    {
        regionEngine.stop();
        regionEngine.releaseResources();
        regionEngine.prepareToPlay (256, sampleRate);
        regionEngine.setPosition (position);
        regionEngine.play();
        renderBlock (regionEngine, regionOutput);
        constexpr auto probeSample = 192;
        return std::array<float, 2> {
            std::abs (regionOutput.getSample (0, probeSample)),
            std::abs (regionOutput.getSample (1, probeSample))
        };
    };

    const auto outsideRegion = probeRegion (0.10);
    const auto insideReducedRegion = probeRegion (0.30);
    if (std::abs (outsideRegion[0] - outsideRegion[1]) > 0.005f
        || insideReducedRegion[1] <= insideReducedRegion[0] * 1.6f)
        return fail ("clip spatial region did not override the base 3D bed at its centre");

    if (! regionEngine.setClipSpatialRegionEnvelope (
            0, *regionClipId, *regionId, 0.0f, transitionSeconds))
        return fail ("could not reset clip spatial region gain");
    const auto insideUnityRegion = probeRegion (0.30);
    if (insideUnityRegion[1] <= insideReducedRegion[1] * 1.55f)
        return fail ("clip spatial region relative gain did not reach the renderer");
    if (! regionEngine.setClipSpatialRegionEnvelope (
            0, *regionClipId, *regionId, automatedGainDb, transitionSeconds))
        return fail ("could not restore clip spatial region gain");

    juce::AudioBuffer<float> transitionOutput (2, static_cast<int> (sampleRate * 0.26));
    regionEngine.stop();
    regionEngine.releaseResources();
    regionEngine.prepareToPlay (transitionOutput.getNumSamples(), sampleRate);
    regionEngine.setPosition (0.18);
    regionEngine.play();
    renderBlock (regionEngine, transitionOutput);
    const auto boundaryJump = [&transitionOutput, sampleRate] (double boundary)
    {
        const auto boundarySample = juce::roundToInt ((boundary - 0.18) * sampleRate);
        auto maximumJump = 0.0f;
        for (int channel = 0; channel < transitionOutput.getNumChannels(); ++channel)
            for (int sample = boundarySample - 64; sample <= boundarySample + 64; ++sample)
                maximumJump = juce::jmax (
                    maximumJump,
                    std::abs (transitionOutput.getSample (channel, sample)
                              - transitionOutput.getSample (channel, sample - 1)));
        return maximumJump;
    };
    if (boundaryJump (0.20) > 0.01f || boundaryJump (0.40) > 0.01f)
        return fail ("clip spatial automation introduced a discontinuity at a region boundary");

    oi::AudioEngine::ExportSettings regionExportSettings;
    regionExportSettings.sampleRate = sampleRate;
    regionExportSettings.bitsPerSample = 24;
    regionExportSettings.channelCount = 2;
    regionExportSettings.samplesPerBlock = 127;
    if (regionEngine.exportWav (regionExportFile.getFile(), regionExportSettings).failed())
        return fail ("clip spatial region WAV export failed");
    juce::AudioFormatManager regionFormats;
    regionFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> regionReader (
        regionFormats.createReaderFor (regionExportFile.getFile()));
    juce::AudioBuffer<float> exportedRegion (2, 2);
    if (regionReader == nullptr
        || ! regionReader->read (&exportedRegion, 0, 1,
                                 static_cast<juce::int64> (sampleRate * 0.10), true, true)
        || ! regionReader->read (&exportedRegion, 1, 1,
                                 static_cast<juce::int64> (sampleRate * 0.30), true, true)
        || std::abs (exportedRegion.getSample (0, 0) - exportedRegion.getSample (1, 0)) > 0.005f
        || std::abs (exportedRegion.getSample (1, 1))
             <= std::abs (exportedRegion.getSample (0, 1)) * 1.6f)
        return fail ("offline WAV export did not preserve the clip spatial region");

    regionParameters.azimuth = -90.0f;
    if (! regionEngine.setClipSpatialRegionParameters (
            0, *regionClipId, *regionId, regionParameters))
        return fail ("could not update clip spatial region parameters");
    const auto insideLeftRegion = probeRegion (0.30);
    if (insideLeftRegion[0] <= insideLeftRegion[1] * 1.6f)
        return fail ("updated clip spatial region parameters did not reach the renderer");

    const auto duplicateRegionClipId = regionEngine.duplicateClip (0, *regionClipId, 1, 1.20);
    if (! duplicateRegionClipId.has_value())
        return fail ("could not duplicate a clip containing a spatial region");
    auto regionProject = regionEngine.getProjectSnapshot();
    if (regionProject->tracks[1].clips.front().spatialRegions.size() != 1
        || regionProject->tracks[1].clips.front().spatialRegions.front().id == *regionId
        || std::abs (regionProject->tracks[1].clips.front().spatialRegions.front().startOffset - 0.20) > 0.0001
        || std::abs (regionProject->tracks[1].clips.front().spatialRegions.front().gainDb
                     - automatedGainDb) > 0.0001f
        || std::abs (regionProject->tracks[1].clips.front().spatialRegions.front().transitionSeconds
                     - transitionSeconds) > 0.0001)
        return fail ("duplicated clip did not preserve its spatial region with a new identity");

    if (! regionEngine.moveClip (0, *regionClipId, 2, 0.50))
        return fail ("could not move a clip containing a spatial region");
    regionProject = regionEngine.getProjectSnapshot();
    if (regionProject->tracks[2].clips.front().spatialRegions.size() != 1
        || std::abs (regionProject->tracks[2].clips.front().spatialRegions.front().startOffset - 0.20) > 0.0001)
        return fail ("clip spatial region did not follow the moved clip");

    const auto splitRegionClipId = regionEngine.splitClipAt (2, *regionClipId, 0.80);
    if (! splitRegionClipId.has_value())
        return fail ("could not split a clip through its spatial region");
    regionProject = regionEngine.getProjectSnapshot();
    const auto& splitClips = regionProject->tracks[2].clips;
    if (splitClips.size() != 2 || splitClips[0].spatialRegions.size() != 1
        || splitClips[1].spatialRegions.size() != 1
        || std::abs (splitClips[0].spatialRegions.front().duration - 0.10) > 0.0001
        || std::abs (splitClips[1].spatialRegions.front().startOffset) > 0.0001
        || std::abs (splitClips[1].spatialRegions.front().duration - 0.10) > 0.0001
        || std::abs (splitClips[0].spatialRegions.front().gainDb
                     - automatedGainDb) > 0.0001f
        || std::abs (splitClips[1].spatialRegions.front().transitionSeconds
                     - transitionSeconds) > 0.0001)
        return fail ("clip split did not trim and transfer the spatial region correctly");
    const auto leftSplitRegionId = splitClips[0].spatialRegions.front().id;
    if (! regionEngine.removeClipSpatialRegion (2, *regionClipId, leftSplitRegionId)
        || regionEngine.getClipSpatialRegion (2, *regionClipId, leftSplitRegionId).has_value())
        return fail ("could not remove a clip spatial region");

    oi::AudioEngine spatialEngine (false);
    spatialEngine.prepareToPlay (256, sampleRate);
    if (! spatialEngine.addFileToTrack (spatialFile.getFile(), 0, 0.0).has_value())
        return fail ("could not import the spatial test source");

    oi::SpatialParameters spatial;
    spatial.enabled = true;
    spatial.azimuth = 0.0f;
    spatial.elevation = 0.0f;
    spatial.distance = 1.0f;
    spatial.orbitSpeed = 0.0f;
    spatial.spread = 0.0f;
    spatial.airAbsorption = false;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    if (! spatialEngine.getTrackSpatialParameters (0).enabled)
        return fail ("spatial parameters were not stored on the track");

    auto spatialProbe = [&] (const oi::SpatialParameters& parameters, double position)
    {
        spatialEngine.stop();
        spatialEngine.releaseResources();
        spatialEngine.prepareToPlay (256, sampleRate);
        spatialEngine.setTrackSpatialParameters (0, parameters);
        spatialEngine.setPosition (position);
        spatialEngine.play();
        renderBlock (spatialEngine, output);
        constexpr auto probeSample = 192;
        return std::array<float, 2> {
            std::abs (output.getSample (0, probeSample)),
            std::abs (output.getSample (1, probeSample))
        };
    };

    const auto frontProbe = spatialProbe (spatial, 0.0);
    if (std::abs (frontProbe[0] - frontProbe[1]) > 0.005f)
        return fail ("front azimuth was not centred in the stereo image");

    spatial.azimuth = 90.0f;
    const auto rightProbe = spatialProbe (spatial, 0.0);
    const auto rightOnsetNear = std::abs (output.getSample (1, 0));
    const auto rightOnsetFar = std::abs (output.getSample (0, 0));
    if (rightProbe[1] <= rightProbe[0] * 1.6f || rightProbe[0] <= 0.03f)
        return fail ("right azimuth did not apply a continuous near/far-ear level cue");
    if (rightOnsetNear <= 0.10f || rightOnsetFar >= 0.001f)
        return fail ("right azimuth did not apply an interaural time delay");

    spatial.azimuth = -90.0f;
    const auto leftProbe = spatialProbe (spatial, 0.0);
    if (leftProbe[0] <= leftProbe[1] * 1.6f || leftProbe[1] <= 0.03f)
        return fail ("left azimuth did not apply a continuous near/far-ear level cue");

    spatial.azimuth = 180.0f;
    const auto rearProbe = spatialProbe (spatial, 0.0);
    if (std::abs (rearProbe[0] - rearProbe[1]) > 0.005f
        || rearProbe[0] < frontProbe[0] * 0.92f)
        return fail ("fixed-radius orbit did not retain energy at the rear position");

    spatial.azimuth = -90.0f;
    spatial.distance = 4.0f;
    const auto distantProbe = spatialProbe (spatial, 0.0);
    if (distantProbe[0] >= leftProbe[0] * 0.35f || distantProbe[0] <= 0.01f)
        return fail ("spatial distance attenuation did not affect the output");

    oi::AudioEngine spectralEngine (false);
    spectralEngine.prepareToPlay (256, sampleRate);
    if (! spectralEngine.addFileToTrack (spatialToneFile.getFile(), 0, 0.0).has_value())
        return fail ("could not import the binaural spectral test source");
    juce::AudioBuffer<float> spectralOutput (2, 256);
    auto spectralRms = [&] (float azimuth)
    {
        auto parameters = spatial;
        parameters.azimuth = azimuth;
        parameters.distance = 1.0f;
        parameters.orbitSpeed = 0.0f;
        spectralEngine.stop();
        spectralEngine.releaseResources();
        spectralEngine.prepareToPlay (256, sampleRate);
        spectralEngine.setTrackSpatialParameters (0, parameters);
        spectralEngine.setPosition (0.25);
        spectralEngine.play();
        renderBlock (spectralEngine, spectralOutput);
        double energy = 0.0;
        for (int sample = 64; sample < spectralOutput.getNumSamples(); ++sample)
        {
            const auto value = spectralOutput.getSample (0, sample);
            energy += static_cast<double> (value) * value;
        }
        return static_cast<float> (std::sqrt (energy / 192.0));
    };
    const auto frontHighFrequencyRms = spectralRms (0.0f);
    const auto rearHighFrequencyRms = spectralRms (180.0f);
    if (rearHighFrequencyRms >= frontHighFrequencyRms * 0.72f)
        return fail ("rear position did not apply a distinct high-frequency spectral cue");

    spatial.azimuth = 0.0f;
    spatial.distance = 1.0f;
    spatial.orbitSpeed = 360.0f;
    const auto orbitFront = spatialProbe (spatial, 0.0);
    const auto orbitRight = spatialProbe (spatial, 0.25);
    const auto orbitRear = spatialProbe (spatial, 0.5);
    const auto orbitLeft = spatialProbe (spatial, 0.75);
    if (std::abs (orbitFront[0] - orbitFront[1]) > 0.01f
        || orbitRight[1] <= orbitRight[0] * 1.5f
        || std::abs (orbitRear[0] - orbitRear[1]) > 0.01f
        || orbitLeft[0] <= orbitLeft[1] * 1.5f)
        return fail ("360-degree orbit did not cover front, right, rear, and left in one second");

    spatial.orbitSpeed = 720.0f;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    if (! near (spatialEngine.getTrackSpatialParameters (0).orbitSpeed, 360.0f, 0.001f))
        return fail ("spatial orbit speed was not clamped to the supported range");

    spatial.orbitSpeed = 180.0f;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    spatialEngine.setPlaybackRate (2.0);
    spatialEngine.setPosition (0.25);
    spatialEngine.play();
    renderBlock (spatialEngine, output);
    const auto positionBeforeFastOrbitBlock = spatialEngine.getPosition();
    renderBlock (spatialEngine, output);
    const auto positionAdvanced = spatialEngine.getPosition() - positionBeforeFastOrbitBlock;
    if (std::abs (positionAdvanced - 512.0 / sampleRate) > 0.000001)
        return fail ("spatial orbit did not follow the faster timeline clock");

    spatialEngine.setPlaybackRate (1.0);
    spatial.elevation = -120.0f;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    if (! near (spatialEngine.getTrackSpatialParameters (0).elevation, -90.0f, 0.001f))
        return fail ("spatial elevation was not clamped to the full vertical range");

    juce::AudioBuffer<float> surroundOutput (6, 256);
    spatial.azimuth = -110.0f;
    spatial.elevation = 0.0f;
    spatial.orbitSpeed = 0.0f;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    spatialEngine.setPosition (0.0);
    spatialEngine.play();
    renderBlock (spatialEngine, surroundOutput);
    if (std::abs (surroundOutput.getSample (4, 0)) < 0.15f
        || std::abs (surroundOutput.getSample (4, 0)) < std::abs (surroundOutput.getSample (0, 0)) * 4.0f)
        return fail ("5.1 spatial rendering did not route a rear-left object to the surround speaker");

    juce::AudioBuffer<float> immersiveOutput (10, 256);
    spatial.azimuth = -45.0f;
    spatial.elevation = 45.0f;
    spatialEngine.setTrackSpatialParameters (0, spatial);
    spatialEngine.setPosition (0.0);
    spatialEngine.play();
    renderBlock (spatialEngine, immersiveOutput);
    if (std::abs (immersiveOutput.getSample (6, 0)) < 0.15f
        || std::abs (immersiveOutput.getSample (6, 0)) < std::abs (immersiveOutput.getSample (0, 0)) * 2.0f)
        return fail ("5.1.4 spatial rendering did not route an elevated object to the top speaker");

    oi::AudioEngine exportEngine (false);
    exportEngine.prepareToPlay (256, sampleRate);
    if (! exportEngine.addFileToTrack (firstFile.getFile(), 0, 0.0).has_value())
        return fail ("could not import the WAV export source");
    exportEngine.setTrackMuted (0, false);
    exportEngine.setPlaybackRate (2.0);
    exportEngine.setPosition (0.04);
    exportEngine.play();

    oi::AudioEngine::ExportSettings stereoSettings;
    stereoSettings.sampleRate = sampleRate;
    stereoSettings.bitsPerSample = 24;
    stereoSettings.channelCount = 2;
    stereoSettings.samplesPerBlock = 127;
    const auto livePositionBeforeExport = exportEngine.getPosition();
    const auto livePlayingBeforeExport = exportEngine.isPlaying();
    const auto stereoExport = exportEngine.exportWav (stereoExportFile.getFile(), stereoSettings);
    if (stereoExport.failed())
        return fail ("stereo WAV export failed");
    if (exportEngine.getPosition() != livePositionBeforeExport
        || exportEngine.isPlaying() != livePlayingBeforeExport)
        return fail ("offline WAV export changed the live transport state");

    juce::AudioFormatManager exportedFormats;
    exportedFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> stereoReader (
        exportedFormats.createReaderFor (stereoExportFile.getFile()));
    if (stereoReader == nullptr
        || stereoReader->numChannels != 2
        || stereoReader->bitsPerSample != 24
        || std::abs (stereoReader->sampleRate - sampleRate) > 0.1
        || stereoReader->lengthInSamples != sourceSamples)
        return fail ("stereo WAV export metadata is incorrect");
    juce::AudioBuffer<float> exportedStereo (2, 1);
    if (! stereoReader->read (&exportedStereo, 0, 1, 0, true, true)
        || ! near (exportedStereo.getSample (0, 0), std::tanh (0.20f), 0.001f)
        || ! near (exportedStereo.getSample (1, 0), std::tanh (0.20f), 0.001f))
        return fail ("stereo WAV export did not match the project renderer");

    spatial.azimuth = -110.0f;
    spatial.elevation = 0.0f;
    spatial.distance = 1.0f;
    spatial.orbitSpeed = 0.0f;
    spatial.airAbsorption = false;
    exportEngine.setTrackSpatialParameters (0, spatial);
    auto surroundSettings = stereoSettings;
    surroundSettings.channelCount = 6;
    const auto surroundExport = exportEngine.exportWav (surroundExportFile.getFile(), surroundSettings);
    if (surroundExport.failed())
        return fail ("5.1 WAV export failed");

    std::unique_ptr<juce::AudioFormatReader> surroundReader (
        exportedFormats.createReaderFor (surroundExportFile.getFile()));
    if (surroundReader == nullptr || surroundReader->numChannels != 6
        || surroundReader->getChannelLayout() != juce::AudioChannelSet::create5point1())
        return fail ("5.1 WAV export channel metadata is incorrect");
    juce::AudioBuffer<float> exportedSurround (6, 1);
    if (! surroundReader->read (&exportedSurround, 0, 1, 0, true, true)
        || std::abs (exportedSurround.getSample (4, 0)) < 0.15f
        || std::abs (exportedSurround.getSample (4, 0))
             < std::abs (exportedSurround.getSample (0, 0)) * 4.0f)
        return fail ("5.1 WAV export did not preserve spatial routing");

    spatial.azimuth = -150.0f;
    exportEngine.setTrackSpatialParameters (0, spatial);
    auto sevenOneSettings = stereoSettings;
    sevenOneSettings.channelCount = 8;
    const auto sevenOneExport = exportEngine.exportWav (sevenOneExportFile.getFile(), sevenOneSettings);
    if (sevenOneExport.failed())
        return fail ("7.1 WAV export failed");

    std::unique_ptr<juce::AudioFormatReader> sevenOneReader (
        exportedFormats.createReaderFor (sevenOneExportFile.getFile()));
    if (sevenOneReader == nullptr || sevenOneReader->numChannels != 8
        || sevenOneReader->getChannelLayout() != wave7point1Layout (false))
        return fail ("7.1 WAV export channel metadata is incorrect");
    juce::AudioBuffer<float> exportedSevenOne (8, 1);
    if (! sevenOneReader->read (&exportedSevenOne, 0, 1, 0, true, true)
        || std::abs (exportedSevenOne.getSample (4, 0)) < 0.15f
        || std::abs (exportedSevenOne.getSample (4, 0))
             < std::abs (exportedSevenOne.getSample (6, 0)) * 1.5f)
        return fail ("7.1 WAV export did not preserve rear spatial routing");

    spatial.azimuth = -45.0f;
    spatial.elevation = 45.0f;
    exportEngine.setTrackSpatialParameters (0, spatial);
    auto immersiveSettings = stereoSettings;
    immersiveSettings.channelCount = 12;
    const auto immersiveExport = exportEngine.exportWav (immersiveExportFile.getFile(), immersiveSettings);
    if (immersiveExport.failed())
        return fail ("7.1.4 WAV export failed");

    std::unique_ptr<juce::AudioFormatReader> immersiveReader (
        exportedFormats.createReaderFor (immersiveExportFile.getFile()));
    if (immersiveReader == nullptr || immersiveReader->numChannels != 12
        || immersiveReader->bitsPerSample != 24
        || immersiveReader->getChannelLayout() != wave7point1Layout (true))
        return fail ("7.1.4 WAV export channel metadata is incorrect");
    juce::AudioBuffer<float> exportedImmersive (12, 1);
    if (! immersiveReader->read (&exportedImmersive, 0, 1, 0, true, true)
        || std::abs (exportedImmersive.getSample (8, 0)) < 0.15f
        || std::abs (exportedImmersive.getSample (8, 0))
             < std::abs (exportedImmersive.getSample (0, 0)) * 2.0f)
        return fail ("7.1.4 WAV export did not preserve height spatial routing");

    oi::AudioEngine trackEngine (false);
    if (trackEngine.getTrackCount() != oi::AudioEngine::defaultTrackCount)
        return fail ("new projects did not start with the default track count");
    const auto emptyProject = trackEngine.getProjectSnapshot();
    if (trackEngine.hasFile() || trackEngine.getClipCount() != 0
        || emptyProject->tracks[0].name != "Track 1")
        return fail ("new projects did not start with one empty generic track");
    if (! trackEngine.addTrack ("Dialogue")
        || trackEngine.getTrackCount() != oi::AudioEngine::defaultTrackCount + 1)
        return fail ("adding a track did not update the active track count");
    if (trackEngine.getProjectSnapshot()->tracks[oi::AudioEngine::defaultTrackCount].name != "Dialogue")
        return fail ("added track name was not stored in project state");
    if (! trackEngine.renameTrack (oi::AudioEngine::defaultTrackCount, "ADR")
        || trackEngine.getProjectSnapshot()->tracks[oi::AudioEngine::defaultTrackCount].name != "ADR")
        return fail ("renaming a track did not update project state");
    if (! trackEngine.undo()
        || trackEngine.getProjectSnapshot()->tracks[oi::AudioEngine::defaultTrackCount].name != "Dialogue")
        return fail ("track rename was not undoable");

    std::cout << "PASS: multitrack edit/mix, playback speed, spatial rendering, and WAV export\n";
    return 0;
}
