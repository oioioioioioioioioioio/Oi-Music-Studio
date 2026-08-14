#include "UpdateService.h"

#include <vector>

namespace oi
{
namespace
{
constexpr auto latestReleaseApi =
    "https://api.github.com/repos/oioioioioioioioioioio/Oi-Music-Studio/releases/latest";
constexpr auto releasesUrl =
    "https://github.com/oioioioioioioioioioio/Oi-Music-Studio/releases";
constexpr auto trustedDownloadPrefix =
    "https://github.com/oioioioioioioioioioio/Oi-Music-Studio/releases/";

std::vector<int> parseVersion (juce::String version)
{
    version = version.trim();
    if (version.startsWithIgnoreCase ("v"))
        version = version.substring (1);

    const auto suffix = version.indexOfAnyOf ("-+");
    if (suffix >= 0)
        version = version.substring (0, suffix);

    juce::StringArray tokens;
    tokens.addTokens (version, ".", {});

    std::vector<int> parts;
    parts.reserve (static_cast<size_t> (tokens.size()));
    for (const auto& token : tokens)
    {
        const auto digits = token.trim().retainCharacters ("0123456789");
        parts.push_back (digits.isEmpty() ? 0 : digits.getIntValue());
    }

    return parts;
}

int platformAssetScore (const juce::String& assetName)
{
    const auto name = assetName.toLowerCase();

   #if JUCE_ANDROID
    if (! name.endsWith (".apk"))
        return -1;
    auto score = name.contains ("android") ? 200 : 100;
   #elif JUCE_WINDOWS
    if (! name.endsWith (".exe"))
        return -1;
    auto score = name.contains ("windows") ? 200 : 100;
   #else
    juce::ignoreUnused (name);
    return -1;
   #endif

    if (name.startsWith ("0i-studio-"))
        score += 20;
    return score;
}

bool isTrustedReleaseUrl (const juce::String& url)
{
    return url.startsWithIgnoreCase (trustedDownloadPrefix);
}
}

bool UpdateService::isVersionNewer (const juce::String& candidate,
                                    const juce::String& current)
{
    const auto candidateParts = parseVersion (candidate);
    const auto currentParts = parseVersion (current);
    const auto count = juce::jmax (candidateParts.size(), currentParts.size());

    for (size_t index = 0; index < count; ++index)
    {
        const auto candidatePart = index < candidateParts.size() ? candidateParts[index] : 0;
        const auto currentPart = index < currentParts.size() ? currentParts[index] : 0;
        if (candidatePart != currentPart)
            return candidatePart > currentPart;
    }

    return false;
}

UpdateCheckResult UpdateService::checkLatest (const juce::String& currentVersion)
{
    UpdateCheckResult result;
    result.releaseUrl = releasesPage();

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs (8000)
        .withNumRedirectsToFollow (3)
        .withStatusCode (&statusCode)
        .withExtraHeaders ("Accept: application/vnd.github+json\r\n"
                           "X-GitHub-Api-Version: 2022-11-28\r\n"
                           "User-Agent: 0i-Studio-Updater\r\n");
    auto stream = juce::URL (latestReleaseApi).createInputStream (options);
    if (stream == nullptr || statusCode < 200 || statusCode >= 300)
    {
        result.error = statusCode > 0 ? "GitHub HTTP " + juce::String (statusCode)
                                      : juce::String ("Unable to connect to GitHub");
        return result;
    }

    juce::var response;
    const auto parseResult = juce::JSON::parse (stream->readEntireStreamAsString(), response);
    auto* release = response.getDynamicObject();
    if (parseResult.failed() || release == nullptr)
    {
        result.error = parseResult.failed() ? parseResult.getErrorMessage()
                                            : juce::String ("Invalid GitHub response");
        return result;
    }

    result.latestVersion = release->getProperty ("tag_name").toString().trim();
    const auto publishedReleaseUrl = release->getProperty ("html_url").toString();
    if (isTrustedReleaseUrl (publishedReleaseUrl))
        result.releaseUrl = publishedReleaseUrl;

    if (result.latestVersion.isEmpty())
    {
        result.error = "The latest release has no version tag";
        return result;
    }

    auto bestScore = -1;
    if (const auto* assets = release->getProperty ("assets").getArray())
    {
        for (const auto& assetValue : *assets)
        {
            const auto* asset = assetValue.getDynamicObject();
            if (asset == nullptr)
                continue;

            const auto name = asset->getProperty ("name").toString();
            const auto url = asset->getProperty ("browser_download_url").toString();
            const auto score = platformAssetScore (name);
            if (score > bestScore && isTrustedReleaseUrl (url))
            {
                bestScore = score;
                result.downloadUrl = url;
            }
        }
    }

    if (result.downloadUrl.isEmpty())
        result.downloadUrl = result.releaseUrl;

    result.state = isVersionNewer (result.latestVersion, currentVersion)
                     ? UpdateCheckResult::State::updateAvailable
                     : UpdateCheckResult::State::upToDate;
    return result;
}

juce::String UpdateService::releasesPage()
{
    return releasesUrl;
}
} // namespace oi
