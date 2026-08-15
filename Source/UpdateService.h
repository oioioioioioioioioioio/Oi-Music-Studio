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
    juce::int64 downloadSize = 0;
    juce::String downloadDigest;
    juce::String releaseUrl;
    juce::String error;
};

struct UpdateDownloadResult
{
    juce::File packageFile;
    juce::String error;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return packageFile.existsAsFile() && error.isEmpty();
    }
};

namespace update_detail
{
struct DownloadCopyResult
{
    juce::int64 bytesWritten = 0;
    juce::String error;

    [[nodiscard]] bool succeeded() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] DownloadCopyResult copyDownloadStream (juce::InputStream& input,
                                                      juce::OutputStream& output,
                                                      juce::int64 expectedSize,
                                                      juce::int64 maximumSize);
}

class UpdateService final
{
public:
    [[nodiscard]] static bool isVersionNewer (const juce::String& candidate,
                                               const juce::String& current);
    [[nodiscard]] static UpdateCheckResult checkLatest (const juce::String& currentVersion);
    [[nodiscard]] static UpdateDownloadResult downloadPackage (
        const juce::String& downloadUrl,
        const juce::String& latestVersion,
        juce::int64 expectedSize,
        const juce::String& expectedDigest);
    [[nodiscard]] static bool launchDownloadedPackage (const juce::File& packageFile,
                                                       juce::String& error);
    [[nodiscard]] static juce::String releasesPage();
};
} // namespace oi
