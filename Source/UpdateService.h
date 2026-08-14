#pragma once

#include <JuceHeader.h>

namespace oi
{
struct UpdateCheckResult
{
    enum class State { updateAvailable, upToDate, failed };

    State state = State::failed;
    juce::String latestVersion;
    juce::String downloadUrl;
    juce::String releaseUrl;
    juce::String error;
};

class UpdateService final
{
public:
    [[nodiscard]] static bool isVersionNewer (const juce::String& candidate,
                                               const juce::String& current);
    [[nodiscard]] static UpdateCheckResult checkLatest (const juce::String& currentVersion);
    [[nodiscard]] static juce::String releasesPage();
};
} // namespace oi
