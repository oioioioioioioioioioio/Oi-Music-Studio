#include "UpdateService.h"

#include <array>
#include <vector>

#if JUCE_ANDROID
 #include <jni.h>
 #include <juce_core/native/juce_JNIHelpers_android.h>
#endif

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
constexpr juce::int64 maximumPackageBytes = 512LL * 1024LL * 1024LL;

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

bool isTrustedPackageUrl (const juce::String& url)
{
    if (! isTrustedReleaseUrl (url))
        return false;

    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
   #if JUCE_ANDROID
    return path.endsWithIgnoreCase (".apk");
   #elif JUCE_WINDOWS
    return path.endsWithIgnoreCase (".exe");
   #else
    juce::ignoreUnused (path);
    return false;
   #endif
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
                result.downloadSize = static_cast<juce::int64> (asset->getProperty ("size"));
                result.downloadDigest = asset->getProperty ("digest").toString().trim();
            }
        }
    }

    if (result.downloadUrl.isEmpty())
   #if JUCE_ANDROID
    {
        result.error = "The release does not contain a compatible Android package";
        return result;
    }
   #else
        result.downloadUrl = result.releaseUrl;
   #endif

    result.state = isVersionNewer (result.latestVersion, currentVersion)
                     ? UpdateCheckResult::State::updateAvailable
                     : UpdateCheckResult::State::upToDate;
    return result;
}

UpdateDownloadResult UpdateService::downloadPackage (const juce::String& downloadUrl,
                                                      const juce::String& latestVersion,
                                                      juce::int64 expectedSize,
                                                      const juce::String& expectedDigest)
{
    UpdateDownloadResult result;
    if (! isTrustedPackageUrl (downloadUrl))
    {
        result.error = "The update download URL is not trusted";
        return result;
    }

    if (expectedSize < 0 || expectedSize > maximumPackageBytes)
    {
        result.error = "The update package size is invalid";
        return result;
    }

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs (15000)
        .withNumRedirectsToFollow (5)
        .withStatusCode (&statusCode)
        .withExtraHeaders ("Accept: application/octet-stream\r\n"
                           "User-Agent: 0i-Studio-Updater\r\n");
    auto input = juce::URL (downloadUrl).createInputStream (options);
    if (input == nullptr || statusCode < 200 || statusCode >= 300)
    {
        result.error = statusCode > 0 ? "Download failed with HTTP " + juce::String (statusCode)
                                      : juce::String ("Could not connect to the update server");
        return result;
    }

    const auto reportedSize = input->getTotalLength();
    if (reportedSize > maximumPackageBytes)
    {
        result.error = "The update package is too large";
        return result;
    }

    auto safeVersion = latestVersion.trim().retainCharacters ("0123456789.-");
    if (safeVersion.isEmpty())
        safeVersion = "latest";

    const auto updateDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("updates");
    const auto directoryResult = updateDirectory.createDirectory();
    if (directoryResult.failed())
    {
        result.error = "Could not create the private update directory: "
                     + directoryResult.getErrorMessage();
        return result;
    }

   #if JUCE_ANDROID
    const auto packageName = "0i-Studio-" + safeVersion + "-android.apk";
   #else
    const auto packageName = "0i-Studio-" + safeVersion + "-windows-x64.exe";
   #endif
    const auto packageFile = updateDirectory.getChildFile (packageName);
    const auto partialFile = packageFile.getSiblingFile (packageFile.getFileName() + ".part");
    partialFile.deleteFile();
    packageFile.deleteFile();

    const juce::ScopeGuard removePartialFile { [partialFile] { partialFile.deleteFile(); } };
    auto fail = [] (juce::String message)
    {
        UpdateDownloadResult failure;
        failure.error = std::move (message);
        return failure;
    };

    auto output = partialFile.createOutputStream();
    if (output == nullptr)
        return fail ("Could not create the update package file");

    std::array<char, 64 * 1024> buffer {};
    juce::int64 bytesWritten = 0;
    for (;;)
    {
        const auto bytesRead = input->read (buffer.data(), static_cast<int> (buffer.size()));
        if (bytesRead < 0)
            return fail ("The update download was interrupted");
        if (bytesRead == 0)
            break;

        bytesWritten += bytesRead;
        if (bytesWritten > maximumPackageBytes)
            return fail ("The update package exceeded the size limit");
        if (! output->write (buffer.data(), static_cast<size_t> (bytesRead)))
            return fail ("Could not write the update package");
    }

    output->flush();
    if (output->getStatus().failed())
        return fail ("Could not finish writing the update package: "
                     + output->getStatus().getErrorMessage());
    output.reset();

    if (bytesWritten == 0 || (expectedSize > 0 && bytesWritten != expectedSize))
        return fail ("The downloaded update package size does not match the release");

    const auto digest = expectedDigest.trim().toLowerCase();
    if (digest.isNotEmpty())
    {
        if (! digest.startsWith ("sha256:") || digest.length() != 71)
            return fail ("The release uses an unsupported package digest");

        const auto actualDigest = "sha256:" + juce::SHA256 (partialFile).toHexString();
        if (actualDigest != digest)
            return fail ("The downloaded update package failed SHA-256 verification");
    }

    if (! partialFile.moveFileTo (packageFile))
        return fail ("Could not finalize the update package");

    result.packageFile = packageFile;
    return result;
}

bool UpdateService::launchDownloadedPackage (const juce::File& packageFile,
                                             juce::String& error)
{
   #if JUCE_ANDROID
    if (! packageFile.existsAsFile())
    {
        error = "The downloaded update package no longer exists";
        return false;
    }

    auto activity = juce::getCurrentActivity();
    if (activity == nullptr)
        activity = juce::getMainActivity();
    if (activity == nullptr)
    {
        error = "The Android activity is not available";
        return false;
    }

    auto* env = juce::getEnv();
    juce::LocalRef<jclass> activityClass (
        static_cast<jclass> (env->GetObjectClass (activity.get())));
    const auto method = env->GetMethodID (activityClass.get(), "installDownloadedApk",
                                          "(Ljava/lang/String;)Z");
    if (method == nullptr || env->ExceptionCheck())
    {
        env->ExceptionClear();
        error = "The Android installer bridge is unavailable";
        return false;
    }

    const auto path = juce::javaString (packageFile.getFullPathName());
    const auto started = env->CallBooleanMethod (activity.get(), method, path.get());
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        error = "Android could not open the package installer";
        return false;
    }

    if (started == 0)
    {
        error = "The downloaded package failed Android package, version, or signature validation";
        return false;
    }

    return true;
   #else
    juce::ignoreUnused (packageFile);
    error = "In-app package installation is only available on Android";
    return false;
   #endif
}

juce::String UpdateService::releasesPage()
{
    return releasesUrl;
}
} // namespace oi
