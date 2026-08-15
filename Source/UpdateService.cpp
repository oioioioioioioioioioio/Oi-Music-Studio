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
constexpr int maximumDownloadAttempts = 4;

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

bool parseNonNegativeInteger (const juce::String& text, juce::int64& value)
{
    const auto token = text.trim();
    if (token.isEmpty() || token.containsOnly ("0123456789") == false)
        return false;

    value = token.getLargeIntValue();
    return value >= 0;
}

bool isExpectedContentRange (const juce::String& header,
                             juce::int64 requestedOffset,
                             juce::int64 expectedTotalSize)
{
    const auto value = header.trim();
    if (! value.startsWithIgnoreCase ("bytes "))
        return false;

    const auto rangeAndTotal = value.substring (6).trim();
    const auto slash = rangeAndTotal.indexOfChar ('/');
    if (slash <= 0)
        return false;

    const auto range = rangeAndTotal.substring (0, slash).trim();
    const auto totalToken = rangeAndTotal.substring (slash + 1).trim();
    const auto dash = range.indexOfChar ('-');
    if (dash <= 0)
        return false;

    juce::int64 first = 0;
    juce::int64 last = 0;
    if (! parseNonNegativeInteger (range.substring (0, dash), first)
        || ! parseNonNegativeInteger (range.substring (dash + 1), last)
        || first != requestedOffset || last < first)
        return false;

    if (totalToken == "*")
        return expectedTotalSize <= 0;

    juce::int64 total = 0;
    if (! parseNonNegativeInteger (totalToken, total)
        || total <= last
        || (expectedTotalSize > 0 && total != expectedTotalSize))
        return false;

    return true;
}
}

update_detail::DownloadCopyResult update_detail::copyDownloadStream (
    juce::InputStream& input,
    juce::OutputStream& output,
    juce::int64 expectedSize,
    juce::int64 maximumSize)
{
    DownloadCopyResult result;
    std::array<char, 64 * 1024> buffer {};

    for (;;)
    {
        const auto bytesRead = input.read (buffer.data(), static_cast<int> (buffer.size()));
        if (bytesRead < 0)
        {
            // JUCE's Android stream forwards Java InputStream.read(), which returns -1 at EOF.
            const auto reachedExpectedSize = expectedSize > 0
                                          && result.bytesWritten == expectedSize;
            if (input.isExhausted() || reachedExpectedSize)
                break;

            result.error = "The update download was interrupted";
            return result;
        }

        if (bytesRead == 0)
            break;

        result.bytesWritten += bytesRead;
        if (result.bytesWritten > maximumSize)
        {
            result.error = "The update package exceeded the size limit";
            return result;
        }

        if (! output.write (buffer.data(), static_cast<size_t> (bytesRead)))
        {
            result.error = "Could not write the update package";
            return result;
        }
    }

    if (result.bytesWritten == 0
        || (expectedSize > 0 && result.bytesWritten != expectedSize))
        result.error = "The downloaded update package size does not match the release";

    return result;
}

update_detail::ResumeResponseAction update_detail::classifyResumeResponse (
    int statusCode,
    const juce::String& contentRange,
    juce::int64 requestedOffset,
    juce::int64 expectedTotalSize)
{
    if (requestedOffset < 0 || expectedTotalSize < 0)
        return ResumeResponseAction::reject;

    if (requestedOffset == 0)
    {
        if (statusCode == 200)
            return ResumeResponseAction::append;

        return statusCode == 206
                   && isExpectedContentRange (contentRange, 0, expectedTotalSize)
            ? ResumeResponseAction::append : ResumeResponseAction::reject;
    }

    if (statusCode == 200)
        return ResumeResponseAction::restart;

    return statusCode == 206
               && isExpectedContentRange (contentRange, requestedOffset, expectedTotalSize)
        ? ResumeResponseAction::append : ResumeResponseAction::reject;
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
    {
        // Platform-only releases should not advertise an incompatible package.
        result.latestVersion = currentVersion;
        result.releaseUrl = releasesPage();
        result.state = UpdateCheckResult::State::upToDate;
        return result;
    }

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
    packageFile.deleteFile();

    if ((expectedSize > 0 && partialFile.getSize() > expectedSize)
        || partialFile.getSize() > maximumPackageBytes
        || (expectedSize == 0 && partialFile.existsAsFile()))
        partialFile.deleteFile();

    auto fail = [&partialFile, expectedSize] (juce::String message, int attempts)
    {
        UpdateDownloadResult failure;
        const auto downloaded = partialFile.existsAsFile() ? partialFile.getSize() : 0;
        failure.error = std::move (message)
                      + " (downloaded " + juce::String (downloaded)
                      + (expectedSize > 0 ? " of " + juce::String (expectedSize) : juce::String())
                      + " bytes after " + juce::String (attempts)
                      + (attempts == 1 ? " attempt)" : " attempts)");
        return failure;
    };

    juce::String lastError = "The update package download did not complete";
    auto attempts = 0;
    auto downloadComplete = expectedSize > 0 && partialFile.getSize() == expectedSize;

    while (! downloadComplete && attempts < maximumDownloadAttempts)
    {
        ++attempts;
        auto offset = partialFile.existsAsFile() ? partialFile.getSize() : 0;
        if (expectedSize > 0 && offset > expectedSize)
        {
            partialFile.deleteFile();
            offset = 0;
        }

        auto requestHeaders = juce::String ("Accept: application/octet-stream\r\n"
                                            "User-Agent: 0i-Studio-Updater\r\n");
        if (offset > 0)
            requestHeaders += "Range: bytes=" + juce::String (offset) + "-\r\n";

        int statusCode = 0;
        juce::StringPairArray responseHeaders;
        const auto options = juce::URL::InputStreamOptions (
                                 juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (15000)
            .withNumRedirectsToFollow (5)
            .withStatusCode (&statusCode)
            .withResponseHeaders (&responseHeaders)
            .withExtraHeaders (requestHeaders);
        auto input = juce::URL (downloadUrl).createInputStream (options);
        if (input == nullptr)
        {
            lastError = statusCode > 0
                ? "Download failed with HTTP " + juce::String (statusCode)
                : juce::String ("Could not connect to the update server");
            continue;
        }

        const auto responseAction = update_detail::classifyResumeResponse (
            statusCode, responseHeaders.getValue ("Content-Range", {}),
            offset, expectedSize);
        if (responseAction == update_detail::ResumeResponseAction::reject)
        {
            lastError = "The update server returned an invalid resume response (HTTP "
                      + juce::String (statusCode) + ")";
            continue;
        }

        if (responseAction == update_detail::ResumeResponseAction::restart)
        {
            if (! partialFile.deleteFile() && partialFile.existsAsFile())
                return fail ("Could not restart the partial update download", attempts);
            offset = 0;
        }

        const auto reportedSize = input->getTotalLength();
        if (reportedSize > 0 && offset + reportedSize > maximumPackageBytes)
        {
            partialFile.deleteFile();
            return fail ("The update package is too large", attempts);
        }

        auto output = std::make_unique<juce::FileOutputStream> (partialFile);
        if (output->failedToOpen() || ! output->setPosition (offset))
            return fail ("Could not open the partial update package for writing", attempts);

        const auto remainingSize = expectedSize > 0 ? expectedSize - offset : -1;
        const auto remainingLimit = expectedSize > 0 ? remainingSize
                                                     : maximumPackageBytes - offset;
        const auto copyResult = update_detail::copyDownloadStream (
            *input, *output, remainingSize, remainingLimit);
        output->flush();
        const auto outputStatus = output->getStatus();
        output.reset();

        if (outputStatus.failed())
            return fail ("Could not finish writing the update package: "
                         + outputStatus.getErrorMessage(), attempts);

        if (! copyResult.succeeded())
        {
            lastError = copyResult.error;
            if (partialFile.getSize() > maximumPackageBytes
                || (expectedSize > 0 && partialFile.getSize() > expectedSize))
                partialFile.deleteFile();
            continue;
        }

        downloadComplete = partialFile.existsAsFile()
            && partialFile.getSize() > 0
            && (expectedSize <= 0 || partialFile.getSize() == expectedSize);
        if (! downloadComplete)
            lastError = "The downloaded update package size does not match the release";
    }

    if (! downloadComplete)
        return fail (lastError, attempts);

    const auto digest = expectedDigest.trim().toLowerCase();
    if (digest.isNotEmpty())
    {
        if (! digest.startsWith ("sha256:") || digest.length() != 71)
            return fail ("The release uses an unsupported package digest", attempts);

        const auto actualDigest = "sha256:" + juce::SHA256 (partialFile).toHexString();
        if (actualDigest != digest)
        {
            partialFile.deleteFile();
            return fail ("The downloaded update package failed SHA-256 verification", attempts);
        }
    }

    if (! partialFile.moveFileTo (packageFile))
        return fail ("Could not finalize the update package", attempts);

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
