#include "StudioComponents.h"
#include "UpdateService.h"

#include <cmath>

namespace oi
{
namespace
{
enum class MediaImportFailureReason
{
    missingLocalFile,
    invalidDocument,
    cannotOpenDocument,
    cannotCreateImportDirectory,
    cannotCreateDestination,
    copyFailed,
    unsupportedUrl
};

struct MediaImportFailure
{
    juce::String name;
    MediaImportFailureReason reason = MediaImportFailureReason::unsupportedUrl;
    juce::String detail;
};

struct MaterialisedMedia
{
    juce::File file;
    MediaImportFailure failure;
};

juce::String extensionForMimeType (juce::String mimeType)
{
    mimeType = mimeType.toLowerCase().upToFirstOccurrenceOf (";", false, false).trim();

    if (mimeType == "audio/mpeg" || mimeType == "audio/mp3") return ".mp3";
    if (mimeType == "audio/wav" || mimeType == "audio/x-wav" || mimeType == "audio/wave") return ".wav";
    if (mimeType == "audio/flac" || mimeType == "audio/x-flac") return ".flac";
    if (mimeType == "audio/mp4" || mimeType == "audio/x-m4a") return ".m4a";
    if (mimeType == "audio/ogg" || mimeType == "application/ogg") return ".ogg";
    if (mimeType == "audio/aiff" || mimeType == "audio/x-aiff") return ".aiff";

    return {};
}

MaterialisedMedia copyDocumentToImportDirectory (std::unique_ptr<juce::InputStream> input,
                                                  juce::String displayName,
                                                  const juce::String& mimeType)
{
    MaterialisedMedia result;
    result.failure.name = displayName;

    const auto importDirectory = juce::File::getSpecialLocation (
        juce::File::userApplicationDataDirectory).getChildFile ("Imported Media");
    const auto directoryResult = importDirectory.createDirectory();
    if (directoryResult.failed())
    {
        result.failure.reason = MediaImportFailureReason::cannotCreateImportDirectory;
        result.failure.detail = directoryResult.getErrorMessage();
        return result;
    }

    displayName = juce::File::createLegalFileName (displayName.trim());
    if (displayName.isEmpty())
        displayName = "Imported Audio";

    auto requestedFile = importDirectory.getChildFile (displayName);
    if (requestedFile.getFileExtension().isEmpty())
    {
        const auto extension = extensionForMimeType (mimeType);
        if (extension.isNotEmpty())
            requestedFile = requestedFile.withFileExtension (extension);
    }

    const auto destination = importDirectory.getNonexistentChildFile (
        requestedFile.getFileNameWithoutExtension(), requestedFile.getFileExtension(), true);
    juce::TemporaryFile temporary (destination);
    auto output = temporary.getFile().createOutputStream();
    if (output == nullptr || output->failedToOpen())
    {
        result.failure.reason = MediaImportFailureReason::cannotCreateDestination;
        if (output != nullptr)
            result.failure.detail = output->getStatus().getErrorMessage();
        return result;
    }

    const auto bytesWritten = output->writeFromInputStream (*input, -1);
    output->flush();
    const auto writeStatus = output->getStatus();
    output.reset();

    if (bytesWritten <= 0 || writeStatus.failed())
    {
        result.failure.reason = MediaImportFailureReason::copyFailed;
        result.failure.detail = writeStatus.getErrorMessage();
        return result;
    }

    if (! temporary.overwriteTargetFileWithTemporary())
    {
        result.failure.reason = MediaImportFailureReason::copyFailed;
        return result;
    }

    result.file = destination;
    return result;
}

MaterialisedMedia materialiseMediaUrl (const juce::URL& url)
{
    MaterialisedMedia result;
    result.failure.name = url.getFileName();

   #if JUCE_ANDROID
    // A content URI carries a one-document SAF grant. Converting it to File first
    // loses that grant under scoped storage, so read it through AndroidDocument.
    if (url.toString (false).startsWithIgnoreCase ("content:"))
    {
        auto document = juce::AndroidDocument::fromDocument (url);
        juce::String mimeType;
        std::unique_ptr<juce::InputStream> input;

        if (document.hasValue())
        {
            const auto info = document.getInfo();
            if (info.getName().isNotEmpty())
                result.failure.name = info.getName();
            mimeType = info.getType();
            input = document.createInputStream();
        }

        // Some vendor pickers return a readable ContentProvider URI that is not a
        // DocumentsContract URI. WebInputStream goes straight to ContentResolver.
        if (input == nullptr)
        {
            auto contentStream = std::make_unique<juce::WebInputStream> (url, false);
            if (contentStream->connect (nullptr) && ! contentStream->isError())
                input = std::move (contentStream);
        }

        if (input == nullptr)
        {
            result.failure.reason = MediaImportFailureReason::cannotOpenDocument;
            return result;
        }

        return copyDocumentToImportDirectory (std::move (input), result.failure.name,
                                              mimeType);
    }
   #endif

    if (url.isLocalFile())
    {
        result.file = url.getLocalFile();
        if (! result.file.existsAsFile())
        {
            result.failure.reason = MediaImportFailureReason::missingLocalFile;
            result.file = juce::File();
        }
        return result;
    }

    result.failure.reason = MediaImportFailureReason::unsupportedUrl;
    return result;
}

juce::String describeMediaImportFailure (const MediaImportFailure& failure, bool chinese)
{
    auto name = failure.name.trim();
    if (name.isEmpty())
        name = chinese ? juce::String::fromUTF8 ("未命名文件") : juce::String ("Unnamed file");

    juce::String reason;
    switch (failure.reason)
    {
        case MediaImportFailureReason::missingLocalFile:
            reason = chinese ? juce::String::fromUTF8 ("文件不存在或已被移动")
                             : juce::String ("the file is missing or was moved");
            break;
        case MediaImportFailureReason::invalidDocument:
            reason = chinese ? juce::String::fromUTF8 ("系统未授予该文档的读取权限")
                             : juce::String ("the system did not grant access to the document");
            break;
        case MediaImportFailureReason::cannotOpenDocument:
            reason = chinese ? juce::String::fromUTF8 ("无法打开系统文档流")
                             : juce::String ("the system document stream could not be opened");
            break;
        case MediaImportFailureReason::cannotCreateImportDirectory:
            reason = chinese ? juce::String::fromUTF8 ("无法创建应用素材目录")
                             : juce::String ("the app media directory could not be created");
            break;
        case MediaImportFailureReason::cannotCreateDestination:
            reason = chinese ? juce::String::fromUTF8 ("无法创建应用内素材文件")
                             : juce::String ("the app-local media file could not be created");
            break;
        case MediaImportFailureReason::copyFailed:
            reason = chinese ? juce::String::fromUTF8 ("复制音频数据失败")
                             : juce::String ("copying the audio data failed");
            break;
        case MediaImportFailureReason::unsupportedUrl:
            reason = chinese ? juce::String::fromUTF8 ("文件位置不受支持")
                             : juce::String ("the selected file location is unsupported");
            break;
    }

    auto message = juce::String ("\"") + name + "\": " + reason;
    if (failure.detail.isNotEmpty())
        message += " (" + failure.detail + ")";
    return message;
}

const Palette& coloursOf (juce::Component& component)
{
    if (auto* studioLook = dynamic_cast<StudioLookAndFeel*> (&component.getLookAndFeel()))
        return studioLook->colours();

    // Child constructors can request placeholder colours before MainComponent has
    // attached its look-and-feel. Keep that initialization path type-safe.
    static StudioLookAndFeel fallbackLook;
    return fallbackLook.colours();
}

void drawIconGlyph (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto cx = bounds.getCentreX();
    const auto cy = bounds.getCentreY();
    const auto w = bounds.getWidth();
    const auto h = bounds.getHeight();
    juce::Path path;
    g.setColour (colour);

    switch (icon)
    {
        case Icon::play:
            path.addTriangle (bounds.getX() + w * 0.31f, bounds.getY() + h * 0.19f,
                              bounds.getX() + w * 0.31f, bounds.getBottom() - h * 0.19f,
                              bounds.getRight() - w * 0.19f, cy);
            g.fillPath (path);
            break;
        case Icon::pause:
            g.fillRoundedRectangle (bounds.getX() + w * 0.25f, bounds.getY() + h * 0.2f, w * 0.18f, h * 0.6f, 1.0f);
            g.fillRoundedRectangle (bounds.getRight() - w * 0.43f, bounds.getY() + h * 0.2f, w * 0.18f, h * 0.6f, 1.0f);
            break;
        case Icon::stop:
            g.fillRoundedRectangle (bounds.reduced (w * 0.25f), 1.5f);
            break;
        case Icon::record:
            g.fillEllipse (bounds.reduced (w * 0.25f));
            break;
        case Icon::previous:
            g.fillRect (bounds.getX() + w * 0.2f, bounds.getY() + h * 0.2f, 1.6f, h * 0.6f);
            path.addTriangle (bounds.getX() + w * 0.68f, bounds.getY() + h * 0.18f,
                              bounds.getX() + w * 0.68f, bounds.getBottom() - h * 0.18f,
                              bounds.getX() + w * 0.28f, cy);
            g.fillPath (path);
            break;
        case Icon::undo:
        case Icon::redo:
        {
            auto arc = juce::Path();
            arc.addCentredArc (cx, cy, w * 0.3f, h * 0.3f, 0.0f,
                               icon == Icon::undo ? -2.7f : 0.45f,
                               icon == Icon::undo ? 0.45f : 3.55f, true);
            g.strokePath (arc, juce::PathStrokeType (1.6f));
            const auto left = icon == Icon::undo;
            const auto x = left ? bounds.getX() + w * 0.16f : bounds.getRight() - w * 0.16f;
            path.addTriangle (x, cy - h * 0.22f, x, cy + h * 0.08f,
                              left ? x + w * 0.22f : x - w * 0.22f, cy - h * 0.08f);
            g.fillPath (path);
            break;
        }
        case Icon::pointer:
            path.startNewSubPath (bounds.getX() + w * 0.24f, bounds.getY() + h * 0.12f);
            path.lineTo (bounds.getX() + w * 0.73f, cy);
            path.lineTo (cx, cy + h * 0.05f);
            path.lineTo (bounds.getX() + w * 0.66f, bounds.getBottom() - h * 0.16f);
            path.lineTo (bounds.getX() + w * 0.53f, bounds.getBottom() - h * 0.08f);
            path.lineTo (bounds.getX() + w * 0.39f, cy + h * 0.09f);
            path.closeSubPath();
            g.strokePath (path, juce::PathStrokeType (1.5f));
            break;
        case Icon::range:
            g.drawHorizontalLine (juce::roundToInt (cy), bounds.getX() + w * 0.16f, bounds.getRight() - w * 0.16f);
            g.drawVerticalLine (juce::roundToInt (bounds.getX() + w * 0.2f), bounds.getY() + h * 0.22f, bounds.getBottom() - h * 0.22f);
            g.drawVerticalLine (juce::roundToInt (bounds.getRight() - w * 0.2f), bounds.getY() + h * 0.22f, bounds.getBottom() - h * 0.22f);
            break;
        case Icon::scissors:
            g.drawEllipse (bounds.getX() + w * 0.08f, cy - 1.0f, w * 0.28f, h * 0.28f, 1.5f);
            g.drawEllipse (bounds.getX() + w * 0.08f, cy - h * 0.27f, w * 0.28f, h * 0.28f, 1.5f);
            g.drawLine (bounds.getX() + w * 0.32f, cy, bounds.getRight() - w * 0.12f, bounds.getY() + h * 0.2f, 1.5f);
            g.drawLine (bounds.getX() + w * 0.32f, cy, bounds.getRight() - w * 0.12f, bounds.getBottom() - h * 0.2f, 1.5f);
            break;
        case Icon::fade:
            path.startNewSubPath (bounds.getX() + w * 0.12f, bounds.getBottom() - h * 0.16f);
            path.cubicTo (bounds.getX() + w * 0.35f, bounds.getBottom() - h * 0.16f,
                          bounds.getX() + w * 0.52f, bounds.getY() + h * 0.18f,
                          bounds.getRight() - w * 0.12f, bounds.getY() + h * 0.18f);
            g.strokePath (path, juce::PathStrokeType (1.6f));
            break;
        case Icon::magnet:
            path.addRoundedRectangle (bounds.getX() + w * 0.18f, bounds.getY() + h * 0.13f,
                                      w * 0.64f, h * 0.7f, 3.0f, 3.0f, true, true, false, false);
            g.strokePath (path, juce::PathStrokeType (1.7f));
            break;
        case Icon::loop:
            g.drawArrow ({ bounds.getX() + w * 0.18f, cy - h * 0.18f, bounds.getRight() - w * 0.18f, cy - h * 0.18f }, 1.4f, 6.0f, 5.0f);
            g.drawArrow ({ bounds.getRight() - w * 0.18f, cy + h * 0.18f, bounds.getX() + w * 0.18f, cy + h * 0.18f }, 1.4f, 6.0f, 5.0f);
            break;
        case Icon::waveform:
            for (int i = 0; i < 7; ++i)
            {
                const auto x = bounds.getX() + w * (0.14f + i * 0.12f);
                const auto amp = h * (0.18f + 0.08f * static_cast<float> ((i * 3) % 4));
                g.drawLine (x, cy - amp, x, cy + amp, 1.5f);
            }
            break;
        case Icon::orbit:
            g.drawEllipse (bounds.reduced (w * 0.12f, h * 0.28f), 1.3f);
            g.drawEllipse (bounds.reduced (w * 0.28f, h * 0.12f), 1.3f);
            g.fillEllipse (cx + w * 0.22f, cy - h * 0.24f, 4.5f, 4.5f);
            break;
        case Icon::mixer:
            for (int i = 0; i < 3; ++i)
            {
                const auto x = bounds.getX() + w * (0.25f + i * 0.25f);
                g.drawLine (x, bounds.getY() + h * 0.14f, x, bounds.getBottom() - h * 0.14f, 1.3f);
                const auto knobY = bounds.getY() + h * (0.3f + 0.2f * static_cast<float> ((i + 1) % 3));
                g.fillRoundedRectangle (x - 3.0f, knobY - 2.0f, 6.0f, 4.0f, 1.0f);
            }
            break;
        case Icon::panelLeft:
        case Icon::panelRight:
        case Icon::panelBottom:
        case Icon::layout:
            g.drawRoundedRectangle (bounds.reduced (w * 0.12f), 2.0f, 1.4f);
            if (icon == Icon::panelLeft)
                g.drawVerticalLine (juce::roundToInt (bounds.getX() + w * 0.38f), bounds.getY() + h * 0.14f, bounds.getBottom() - h * 0.14f);
            else if (icon == Icon::panelRight)
                g.drawVerticalLine (juce::roundToInt (bounds.getRight() - w * 0.38f), bounds.getY() + h * 0.14f, bounds.getBottom() - h * 0.14f);
            else if (icon == Icon::panelBottom)
                g.drawHorizontalLine (juce::roundToInt (bounds.getBottom() - h * 0.38f), bounds.getX() + w * 0.14f, bounds.getRight() - w * 0.14f);
            else
            {
                g.drawVerticalLine (juce::roundToInt (cx), bounds.getY() + h * 0.14f, bounds.getBottom() - h * 0.14f);
                g.drawHorizontalLine (juce::roundToInt (cy), bounds.getX() + w * 0.14f, bounds.getRight() - w * 0.14f);
            }
            break;
        case Icon::sun:
            g.drawEllipse (bounds.reduced (w * 0.29f), 1.4f);
            for (int i = 0; i < 8; ++i)
            {
                const auto angle = juce::MathConstants<float>::twoPi * i / 8.0f;
                g.drawLine (cx + std::cos (angle) * w * 0.29f, cy + std::sin (angle) * h * 0.29f,
                            cx + std::cos (angle) * w * 0.42f, cy + std::sin (angle) * h * 0.42f, 1.2f);
            }
            break;
        case Icon::moon:
            path.addCentredArc (cx, cy, w * 0.31f, h * 0.31f, 0.0f, 0.3f, 5.6f, true);
            path.cubicTo (cx + w * 0.06f, cy - h * 0.12f, cx + w * 0.06f, cy + h * 0.12f,
                          bounds.getX() + w * 0.38f, bounds.getBottom() - h * 0.18f);
            g.strokePath (path, juce::PathStrokeType (1.5f));
            break;
        case Icon::settings:
            g.drawEllipse (bounds.reduced (w * 0.17f), 1.5f);
            g.drawEllipse (bounds.reduced (w * 0.36f), 1.5f);
            for (int i = 0; i < 6; ++i)
            {
                const auto angle = juce::MathConstants<float>::twoPi * i / 6.0f;
                g.drawLine (cx + std::cos (angle) * w * 0.31f, cy + std::sin (angle) * h * 0.31f,
                            cx + std::cos (angle) * w * 0.43f, cy + std::sin (angle) * h * 0.43f, 2.0f);
            }
            break;
        case Icon::plus:
            g.drawLine (cx, bounds.getY() + h * 0.2f, cx, bounds.getBottom() - h * 0.2f, 1.6f);
            g.drawLine (bounds.getX() + w * 0.2f, cy, bounds.getRight() - w * 0.2f, cy, 1.6f);
            break;
        case Icon::search:
            g.drawEllipse (bounds.getX() + w * 0.13f, bounds.getY() + h * 0.13f, w * 0.5f, h * 0.5f, 1.5f);
            g.drawLine (bounds.getX() + w * 0.56f, bounds.getY() + h * 0.56f,
                        bounds.getRight() - w * 0.12f, bounds.getBottom() - h * 0.12f, 1.5f);
            break;
        case Icon::close:
            g.drawLine (bounds.getX() + w * 0.22f, bounds.getY() + h * 0.22f, bounds.getRight() - w * 0.22f, bounds.getBottom() - h * 0.22f, 1.5f);
            g.drawLine (bounds.getRight() - w * 0.22f, bounds.getY() + h * 0.22f, bounds.getX() + w * 0.22f, bounds.getBottom() - h * 0.22f, 1.5f);
            break;
        case Icon::copy:
            g.drawRoundedRectangle (bounds.getX() + w * 0.28f, bounds.getY() + h * 0.14f,
                                    w * 0.54f, h * 0.58f, 1.5f, 1.4f);
            g.drawRoundedRectangle (bounds.getX() + w * 0.14f, bounds.getY() + h * 0.28f,
                                    w * 0.54f, h * 0.58f, 1.5f, 1.4f);
            break;
        case Icon::trash:
            g.drawLine (bounds.getX() + w * 0.22f, bounds.getY() + h * 0.29f,
                        bounds.getRight() - w * 0.22f, bounds.getY() + h * 0.29f, 1.5f);
            g.drawLine (bounds.getX() + w * 0.38f, bounds.getY() + h * 0.18f,
                        bounds.getRight() - w * 0.38f, bounds.getY() + h * 0.18f, 1.5f);
            g.drawRoundedRectangle (bounds.getX() + w * 0.28f, bounds.getY() + h * 0.34f,
                                    w * 0.44f, h * 0.5f, 1.5f, 1.4f);
            break;
        case Icon::import:
        case Icon::exportFile:
            g.drawRoundedRectangle (bounds.reduced (w * 0.2f, h * 0.12f), 2.0f, 1.3f);
            g.drawArrow ({ cx, icon == Icon::import ? bounds.getY() + h * 0.12f : cy,
                           cx, icon == Icon::import ? cy : bounds.getBottom() - h * 0.08f }, 1.4f, 6.0f, 5.0f);
            break;
        case Icon::folder:
            path.addRoundedRectangle (bounds.getX() + w * 0.1f, bounds.getY() + h * 0.28f, w * 0.8f, h * 0.56f, 2.0f);
            path.addRoundedRectangle (bounds.getX() + w * 0.16f, bounds.getY() + h * 0.16f, w * 0.31f, h * 0.24f, 2.0f);
            g.strokePath (path, juce::PathStrokeType (1.4f));
            break;
        case Icon::music:
            g.drawLine (cx + w * 0.08f, bounds.getY() + h * 0.16f, cx + w * 0.08f, bounds.getBottom() - h * 0.25f, 1.5f);
            g.drawLine (cx + w * 0.08f, bounds.getY() + h * 0.16f, bounds.getRight() - w * 0.12f, bounds.getY() + h * 0.25f, 1.5f);
            g.fillEllipse (bounds.getX() + w * 0.2f, bounds.getBottom() - h * 0.34f, w * 0.32f, h * 0.24f);
            break;
        case Icon::speaker:
            path.addTriangle (bounds.getX() + w * 0.13f, cy - h * 0.15f, bounds.getX() + w * 0.13f, cy + h * 0.15f, cx, cy + h * 0.32f);
            path.lineTo (cx, cy - h * 0.32f);
            path.closeSubPath();
            g.fillPath (path);
            path.clear();
            path.addCentredArc (cx + w * 0.05f, cy, w * 0.3f, h * 0.3f,
                                0.0f, 1.0f, 2.15f, true);
            g.strokePath (path, juce::PathStrokeType (1.5f));
            break;
        case Icon::more:
            g.fillEllipse (cx - w * 0.28f, cy - 1.8f, 3.6f, 3.6f);
            g.fillEllipse (cx - 1.8f, cy - 1.8f, 3.6f, 3.6f);
            g.fillEllipse (cx + w * 0.28f - 3.6f, cy - 1.8f, 3.6f, 3.6f);
            break;
        case Icon::logo:
        case Icon::chevronDown:
            path.addTriangle (bounds.getX() + w * 0.22f, cy - h * 0.1f, bounds.getRight() - w * 0.22f, cy - h * 0.1f, cx, cy + h * 0.18f);
            g.fillPath (path);
            break;
    }
}

void configureTab (juce::TextButton& button)
{
    button.setClickingTogglesState (false);
    button.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
}

juce::String formatTime (double seconds)
{
    const auto totalMillis = static_cast<long long> (std::llround (seconds * 1000.0));
    const auto minutes = totalMillis / 60000;
    const auto secs = (totalMillis / 1000) % 60;
    const auto millis = totalMillis % 1000;
    return juce::String::formatted ("%02lld:%02lld.%03lld", minutes, secs, millis);
}

float wrapDegreesForUi (float degrees) noexcept
{
    auto result = std::fmod (degrees + 180.0f, 360.0f);
    if (result < 0.0f)
        result += 360.0f;
    return result - 180.0f;
}

constexpr bool initialiseAudioDuringConstruction =
   #if JUCE_ANDROID
    false;
   #else
    true;
   #endif
}

IconButton::IconButton (Icon initialIcon, const juce::String& name)
    : juce::Button (name), icon (initialIcon)
{
    setTitle (name);
    setTooltip (name);
    setWantsKeyboardFocus (true);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& c = coloursOf (*this);
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = getToggleState() ? c.accentSoft : juce::Colours::transparentBlack;

    if (getProperties().contains ("accent"))
        fill = c.accent;
    else if (down)
        fill = c.line;
    else if (highlighted)
        fill = c.hover;

    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 3.0f);
    }

    const auto glyphColour = getProperties().contains ("accent") ? c.accentInk
                           : getProperties().contains ("danger") ? c.coral
                           : getToggleState() ? c.accent : c.muted;
    drawIconGlyph (g, icon, bounds.reduced (6.0f), glyphColour);
}

ClipContextToolbar::ClipContextToolbar()
{
    deleteButton.getProperties().set ("danger", true);
    for (auto* button : { &moveButton, &splitButton, &duplicateButton,
                          &spatialButton, &deleteButton })
        addAndMakeVisible (*button);
}

void ClipContextToolbar::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.panel.withAlpha (0.98f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (c.line);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void ClipContextToolbar::resized()
{
    auto area = getLocalBounds().reduced (3);
    const auto buttonWidth = juce::jmax (1, area.getWidth() / 5);
    IconButton* const buttons[] { &moveButton, &splitButton, &duplicateButton,
                                  &spatialButton, &deleteButton };
    for (size_t index = 0; index < std::size (buttons); ++index)
        buttons[index]->setBounds (index + 1 == std::size (buttons)
                                       ? area
                                       : area.removeFromLeft (buttonWidth));
}

PanelHeader::PanelHeader (Localizer& strings, TextId title)
    : localizer (strings), titleId (title)
{
}

void PanelHeader::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.panel);
    g.setColour (c.lineSoft);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
    g.setColour (c.muted);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (localizer.text (titleId), getLocalBounds().reduced (9, 0),
                      juce::Justification::centredLeft, 1);
}

MediaBrowser::MediaBrowser (Localizer& strings, AudioEngine& engine)
    : localizer (strings), audioEngine (engine), header (strings, TextId::media)
{
    audioEngine.addChangeListener (this);
    addAndMakeVisible (header);
    addAndMakeVisible (importButton);
    addAndMakeVisible (directoryButton);
    addAndMakeVisible (refreshButton);
    importButton.onClick = [this] { if (onImportRequested) onImportRequested(); };
    directoryButton.onClick = [this] { if (onDirectoryRequested) onDirectoryRequested(); };
    refreshButton.onClick = [this] { scanLibrary(); };

    search.setMultiLine (false);
    search.setSelectAllWhenFocused (true);
    search.setIndents (9, 0);
    search.onTextChange = [this] { rebuildItems(); };
    addAndMakeVisible (search);

    for (auto* button : { &filesTab, &effectsTab, &presetsTab })
    {
        configureTab (*button);
        addAndMakeVisible (*button);
    }
    filesTab.onClick = [this] { setPage (Page::project); };
    effectsTab.onClick = [this] { setPage (Page::library); };
    presetsTab.onClick = [this] { setPage (Page::presets); };

    itemList.setRowHeight (43);
    itemList.setOutlineThickness (0);
    itemList.setMultipleSelectionEnabled (false);
    itemList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    itemList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (itemList);

    refreshText();
}

MediaBrowser::~MediaBrowser()
{
    audioEngine.removeChangeListener (this);
}

void MediaBrowser::refreshText()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    search.setTextToShowWhenEmpty (localizer.text (TextId::search), coloursOf (*this).faint);
    filesTab.setButtonText (chinese ? juce::String::fromUTF8 ("工程文件") : "Project");
    effectsTab.setButtonText (chinese ? juce::String::fromUTF8 ("本地素材") : "Local");
    presetsTab.setButtonText (chinese ? juce::String::fromUTF8 ("空间预设") : "Spatial");
    importButton.setTitle (localizer.text (TextId::importAudio));
    importButton.setTooltip (localizer.text (TextId::importAudio));
    const auto directoryHelp = chinese ? juce::String::fromUTF8 ("选择本地素材目录")
                                       : juce::String ("Import a media folder");
    directoryButton.setTitle (directoryHelp);
    directoryButton.setTooltip (directoryHelp);
    const auto refreshHelp = chinese ? juce::String::fromUTF8 ("刷新素材与工程文件")
                                     : juce::String ("Refresh imported media");
    refreshButton.setTitle (refreshHelp);
    refreshButton.setTooltip (refreshHelp);
    itemList.setTooltip (chinese
        ? juce::String::fromUTF8 ("单击工程文件可定位剪辑；双击本地素材可导入；双击空间预设可应用。")
        : juce::String ("Drag imported media to the timeline; double-click to add it at the playhead."));
    rebuildItems();
    repaint();
}

void MediaBrowser::refreshContent()
{
    rebuildItems();
}

void MediaBrowser::setLibraryDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;

    libraryDirectory = directory;
    scanLibrary();
    setPage (Page::library);
}

void MediaBrowser::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.panel);
    g.setColour (c.line);
    g.drawVerticalLine (getWidth() - 1, 0.0f, static_cast<float> (getHeight()));

    g.setColour (c.faint);
    g.setFont (juce::FontOptions (9.0f));
    juce::String sectionTitle;
    if (page == Page::project)
        sectionTitle = localizer.text (TextId::projectFiles).toUpperCase();
    else if (page == Page::library)
        sectionTitle = libraryDirectory.isDirectory() ? libraryDirectory.getFullPathName()
                                                       : juce::String ("IMPORTED MEDIA");
    else
        sectionTitle = localizer.getLanguage() == Language::chinese
                           ? juce::String::fromUTF8 ("空间预设") : juce::String ("SPATIAL PRESETS");
    g.drawFittedText (sectionTitle, 9, 108, getWidth() - 47, 18,
                      juce::Justification::centredLeft, 1);
    g.drawText (juce::String (items.size()), getWidth() - 36, 108, 27, 18,
                juce::Justification::centredRight, false);

    if (items.empty())
    {
        const auto emptyText = page == Page::project
            ? (localizer.getLanguage() == Language::chinese
                   ? juce::String::fromUTF8 ("工程中还没有音频") : juce::String ("No audio in this project"))
            : page == Page::library
                ? (localizer.getLanguage() == Language::chinese
                       ? juce::String::fromUTF8 ("目录中没有匹配的音频") : juce::String ("No matching audio files"))
                : (localizer.getLanguage() == Language::chinese
                       ? juce::String::fromUTF8 ("没有匹配的空间预设") : juce::String ("No matching spatial presets"));
        g.setColour (c.faint);
        g.setFont (juce::FontOptions (9.5f));
        g.drawFittedText (emptyText, getLocalBounds().withTrimmedTop (142).reduced (12, 0)
                                            .removeFromTop (46),
                          juce::Justification::centred, 2);
    }
}

void MediaBrowser::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (36));
    importButton.setBounds (getWidth() - 34, 3, 29, 29);
    refreshButton.setBounds (getWidth() - 65, 3, 29, 29);
    directoryButton.setBounds (getWidth() - 96, 3, 29, 29);
    search.setBounds (area.removeFromTop (40).reduced (8, 6));

    auto tabs = area.removeFromTop (30).reduced (7, 0);
    const auto tabWidth = tabs.getWidth() / 3;
    filesTab.setBounds (tabs.removeFromLeft (tabWidth));
    effectsTab.setBounds (tabs.removeFromLeft (tabWidth));
    presetsTab.setBounds (tabs);

    area.removeFromTop (24);
    itemList.setBounds (area);
}

void MediaBrowser::lookAndFeelChanged()
{
    search.applyColourToAllText (coloursOf (*this).text, true);
    repaint();
}

int MediaBrowser::getNumRows()
{
    return static_cast<int> (items.size());
}

juce::String MediaBrowser::getNameForRow (int rowNumber)
{
    return juce::isPositiveAndBelow (rowNumber, static_cast<int> (items.size()))
               ? items[static_cast<size_t> (rowNumber)].title : juce::String();
}

void MediaBrowser::paintListBoxItem (int rowNumber, juce::Graphics& g, int width,
                                     int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, static_cast<int> (items.size())))
        return;

    const auto& c = coloursOf (*this);
    const auto& item = items[static_cast<size_t> (rowNumber)];
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 2);
    if (rowIsSelected)
    {
        g.setColour (c.accentSoft);
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
    }
    else
    {
        g.setColour (c.hover.withAlpha (0.36f));
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
    }

    auto iconBounds = bounds.removeFromLeft (27).toFloat().reduced (5.0f, 10.0f);
    const auto icon = item.kind == ItemKind::projectClip ? Icon::waveform
                    : item.kind == ItemKind::audioFile ? Icon::music : Icon::orbit;
    drawIconGlyph (g, icon, iconBounds, rowIsSelected ? c.accent : c.muted);

    auto text = bounds.reduced (2, 3);
    g.setColour (c.text);
    g.setFont (juce::FontOptions (9.5f));
    g.drawFittedText (item.title, text.removeFromTop (18), juce::Justification::centredLeft, 1);
    g.setColour (c.faint);
    g.setFont (juce::FontOptions (8.0f));
    g.drawFittedText (item.detail, text, juce::Justification::centredLeft, 1);
}

void MediaBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, static_cast<int> (items.size()))
        && items[static_cast<size_t> (row)].kind == ItemKind::projectClip)
        activateItem (row);
}

void MediaBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    activateItem (row);
}

void MediaBrowser::returnKeyPressed (int lastRowSelected)
{
    activateItem (lastRowSelected);
}

void MediaBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildItems();
}

void MediaBrowser::setPage (Page nextPage)
{
    page = nextPage;
    filesTab.setToggleState (page == Page::project, juce::dontSendNotification);
    effectsTab.setToggleState (page == Page::library, juce::dontSendNotification);
    presetsTab.setToggleState (page == Page::presets, juce::dontSendNotification);
    rebuildItems();
}

void MediaBrowser::addImportedFiles (const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
    {
        if (! file.existsAsFile())
            continue;

        const auto existing = std::find_if (libraryFiles.begin(), libraryFiles.end(),
            [&file] (const juce::File& candidate) { return candidate == file; });
        if (existing == libraryFiles.end())
            libraryFiles.push_back (file);
    }

    std::sort (libraryFiles.begin(), libraryFiles.end(), [] (const auto& a, const auto& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    libraryDirectory = juce::File();
    setPage (Page::library);
}

juce::var MediaBrowser::getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe)
{
    for (int index = 0; index < rowsToDescribe.size(); ++index)
    {
        const auto row = rowsToDescribe[index];
        if (! juce::isPositiveAndBelow (row, static_cast<int> (items.size())))
            continue;

        const auto& item = items[static_cast<size_t> (row)];
        if (item.kind == ItemKind::audioFile && item.file.existsAsFile())
            return item.file.getFullPathName();
    }
    return {};
}

void MediaBrowser::scanLibrary()
{
    if (libraryDirectory.isDirectory())
    {
        libraryFiles.clear();
        constexpr auto patterns = "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg";
        for (const auto& entry : juce::RangedDirectoryIterator (
                 libraryDirectory, false, patterns, juce::File::findFiles))
            libraryFiles.push_back (entry.getFile());

        std::sort (libraryFiles.begin(), libraryFiles.end(), [] (const auto& a, const auto& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
    }
    else
    {
        std::erase_if (libraryFiles, [] (const juce::File& file)
        {
            return ! file.existsAsFile();
        });
    }
    rebuildItems();
}

void MediaBrowser::rebuildItems()
{
    items.clear();
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto query = search.getText().trim();
    const auto matches = [&query] (const Item& item)
    {
        return query.isEmpty() || item.title.containsIgnoreCase (query)
            || item.detail.containsIgnoreCase (query);
    };
    const auto append = [this, &matches] (Item item)
    {
        if (matches (item))
            items.push_back (std::move (item));
    };

    if (page == Page::project)
    {
        const auto project = audioEngine.getProjectSnapshot();
        for (int trackIndex = 0; trackIndex < audioEngine.getTrackCount(); ++trackIndex)
        {
            for (const auto& clip : project->tracks[static_cast<size_t> (trackIndex)].clips)
            {
                if (clip.source == nullptr)
                    continue;
                Item item;
                item.kind = ItemKind::projectClip;
                item.title = clip.source->name;
                item.trackIndex = trackIndex;
                item.clipId = clip.id;
                item.file = clip.source->file;
                item.detail = (chinese ? juce::String::fromUTF8 ("轨道 ") : juce::String ("Track "))
                            + juce::String (trackIndex + 1) + " | " + formatTime (clip.timelineStart)
                            + " | " + juce::String (clip.duration, 2) + " s";
                if (! clip.spatialRegions.empty())
                    item.detail << " | 3D " << juce::String (clip.spatialRegions.size());
                append (std::move (item));
            }
        }
    }
    else if (page == Page::library)
    {
        for (const auto& file : libraryFiles)
        {
            Item item;
            item.kind = ItemKind::audioFile;
            item.file = file;
            item.title = file.getFileNameWithoutExtension();
            const auto megabytes = static_cast<double> (file.getSize()) / (1024.0 * 1024.0);
            const auto extension = file.getFileExtension().trimCharactersAtStart (".").toUpperCase();
            item.detail = extension
                        + " | " + juce::String (megabytes, megabytes < 10.0 ? 1 : 0) + " MB";
            if (extension == "M4A")
                item.detail << (chinese ? juce::String::fromUTF8 (" | 需要 AAC 解码器")
                                        : juce::String (" | AAC decoder required"));
            append (std::move (item));
        }
    }
    else
    {
        const auto addPreset = [&append] (juce::String title, juce::String detail,
                                          float azimuth, float elevation, float distance,
                                          float orbitSpeed, float spread, float directivity)
        {
            Item item;
            item.kind = ItemKind::spatialPreset;
            item.title = std::move (title);
            item.detail = std::move (detail);
            item.preset.enabled = true;
            item.preset.azimuth = azimuth;
            item.preset.elevation = elevation;
            item.preset.distance = distance;
            item.preset.orbitSpeed = orbitSpeed;
            item.preset.spread = spread;
            item.preset.directivity = directivity;
            append (std::move (item));
        };
        addPreset (chinese ? juce::String::fromUTF8 ("正前方聚焦") : "Front Focus",
                   chinese ? juce::String::fromUTF8 ("0° | 1.0 m | 静止") : "0 deg | 1.0 m | Static",
                   0.0f, 0.0f, 1.0f, 0.0f, 12.0f, 68.0f);
        addPreset (chinese ? juce::String::fromUTF8 ("宽阔慢速环绕") : "Wide Slow Orbit",
                   chinese ? juce::String::fromUTF8 ("60°/s | 2.0 m | 宽扩散") : "60 deg/s | 2.0 m | Wide",
                   0.0f, 0.0f, 2.0f, 60.0f, 52.0f, 22.0f);
        addPreset (chinese ? juce::String::fromUTF8 ("快速圆周") : "Fast Circle",
                   chinese ? juce::String::fromUTF8 ("180°/s | 1.5 m") : "180 deg/s | 1.5 m",
                   0.0f, 0.0f, 1.5f, 180.0f, 28.0f, 38.0f);
        addPreset (chinese ? juce::String::fromUTF8 ("高空环绕") : "Overhead Orbit",
                   chinese ? juce::String::fromUTF8 ("仰角 +45° | 90°/s") : "Elevation +45 deg | 90 deg/s",
                   0.0f, 45.0f, 2.5f, 90.0f, 36.0f, 30.0f);
        addPreset (chinese ? juce::String::fromUTF8 ("后方氛围") : "Rear Atmosphere",
                   chinese ? juce::String::fromUTF8 ("后方 180° | 4.0 m | 宽扩散") : "Rear 180 deg | 4.0 m | Wide",
                   180.0f, 10.0f, 4.0f, 0.0f, 76.0f, 12.0f);
        addPreset (chinese ? juce::String::fromUTF8 ("近距离人声") : "Close Vocal",
                   chinese ? juce::String::fromUTF8 ("0° | 0.7 m | 高指向") : "0 deg | 0.7 m | Focused",
                   0.0f, 0.0f, 0.7f, 0.0f, 8.0f, 82.0f);
    }

    itemList.updateContent();
    itemList.deselectAllRows();
    repaint();
}

void MediaBrowser::activateItem (int row)
{
    if (! juce::isPositiveAndBelow (row, static_cast<int> (items.size())))
        return;

    const auto& item = items[static_cast<size_t> (row)];
    if (item.kind == ItemKind::projectClip)
    {
        if (onProjectClipSelected) onProjectClipSelected (item.trackIndex, item.clipId);
    }
    else if (item.kind == ItemKind::audioFile)
    {
        if (onAudioFileRequested) onAudioFileRequested (item.file);
    }
    else if (onSpatialPresetSelected)
    {
        onSpatialPresetSelected (item.preset);
    }
}

TimelineComponent::TimelineComponent (Localizer& strings, AppState& appState, AudioEngine& engine)
    : localizer (strings), state (appState), audioEngine (engine),
      tracks {{
          { TextId::leadVocal, "OBJ 01", juce::Colour (0xfff17868), 0.74f, {{ 0.04f, 0.39f }, { 0.48f, 0.35f }} },
          { TextId::synth, "ST", juce::Colour (0xff6b9ff1), 0.57f, {{ 0.00f, 0.29f }, { 0.31f, 0.52f }} },
          { TextId::drums, "ST", juce::Colour (0xff55ca7a), 0.86f, {{ 0.00f, 0.83f }} },
          { TextId::atmosphere, "OBJ 02", juce::Colour (0xffdcb055), 0.42f, {{ 0.08f, 0.75f }} },
          { TextId::fxReturn, "BUS", juce::Colour (0xffb781df), 0.35f, {{ 0.14f, 0.69f }} }
      }}
{
    audioEngine.addChangeListener (this);
    addTrackButton.onClick = [this]
    {
        if (onAddTrackRequested)
            onAddTrackRequested();
    };
    addAndMakeVisible (addTrackButton);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

TimelineComponent::~TimelineComponent()
{
    audioEngine.removeChangeListener (this);
}

void TimelineComponent::refreshText()
{
    const auto addTrackHelp = localizer.getLanguage() == Language::chinese
        ? juce::String::fromUTF8 ("\u65b0\u5efa\u8f68\u9053")
        : juce::String ("Add track");
    addTrackButton.setTitle (addTrackHelp);
    addTrackButton.setTooltip (addTrackHelp);
    addTrackButton.setEnabled (audioEngine.getTrackCount() < AudioEngine::maximumTrackCount);
    setTooltip (localizer.getLanguage() == Language::chinese
        ? juce::String::fromUTF8 ("\u7a7a\u683c\uff1a\u64ad\u653e/\u6682\u505c  |  Ctrl+\u9f20\u6807\u6eda\u8f6e\uff1a\u7f29\u653e  |  Alt+\u9f20\u6807\u6eda\u8f6e\uff1a\u6c34\u5e73\u6eda\u52a8")
        : juce::String ("Space: play/pause  |  Ctrl+wheel: zoom  |  Alt+wheel: horizontal scroll"));
    repaint();
}

juce::Rectangle<int> TimelineComponent::timelineArea() const
{
    return getLocalBounds().withTrimmedLeft (trackHeaderWidth()).withTrimmedTop (rulerHeight());
}

int TimelineComponent::trackHeaderWidth() const noexcept
{
    const auto landscapeTouch = getWidth() >= 600 && getHeight() < 520;
    return landscapeTouch ? 100 : getWidth() < 600 ? 88 : 126;
}

int TimelineComponent::rulerHeight() const noexcept
{
    const auto landscapeTouch = getWidth() >= 600 && getHeight() < 520;
    return landscapeTouch ? 30 : 36;
}

double TimelineComponent::visibleLength() const noexcept
{
    const auto projectLength = audioEngine.getLength();
    const auto baseLength = projectLength > 0.0
        ? juce::jmax (30.0, std::ceil (projectLength / 30.0) * 30.0)
        : 120.0;
    return juce::jmax (8.0, baseLength / timelineZoom);
}

int TimelineComponent::trackHeight() const noexcept
{
    const auto preferred = state.workspace == Workspace::mix
        ? juce::jmax (42, static_cast<int> (state.layout.trackHeight * 0.72f))
        : state.workspace == Workspace::spatial
            ? juce::jmax (44, static_cast<int> (state.layout.trackHeight * 0.78f))
            : state.layout.trackHeight;
    const auto touchPreferred = getWidth() < 600 || getHeight() < 360
                                  ? juce::jmax (72, preferred) : preferred;
    const auto count = visibleTrackCount();
    if (count <= 0 || getHeight() <= rulerHeight())
        return touchPreferred;
    return juce::jmin (touchPreferred, juce::jmax (30, (getHeight() - rulerHeight()) / count));
}

int TimelineComponent::visibleTrackCount() const noexcept
{
    const auto available = audioEngine.getTrackCount();
    const auto workspaceLimit = state.workspace == Workspace::mix ? 3
                              : state.workspace == Workspace::spatial ? 4
                              : AudioEngine::maximumTrackCount;
    const auto availableHeight = juce::jmax (0, getHeight() - rulerHeight());
    const auto heightLimit = juce::jmax (1, availableHeight / 40);
    return juce::jmin (available, workspaceLimit, heightLimit);
}

double TimelineComponent::timeAtX (float x) const noexcept
{
    const auto headerWidth = static_cast<float> (trackHeaderWidth());
    const auto laneWidth = juce::jmax (1.0f, static_cast<float> (getWidth()) - headerWidth);
    const auto normal = juce::jlimit (0.0f, 1.0f, (x - headerWidth) / laneWidth);
    return timelineViewStart + static_cast<double> (normal) * visibleLength();
}

float TimelineComponent::xAtTime (double seconds) const noexcept
{
    const auto headerWidth = static_cast<float> (trackHeaderWidth());
    const auto laneWidth = juce::jmax (1.0f, static_cast<float> (getWidth()) - headerWidth);
    return headerWidth + laneWidth * static_cast<float> ((seconds - timelineViewStart)
                                                               / visibleLength());
}

int TimelineComponent::trackAtY (float y) const noexcept
{
    const auto index = static_cast<int> ((y - static_cast<float> (rulerHeight()))
                                         / static_cast<float> (trackHeight()));
    return index >= 0 && index < visibleTrackCount() ? index : -1;
}

std::optional<AudioEngine::Clip> TimelineComponent::clipAt (int trackIndex,
                                                            double timelineSeconds) const
{
    if (trackIndex < 0 || trackIndex >= audioEngine.getTrackCount())
        return std::nullopt;

    const auto project = audioEngine.getProjectSnapshot();
    const auto& clips = project->tracks[static_cast<size_t> (trackIndex)].clips;
    for (auto iterator = clips.rbegin(); iterator != clips.rend(); ++iterator)
        if (timelineSeconds >= iterator->timelineStart
            && timelineSeconds <= iterator->timelineStart + iterator->duration)
            return *iterator;
    return std::nullopt;
}

juce::Rectangle<float> TimelineComponent::spatialRegionBounds (
    const AudioEngine::Clip& clip, const AudioEngine::SpatialRegion& region,
    int trackIndex) const
{
    const auto rowHeight = trackHeight();
    auto clipBounds = juce::Rectangle<float> (
        xAtTime (clip.timelineStart),
        static_cast<float> (rulerHeight() + trackIndex * rowHeight + 7),
        juce::jmax (12.0f, xAtTime (clip.timelineStart + clip.duration)
                              - xAtTime (clip.timelineStart)),
        static_cast<float> (rowHeight - 14));
    const auto regionStart = clip.timelineStart + region.startOffset;
    return juce::Rectangle<float> (
        xAtTime (regionStart), clipBounds.getY() + 1.0f,
        juce::jmax (2.0f, xAtTime (regionStart + region.duration) - xAtTime (regionStart)),
        clipBounds.getHeight() - 2.0f).getIntersection (clipBounds.reduced (1.0f));
}

juce::Rectangle<float> TimelineComponent::spatialRegionDeleteBounds (
    const AudioEngine::Clip& clip, const AudioEngine::SpatialRegion& region,
    int trackIndex) const
{
    const auto regionBounds = spatialRegionBounds (clip, region, trackIndex);
    if (regionBounds.getWidth() < 24.0f)
        return {};

    return juce::Rectangle<float> (16.0f, 16.0f)
        .withPosition (regionBounds.getRight() - 18.0f, regionBounds.getY() + 2.0f);
}

void TimelineComponent::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.background);
    const auto headerHeight = rulerHeight();
    const auto headerWidth = trackHeaderWidth();

    g.setColour (c.panel);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (c.line);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, static_cast<float> (getWidth()));
    g.drawVerticalLine (headerWidth - 1, 0.0f, static_cast<float> (getHeight()));

    g.setColour (c.muted);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (localizer.text (TextId::tracks), 10, 0, headerWidth - 50, headerHeight,
                juce::Justification::centredLeft, true);

    const auto rulerLength = visibleLength();
    for (size_t i = 0; i < 5; ++i)
    {
        const auto normal = static_cast<float> (i) / 4.0f;
        const auto x = headerWidth + normal * (getWidth() - headerWidth - 20) + 8.0f;
        const auto rulerSeconds = juce::roundToInt (timelineViewStart + rulerLength * normal);
        const auto rulerLabel = juce::String::formatted ("%02d:%02d", rulerSeconds / 60,
                                                         rulerSeconds % 60);
        g.setColour (c.faint);
        g.setFont (juce::FontOptions (8.5f));
        g.drawText (rulerLabel, juce::roundToInt (x - 20.0f), 4, 40, 14,
                    juce::Justification::centred, false);
    }

    const auto visibleTrackCount = this->visibleTrackCount();
    const auto trackHeight = this->trackHeight();
    const auto compactTrackHeader = trackHeight < 54;
    const auto project = audioEngine.getProjectSnapshot();
    const auto timelineLength = visibleLength();

    for (int index = 0; index < visibleTrackCount; ++index)
    {
        const auto y = headerHeight + index * trackHeight;
        auto trackBounds = juce::Rectangle<int> (0, y, getWidth(), trackHeight);
        auto header = trackBounds.removeFromLeft (headerWidth);
        auto& track = tracks[static_cast<size_t> (index)];
        const auto& engineTrack = project->tracks[static_cast<size_t> (index)];
        if (track.colour == juce::Colour())
            track.colour = juce::Colour::fromHSV (std::fmod (0.08f + index * 0.113f, 1.0f),
                                                  0.55f, 0.92f, 1.0f);

        g.setColour (index == state.selectedTrack ? c.raised : c.panel);
        g.fillRect (header);
        g.setColour (track.colour);
        g.fillRoundedRectangle (0.0f, static_cast<float> (y + 6), 4.0f,
                                static_cast<float> (trackHeight - 12), 1.5f);

        g.setColour (c.text);
        g.setFont (juce::FontOptions (compactTrackHeader ? 9.5f : 10.5f));
        const auto trackName = engineTrack.name.isNotEmpty() ? engineTrack.name
                                                              : localizer.text (track.name);
        g.drawFittedText (trackName,
                          header.withTrimmedLeft (12).withTrimmedRight (34)
                                .removeFromTop (compactTrackHeader ? 20 : 29),
                          juce::Justification::centredLeft, 1);
        g.setColour (c.faint);
        g.setFont (juce::FontOptions (8.0f));
        if (! compactTrackHeader && headerWidth >= 120)
            g.drawText (track.role, header.getRight() - 36, header.getY() + 6, 30, 15,
                        juce::Justification::centredRight, false);

        const auto buttonY = y + trackHeight - (compactTrackHeader ? 20 : 24);
        const auto buttonHeight = compactTrackHeader ? 15.0f : 17.0f;
        const std::array<juce::String, 3> smallButtons { "R", "M", "S" };
        for (size_t i = 0; i < smallButtons.size(); ++i)
        {
            auto small = juce::Rectangle<float> (12.0f + static_cast<float> (i) * 23.0f,
                                                  static_cast<float> (buttonY), 19.0f,
                                                  buttonHeight);
            const auto active = (i == 1 && engineTrack.muted)
                             || (i == 2 && engineTrack.solo);
            g.setColour (active ? c.accent : c.background);
            g.fillRoundedRectangle (small, 2.0f);
            g.setColour (active ? c.accentInk : c.line);
            g.drawRoundedRectangle (small, 2.0f, 1.0f);
            g.setColour (active ? c.accentInk : c.muted);
            g.setFont (juce::FontOptions (8.0f));
            g.drawText (smallButtons[i], small.toNearestInt(), juce::Justification::centred, false);
        }

        const auto meterWidth = headerWidth < 120 ? 26.0f : 33.0f;
        auto meter = juce::Rectangle<float> (static_cast<float> (headerWidth) - meterWidth - 6.0f,
                                             static_cast<float> (buttonY + 7), meterWidth, 3.0f);
        g.setColour (c.hover);
        g.fillRoundedRectangle (meter, 1.5f);
        const auto meterLevel = audioEngine.hasFile() ? audioEngine.getTrackMeter (index)
                                                      : track.level;
        g.setColour (c.green);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth()
                                                  * juce::jlimit (0.0f, 1.0f, meterLevel)),
                                1.5f);

        g.setColour (c.lineSoft);
        g.drawHorizontalLine (y + trackHeight - 1, 0.0f, static_cast<float> (getWidth()));

        const auto lane = trackBounds.toFloat();
        for (int grid = 1; grid < 8; ++grid)
        {
            const auto x = lane.getX() + lane.getWidth() * grid / 8.0f;
            g.setColour (c.lineSoft.withAlpha (0.8f));
            g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
        }

        const auto& projectClips = engineTrack.clips;
        if (audioEngine.hasFile())
        {
            for (size_t clipIndex = 0; clipIndex < projectClips.size(); ++clipIndex)
            {
                const auto& projectClip = projectClips[clipIndex];
                if (draggingClip && projectClip.id == draggedClipId && dragTargetTrack != index)
                    continue;
                const auto isDragged = draggingClip && projectClip.id == draggedClipId;
                paintClip (g, projectClip, index, lane, track.colour, timelineLength, isDragged);
            }
        }
        else
        {
            for (size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex)
            {
                const auto [left, width] = track.clips[clipIndex];
                auto clip = juce::Rectangle<float> (lane.getX() + lane.getWidth() * left,
                                                    lane.getY() + 7.0f,
                                                    lane.getWidth() * width,
                                                    lane.getHeight() - 14.0f);
                g.setColour (track.colour.withAlpha (0.2f).overlaidWith (c.raised.withAlpha (0.75f)));
                g.fillRoundedRectangle (clip, 3.0f);
                g.setColour (track.colour.withAlpha (0.72f));
                g.drawRoundedRectangle (clip, 3.0f, 1.0f);
                drawWaveform (g, clip.reduced (3.0f).withTrimmedTop (13.0f), track.colour,
                              index * 7 + static_cast<int> (clipIndex));
                const auto* clipNameUtf8 = index == 0 ? (clipIndex == 0 ? "Lead Vox · Verse" : "Lead Vox · Chorus")
                                         : index == 1 ? "Glass Keys · Main"
                                         : index == 2 ? "Neon Kit · 96 BPM"
                                         : index == 3 ? "City Rain · Wide" : "Spatial Reverb Print";
                const auto clipName = juce::String::fromUTF8 (clipNameUtf8);
                g.setColour (c.text);
                g.setFont (juce::FontOptions (8.5f));
                g.drawFittedText (clipName, clip.toNearestInt().withTrimmedLeft (6).removeFromTop (16),
                                  juce::Justification::centredLeft, 1);
            }
        }

        if (index == 0)
        {
            juce::Path automation;
            const auto top = lane.getY() + lane.getHeight() * 0.58f;
            automation.startNewSubPath (lane.getX(), top + 10.0f);
            automation.cubicTo (lane.getX() + lane.getWidth() * 0.2f, top + 10.0f,
                                lane.getX() + lane.getWidth() * 0.28f, top - 12.0f,
                                lane.getX() + lane.getWidth() * 0.45f, top - 4.0f);
            automation.cubicTo (lane.getX() + lane.getWidth() * 0.62f, top + 15.0f,
                                lane.getX() + lane.getWidth() * 0.73f, top - 12.0f,
                                lane.getRight(), top - 7.0f);
            g.setColour (c.yellow);
            g.strokePath (automation, juce::PathStrokeType (1.4f));
        }
    }

    if (draggingClip && dragTargetTrack >= 0 && dragTargetTrack != dragSourceTrack)
    {
        const auto& sourceClips = project->tracks[static_cast<size_t> (dragSourceTrack)].clips;
        const auto iterator = std::find_if (sourceClips.begin(), sourceClips.end(), [this] (const AudioEngine::Clip& clip)
        {
            return clip.id == draggedClipId;
        });
        if (iterator != sourceClips.end())
        {
            const auto destinationLane = juce::Rectangle<float> (
                static_cast<float> (headerWidth),
                static_cast<float> (headerHeight + dragTargetTrack * trackHeight),
                static_cast<float> (getWidth() - headerWidth), static_cast<float> (trackHeight));
            paintClip (g, *iterator, dragTargetTrack, destinationLane,
                       tracks[static_cast<size_t> (dragTargetTrack)].colour,
                       timelineLength, true);
        }
    }

    if (dragDropTrack >= 0 && dragDropTrack < visibleTrackCount)
    {
        const auto y = headerHeight + dragDropTrack * trackHeight;
        g.setColour (c.accentSoft.withAlpha (0.55f));
        g.fillRect (headerWidth, y, getWidth() - headerWidth, trackHeight);
        const auto dropX = xAtTime (dragDropTime);
        g.setColour (c.accent);
        g.drawVerticalLine (juce::roundToInt (dropX), static_cast<float> (y + 3),
                            static_cast<float> (y + trackHeight - 3));
    }

    if (snapGuideTime >= timelineViewStart - 0.001
        && snapGuideTime <= timelineViewStart + timelineLength + 0.001)
    {
        const auto snapX = xAtTime (snapGuideTime);
        g.setColour (c.accent.withAlpha (0.78f));
        g.drawVerticalLine (juce::roundToInt (snapX), headerHeight,
                            static_cast<float> (headerHeight + visibleTrackCount * trackHeight));
        g.setFont (juce::FontOptions (8.0f));
        g.drawText ("SNAP", juce::roundToInt (snapX + 4.0f), headerHeight + 2,
                    42, 14, juce::Justification::centredLeft, false);
    }

    const auto playheadX = xAtTime (audioEngine.getPosition());
    g.setColour (c.coral);
    g.drawVerticalLine (juce::roundToInt (playheadX), 0.0f,
                        static_cast<float> (headerHeight + visibleTrackCount * trackHeight));
    juce::Path marker;
    marker.addTriangle (playheadX - 4.0f, 0.0f, playheadX + 4.0f, 0.0f, playheadX, 7.0f);
    g.fillPath (marker);
}

void TimelineComponent::paintClip (juce::Graphics& g, const AudioEngine::Clip& projectClip,
                                   int trackIndex, juce::Rectangle<float> lane, juce::Colour trackColour,
                                   double timelineLength, bool dragged)
{
    const auto& c = coloursOf (*this);
    const auto displayStart = dragged ? dragPreviewStart : projectClip.timelineStart;
    const auto left = static_cast<float> (displayStart / timelineLength);
    const auto width = static_cast<float> (projectClip.duration / timelineLength);
    auto clip = juce::Rectangle<float> (lane.getX() + lane.getWidth() * left,
                                        lane.getY() + 7.0f,
                                        juce::jmax (12.0f, lane.getWidth() * width),
                                        lane.getHeight() - 14.0f);
    const auto selected = state.selectedClipId == projectClip.id;
    const auto hovered = hoveredClipId == projectClip.id;
    g.setColour (trackColour.withAlpha (dragged ? 0.28f : 0.2f)
                              .overlaidWith (c.raised.withAlpha (0.75f)));
    g.fillRoundedRectangle (clip, 3.0f);
    g.setColour (selected ? c.accent : hovered ? trackColour : trackColour.withAlpha (0.72f));
    g.drawRoundedRectangle (clip, 3.0f, selected ? 2.0f : hovered ? 1.5f : 1.0f);
    drawClipThumbnail (g, clip.reduced (3.0f).withTrimmedTop (13.0f), projectClip);

    for (const auto& region : projectClip.spatialRegions)
    {
        const auto regionStart = displayStart + region.startOffset;
        auto regionBounds = juce::Rectangle<float> (
            lane.getX() + lane.getWidth() * static_cast<float> (regionStart / timelineLength),
            clip.getY() + 1.0f,
            juce::jmax (2.0f, lane.getWidth() * static_cast<float> (region.duration / timelineLength)),
            clip.getHeight() - 2.0f).getIntersection (clip.reduced (1.0f));
        const auto regionSelected = region.id == state.selectedSpatialRegionId;
        g.setColour (c.accent.withAlpha (regionSelected ? 0.28f : 0.16f));
        g.fillRoundedRectangle (regionBounds, 2.0f);
        g.setColour (regionSelected ? c.accent : c.accent.withAlpha (0.7f));
        g.drawRoundedRectangle (regionBounds, 2.0f, regionSelected ? 2.0f : 1.0f);

        const auto transitionSeconds = juce::jmin (region.transitionSeconds,
                                                    region.duration * 0.5);
        const auto transitionWidth = juce::jmin (
            regionBounds.getWidth() * 0.5f,
            lane.getWidth() * static_cast<float> (transitionSeconds / timelineLength));
        if (transitionWidth >= 1.0f && regionBounds.getHeight() >= 12.0f)
        {
            const auto lowY = regionBounds.getBottom() - 4.0f;
            const auto highY = regionBounds.getY() + 5.0f;
            const auto leftRampEnd = regionBounds.getX() + transitionWidth;
            const auto rightRampStart = regionBounds.getRight() - transitionWidth;
            juce::Path envelope;
            envelope.startNewSubPath (regionBounds.getX(), lowY);
            envelope.cubicTo (regionBounds.getX() + transitionWidth * 0.35f, lowY,
                              leftRampEnd - transitionWidth * 0.35f, highY,
                              leftRampEnd, highY);
            envelope.lineTo (rightRampStart, highY);
            envelope.cubicTo (rightRampStart + transitionWidth * 0.35f, highY,
                              regionBounds.getRight() - transitionWidth * 0.35f, lowY,
                              regionBounds.getRight(), lowY);
            g.setColour (c.accent.withAlpha (regionSelected ? 0.95f : 0.58f));
            g.strokePath (envelope, juce::PathStrokeType (1.15f));
        }

        if (regionBounds.getWidth() >= 30.0f)
        {
            g.setFont (juce::FontOptions (8.0f));
            auto label = juce::String ("3D");
            if (regionBounds.getWidth() >= 72.0f)
            {
                label << "  " << (region.gainDb >= 0.0f ? "+" : "")
                      << juce::String (region.gainDb, 1) << " dB";
            }
            auto labelBounds = regionBounds.toNearestInt().reduced (4, 1);
            if (regionSelected)
                labelBounds.removeFromRight (18);
            g.drawFittedText (label, labelBounds, juce::Justification::bottomLeft, 1);
        }
        if (regionSelected)
        {
            const auto remove = spatialRegionDeleteBounds (projectClip, region, trackIndex);
            if (! remove.isEmpty())
            {
                g.setColour (c.background.withAlpha (0.82f));
                g.fillRoundedRectangle (remove, 2.0f);
                g.setColour (c.text);
                g.drawLine (remove.getX() + 4.5f, remove.getY() + 4.5f,
                            remove.getRight() - 4.5f, remove.getBottom() - 4.5f, 1.2f);
                g.drawLine (remove.getRight() - 4.5f, remove.getY() + 4.5f,
                            remove.getX() + 4.5f, remove.getBottom() - 4.5f, 1.2f);
            }
        }
    }

    if (draggingRange && projectClip.id == state.selectedClipId
        && rangePreviewEnd > rangePreviewStart)
    {
        auto preview = juce::Rectangle<float> (
            lane.getX() + lane.getWidth() * static_cast<float> (rangePreviewStart / timelineLength),
            clip.getY() + 1.0f,
            juce::jmax (2.0f, lane.getWidth()
                                  * static_cast<float> ((rangePreviewEnd - rangePreviewStart)
                                                        / timelineLength)),
            clip.getHeight() - 2.0f).getIntersection (clip.reduced (1.0f));
        g.setColour (c.accent.withAlpha (0.24f));
        g.fillRoundedRectangle (preview, 2.0f);
        g.setColour (c.accent);
        g.drawRoundedRectangle (preview, 2.0f, 2.0f);
    }
    g.setColour (c.text);
    g.setFont (juce::FontOptions (8.5f));
    g.drawFittedText (projectClip.source != nullptr ? projectClip.source->name : "Audio",
                      clip.toNearestInt().withTrimmedLeft (6).withTrimmedRight (selected ? 24 : 2)
                          .removeFromTop (16),
                      juce::Justification::centredLeft, 1);
    if (selected && state.selectedSpatialRegionId == 0 && clip.getWidth() >= 46.0f)
    {
        auto close = clip.withSizeKeepingCentre (18.0f, 18.0f)
                         .withX (clip.getRight() - 21.0f).withY (clip.getY() + 3.0f);
        g.setColour (c.background.withAlpha (0.72f));
        g.fillRoundedRectangle (close, 2.0f);
        g.setColour (c.text);
        g.drawLine (close.getX() + 5.0f, close.getY() + 5.0f,
                    close.getRight() - 5.0f, close.getBottom() - 5.0f, 1.2f);
        g.drawLine (close.getRight() - 5.0f, close.getY() + 5.0f,
                    close.getX() + 5.0f, close.getBottom() - 5.0f, 1.2f);
    }
}

void TimelineComponent::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                                      juce::Colour colour, int seed) const
{
    if (area.isEmpty())
        return;

    juce::Path path;
    path.startNewSubPath (area.getX(), area.getCentreY());
    const auto columns = juce::jmax (12, juce::roundToInt (area.getWidth() / 4.0f));
    for (int i = 0; i <= columns; ++i)
    {
        const auto phase = static_cast<float> ((i * 37 + seed * 19) % 97) / 97.0f;
        const auto harmonic = std::abs (std::sin ((i + seed * 0.6f) * 0.71f));
        const auto amplitude = area.getHeight() * (0.12f + 0.34f * phase + 0.18f * harmonic);
        const auto x = area.getX() + area.getWidth() * i / static_cast<float> (columns);
        path.lineTo (x, area.getCentreY() - amplitude);
        path.lineTo (x, area.getCentreY() + amplitude);
    }
    g.setColour (colour.withAlpha (0.78f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void TimelineComponent::drawClipThumbnail (juce::Graphics& g, juce::Rectangle<float> area,
                                           const AudioEngine::Clip& clip) const
{
    if (clip.source == nullptr || clip.source->thumbnail == nullptr
        || clip.source->thumbnail->getTotalLength() <= 0.0)
        return;
    clip.source->thumbnail->drawChannels (g, area.toNearestInt(), clip.sourceOffset,
                                          clip.sourceOffset + clip.duration, 0.82f);
}

void TimelineComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto headerWidth = trackHeaderWidth();
    const auto ruler = rulerHeight();
    draggingPlayhead = false;
    draggingClip = false;
    draggingRange = false;
    clipDragMoved = false;
    rangeDragMoved = false;
    rangeHitRegionId = 0;
    grabKeyboardFocus();

    if (event.y < ruler)
    {
        if (event.x >= headerWidth && ! event.mods.isPopupMenu())
        {
            draggingPlayhead = true;
            setPlayheadFromX (event.position.x);
        }
        return;
    }

    const auto rowHeight = trackHeight();
    const auto trackIndex = trackAtY (event.position.y);
    if (trackIndex < 0)
        return;

    state.selectedTrack = trackIndex;
    if (onTrackSelected) onTrackSelected (trackIndex);

    if (event.x < headerWidth)
    {
        selectClip (trackIndex, 0);
        const auto buttonY = ruler + trackIndex * rowHeight + rowHeight - 24;

        if (event.mods.isPopupMenu())
        {
            if (onRenameTrackRequested)
                onRenameTrackRequested (trackIndex);
            return;
        }

        if (event.getNumberOfClicks() >= 2 && event.y < buttonY)
        {
            if (onRenameTrackRequested)
                onRenameTrackRequested (trackIndex);
            return;
        }

        if (event.y >= buttonY && event.y < buttonY + 17)
        {
            const auto project = audioEngine.getProjectSnapshot();
            const auto& track = project->tracks[static_cast<size_t> (trackIndex)];
            if (event.x >= 35 && event.x < 54)
            {
                audioEngine.setTrackMuted (trackIndex, ! track.muted);
                if (onTrackControlsChanged) onTrackControlsChanged();
            }
            else if (event.x >= 58 && event.x < 77)
            {
                audioEngine.setTrackSolo (trackIndex, ! track.solo);
                if (onTrackControlsChanged) onTrackControlsChanged();
            }
        }

        repaint();
        return;
    }

    const auto clickedTime = timeAtX (event.position.x);
    const auto clickedClip = clipAt (trackIndex, clickedTime);
    if (! clickedClip.has_value())
    {
        selectClip (trackIndex, 0);
        repaint();
        return;
    }

    const auto clickedClipId = clickedClip->id;
    const auto wasSelected = state.selectedClipId == clickedClipId;
    const auto selectedRegionBeforeClick = state.selectedSpatialRegionId;
    if (! event.mods.isPopupMenu() && state.activeTool == Tool::range && wasSelected
        && selectedRegionBeforeClick != 0)
    {
        const auto selectedRegion = std::find_if (
            clickedClip->spatialRegions.begin(), clickedClip->spatialRegions.end(),
            [selectedRegionBeforeClick] (const AudioEngine::SpatialRegion& region)
            {
                return region.id == selectedRegionBeforeClick;
            });
        if (selectedRegion != clickedClip->spatialRegions.end()
            && spatialRegionDeleteBounds (*clickedClip, *selectedRegion, trackIndex)
                   .contains (event.position))
        {
            deleteSelectedSpatialRegion();
            return;
        }
    }
    selectClip (trackIndex, clickedClipId);

    if (event.mods.isPopupMenu())
    {
        showClipMenu (event.getPosition(), trackIndex, clickedClipId, clickedTime);
        repaint();
        return;
    }

    const auto clipRight = juce::jmax (xAtTime (clickedClip->timelineStart) + 12.0f,
                                       xAtTime (clickedClip->timelineStart + clickedClip->duration));
    const auto laneTop = static_cast<float> (ruler + trackIndex * rowHeight + 7);
    if (wasSelected && selectedRegionBeforeClick == 0 && event.position.x >= clipRight - 24.0f
        && event.position.x <= clipRight && event.position.y <= laneTop + 23.0f)
    {
        deleteSelectedClip();
        return;
    }

    if (state.activeTool == Tool::split)
    {
        if (const auto rightClip = audioEngine.splitClipAt (trackIndex, clickedClipId, clickedTime))
            selectClip (trackIndex, *rightClip);
    }
    else if (state.activeTool == Tool::select)
    {
        draggingClip = true;
        dragSourceTrack = trackIndex;
        dragTargetTrack = trackIndex;
        draggedClipId = clickedClipId;
        dragClipOriginalStart = clickedClip->timelineStart;
        dragPreviewStart = clickedClip->timelineStart;
        dragOffsetSeconds = clickedTime - clickedClip->timelineStart;
    }
    else if (state.activeTool == Tool::range)
    {
        draggingRange = true;
        rangeAnchor = juce::jlimit (clickedClip->timelineStart,
                                    clickedClip->timelineStart + clickedClip->duration,
                                    clickedTime);
        rangePreviewStart = rangeAnchor;
        rangePreviewEnd = rangeAnchor;
        const auto clipOffset = rangeAnchor - clickedClip->timelineStart;
        const auto existing = std::find_if (
            clickedClip->spatialRegions.begin(), clickedClip->spatialRegions.end(),
            [clipOffset] (const AudioEngine::SpatialRegion& region)
            {
                return clipOffset >= region.startOffset
                    && clipOffset < region.startOffset + region.duration;
            });
        if (existing != clickedClip->spatialRegions.end())
            rangeHitRegionId = existing->id;
    }
    repaint();
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingPlayhead)
    {
        setPlayheadFromX (event.position.x);
        return;
    }

    if (draggingRange)
    {
        if (event.getDistanceFromDragStart() >= 3)
            rangeDragMoved = true;
        const auto project = audioEngine.getProjectSnapshot();
        const auto& clips = project->tracks[static_cast<size_t> (state.selectedTrack)].clips;
        const auto clip = std::find_if (clips.begin(), clips.end(), [this] (const AudioEngine::Clip& item)
        {
            return item.id == state.selectedClipId;
        });
        if (clip != clips.end())
        {
            const auto current = juce::jlimit (clip->timelineStart,
                                               clip->timelineStart + clip->duration,
                                               timeAtX (event.position.x));
            rangePreviewStart = juce::jmin (rangeAnchor, current);
            rangePreviewEnd = juce::jmax (rangeAnchor, current);
            repaint();
        }
        return;
    }

    if (! draggingClip)
        return;

    if (event.getDistanceFromDragStart() >= 3)
        clipDragMoved = true;
    if (! clipDragMoved)
        return;

    const auto destination = trackAtY (event.position.y);
    dragTargetTrack = destination >= 0 ? destination : dragSourceTrack;
    const auto desiredStart = juce::jmax (0.0, timeAtX (event.position.x) - dragOffsetSeconds);
    const auto project = audioEngine.getProjectSnapshot();
    const auto& sourceClips = project->tracks[static_cast<size_t> (dragSourceTrack)].clips;
    const auto dragged = std::find_if (sourceClips.begin(), sourceClips.end(), [this] (const auto& clip)
    {
        return clip.id == draggedClipId;
    });
    snapGuideTime = -1.0;
    if (dragged != sourceClips.end() && snappingEnabled && ! event.mods.isAltDown())
    {
        if (! hasSnapForDrag (desiredStart, dragTargetTrack, draggedClipId,
                              dragged->duration, dragPreviewStart))
            dragPreviewStart = desiredStart;
        else
            snapGuideTime = dragPreviewStart;
    }
    else
    {
        dragPreviewStart = desiredStart;
    }
    repaint();
}

void TimelineComponent::mouseUp (const juce::MouseEvent&)
{
    draggingPlayhead = false;
    if (draggingRange)
    {
        if (rangeDragMoved && rangePreviewEnd - rangePreviewStart >= 0.001)
        {
            auto parameters = audioEngine.getTrackSpatialParameters (state.selectedTrack);
            parameters.enabled = true;
            audioEngine.setTrackSpatialParameters (state.selectedTrack, parameters);
            if (const auto regionId = audioEngine.createClipSpatialRegion (
                    state.selectedTrack, state.selectedClipId, rangePreviewStart,
                    rangePreviewEnd, parameters))
            {
                state.selectedSpatialRegionId = *regionId;
                state.selectedRangeStart = rangePreviewStart;
                state.selectedRangeEnd = rangePreviewEnd;
                if (onSpatialRangeSelected) onSpatialRangeSelected();
            }
        }
        else if (rangeHitRegionId != 0)
        {
            state.selectedSpatialRegionId = rangeHitRegionId;
            if (const auto region = audioEngine.getClipSpatialRegion (
                    state.selectedTrack, state.selectedClipId, rangeHitRegionId))
            {
                const auto project = audioEngine.getProjectSnapshot();
                const auto& clips = project->tracks[static_cast<size_t> (state.selectedTrack)].clips;
                const auto clip = std::find_if (clips.begin(), clips.end(), [this] (const auto& item)
                {
                    return item.id == state.selectedClipId;
                });
                if (clip != clips.end())
                {
                    state.selectedRangeStart = clip->timelineStart + region->startOffset;
                    state.selectedRangeEnd = state.selectedRangeStart + region->duration;
                }
            }
            if (onSpatialRangeSelected) onSpatialRangeSelected();
        }
        draggingRange = false;
        rangeDragMoved = false;
        rangeHitRegionId = 0;
        grabKeyboardFocus();
        repaint();
        return;
    }

    if (draggingClip && clipDragMoved && dragTargetTrack >= 0)
    {
        if (audioEngine.moveClip (dragSourceTrack, draggedClipId, dragTargetTrack, dragPreviewStart))
            selectClip (dragTargetTrack, draggedClipId);
    }

    draggingClip = false;
    clipDragMoved = false;
    dragSourceTrack = -1;
    dragTargetTrack = -1;
    draggedClipId = 0;
    snapGuideTime = -1.0;
    repaint();
}

double TimelineComponent::snapTimeForDrag (double desiredStart, int destinationTrack,
                                            uint64_t clipId, double duration) const noexcept
{
    auto snapped = desiredStart;
    juce::ignoreUnused (hasSnapForDrag (desiredStart, destinationTrack, clipId, duration, snapped));
    return snapped;
}

bool TimelineComponent::hasSnapForDrag (double desiredStart, int destinationTrack,
                                        uint64_t clipId, double duration,
                                        double& snappedStart) const noexcept
{
    if (destinationTrack < 0 || destinationTrack >= audioEngine.getTrackCount())
        return false;

    const auto laneWidth = juce::jmax (1.0f, static_cast<float> (getWidth() - trackHeaderWidth()));
    const auto threshold = juce::jmax (0.015, static_cast<double> (12.0f / laneWidth)
                                                * visibleLength());
    const auto project = audioEngine.getProjectSnapshot();
    const auto& clips = project->tracks[static_cast<size_t> (destinationTrack)].clips;
    double bestDistance = threshold;
    double best = desiredStart;
    const auto consider = [&] (double candidate)
    {
        candidate = juce::jmax (0.0, candidate);
        const auto distance = std::abs (candidate - desiredStart);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    };

    consider (0.0);
    consider (audioEngine.getPosition());
    for (const auto& clip : clips)
    {
        if (clip.id == clipId)
            continue;
        consider (clip.timelineStart);
        consider (clip.timelineStart + clip.duration);
        consider (clip.timelineStart - duration);
        consider (clip.timelineStart + clip.duration - duration);
    }

    if (bestDistance >= threshold)
        return false;
    snappedStart = best;
    return true;
}

void TimelineComponent::mouseMove (const juce::MouseEvent& event)
{
    uint64_t nextHovered = 0;
    if (const auto trackIndex = trackAtY (event.position.y); trackIndex >= 0
        && event.x >= trackHeaderWidth())
        if (const auto clip = clipAt (trackIndex, timeAtX (event.position.x)))
            nextHovered = clip->id;

    if (nextHovered != hoveredClipId)
    {
        hoveredClipId = nextHovered;
        repaint();
    }

    if (event.y < rulerHeight() && event.x >= trackHeaderWidth())
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (hoveredClipId != 0)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseExit (const juce::MouseEvent&)
{
    if (! draggingClip && hoveredClipId != 0)
    {
        hoveredClipId = 0;
        repaint();
    }
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& event,
                                        const juce::MouseWheelDetails& details)
{
    const auto projectLength = audioEngine.getLength();
    const auto baseLength = projectLength > 0.0
        ? juce::jmax (30.0, std::ceil (projectLength / 30.0) * 30.0)
        : 120.0;

    if (event.mods.isCtrlDown())
    {
        const auto anchorTime = timeAtX (event.position.x);
        const auto oldLength = visibleLength();
        const auto normal = juce::jlimit (0.0, 1.0,
                                          (static_cast<double> (event.position.x) - trackHeaderWidth())
                                              / juce::jmax (1.0, static_cast<double> (getWidth() - trackHeaderWidth())));
        const auto zoomFactor = details.deltaY > 0.0f ? 1.18 : 1.0 / 1.18;
        timelineZoom = juce::jlimit (0.25, 16.0, timelineZoom * zoomFactor);
        const auto newLength = visibleLength();
        timelineViewStart = anchorTime - normal * newLength;
        timelineViewStart = juce::jlimit (0.0, juce::jmax (0.0, baseLength - newLength),
                                          timelineViewStart);
        juce::ignoreUnused (oldLength);
        repaint();
        return;
    }

    if (event.mods.isAltDown())
    {
        timelineViewStart -= static_cast<double> (details.deltaY) * visibleLength() * 0.22;
        timelineViewStart -= static_cast<double> (details.deltaX) * visibleLength() * 0.22;
        timelineViewStart = juce::jlimit (0.0, juce::jmax (0.0, baseLength - visibleLength()),
                                          timelineViewStart);
        repaint();
        return;
    }

    juce::Component::mouseWheelMove (event, details);
}

bool TimelineComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey || key.getTextCharacter() == ' ')
    {
        if (audioEngine.isPlaying())
            audioEngine.stop();
        else
            audioEngine.play();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (deleteSelectedSpatialRegion())
            return true;
        deleteSelectedClip();
        return true;
    }

    if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D')
        && state.selectedClipId != 0)
    {
        const auto project = audioEngine.getProjectSnapshot();
        const auto& clips = project->tracks[static_cast<size_t> (state.selectedTrack)].clips;
        const auto iterator = std::find_if (clips.begin(), clips.end(), [this] (const AudioEngine::Clip& clip)
        {
            return clip.id == state.selectedClipId;
        });
        if (iterator != clips.end())
            if (const auto duplicate = audioEngine.duplicateClip (state.selectedTrack, iterator->id,
                                                                  state.selectedTrack,
                                                                  iterator->timelineStart + iterator->duration))
                selectClip (state.selectedTrack, *duplicate);
        return true;
    }

    return false;
}

bool TimelineComponent::deleteSelectedSpatialRegion()
{
    if (state.selectedSpatialRegionId == 0)
        return false;

    if (! audioEngine.removeClipSpatialRegion (state.selectedTrack, state.selectedClipId,
                                               state.selectedSpatialRegionId))
        return false;

    state.selectedSpatialRegionId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    if (onSpatialRangeSelected) onSpatialRangeSelected();
    grabKeyboardFocus();
    repaint();
    return true;
}

void TimelineComponent::setPlayheadFromX (float x)
{
    audioEngine.setPosition (juce::jmin (timeAtX (x), audioEngine.getLength()));
    repaint();
}

void TimelineComponent::selectClip (int trackIndex, uint64_t clipId)
{
    state.selectedTrack = trackIndex;
    state.selectedClipId = clipId;
    state.selectedSpatialRegionId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    if (onTrackSelected) onTrackSelected (trackIndex);
    if (onClipSelected) onClipSelected (clipId);
}

void TimelineComponent::deleteSelectedClip()
{
    if (state.selectedClipId == 0)
        return;

    if (audioEngine.removeClip (state.selectedTrack, state.selectedClipId))
    {
        state.selectedClipId = 0;
        state.selectedSpatialRegionId = 0;
        state.selectedRangeStart = 0.0;
        state.selectedRangeEnd = 0.0;
        hoveredClipId = 0;
        if (onTrackSelected) onTrackSelected (state.selectedTrack);
        if (onClipSelected) onClipSelected (0);
        repaint();
    }
}

void TimelineComponent::showClipMenu (juce::Point<int> position, int trackIndex,
                                      uint64_t clipId, double clickedTime)
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto project = audioEngine.getProjectSnapshot();
    const auto& clips = project->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(), [clipId] (const AudioEngine::Clip& clip)
    {
        return clip.id == clipId;
    });
    if (iterator == clips.end())
        return;

    const auto originalStart = iterator->timelineStart;
    const auto duration = iterator->duration;
    const auto canSplit = clickedTime > originalStart + 0.001
                       && clickedTime < originalStart + duration - 0.001;
    const auto clipOffset = clickedTime - originalStart;
    const auto clickedRegion = std::find_if (
        iterator->spatialRegions.begin(), iterator->spatialRegions.end(),
        [clipOffset] (const AudioEngine::SpatialRegion& region)
        {
            return clipOffset >= region.startOffset
                && clipOffset < region.startOffset + region.duration;
        });
    const auto clickedRegionId = clickedRegion != iterator->spatialRegions.end()
                               ? clickedRegion->id : uint64_t { 0 };
    if (clickedRegionId != 0)
    {
        state.selectedSpatialRegionId = clickedRegionId;
        state.selectedRangeStart = originalStart + clickedRegion->startOffset;
        state.selectedRangeEnd = state.selectedRangeStart + clickedRegion->duration;
        if (onSpatialRangeSelected) onSpatialRangeSelected();
        repaint();
    }

    juce::PopupMenu menu;
    if (clickedRegionId != 0)
    {
        menu.addItem (4, chinese ? juce::String::fromUTF8 ("删除 3D 空间区间")
                                 : juce::String ("Delete 3D spatial region"));
        menu.addSeparator();
    }
    menu.addItem (1, chinese ? juce::String::fromUTF8 ("在此处分割") : "Split here", canSplit);
    menu.addItem (2, chinese ? juce::String::fromUTF8 ("创建副本") : "Duplicate");
    menu.addItem (3, chinese ? juce::String::fromUTF8 ("删除剪辑") : "Delete clip");
    menu.addSeparator();
    juce::PopupMenu moveMenu;
    for (int destination = 0; destination < audioEngine.getTrackCount(); ++destination)
        moveMenu.addItem (101 + destination,
                          (chinese ? juce::String::fromUTF8 ("轨道 ") : "Track ")
                              + juce::String (destination + 1),
                          destination != trackIndex);
    menu.addSubMenu (chinese ? juce::String::fromUTF8 ("移动到轨道") : "Move to track", moveMenu);

    const auto screenPoint = localPointToGlobal (position);
    auto safeThis = juce::Component::SafePointer<TimelineComponent> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea ({ screenPoint.x, screenPoint.y, 1, 1 }),
                        [safeThis, trackIndex, clipId, clickedTime, originalStart, duration,
                         clickedRegionId] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == 1)
        {
            if (const auto right = safeThis->audioEngine.splitClipAt (trackIndex, clipId, clickedTime))
                safeThis->selectClip (trackIndex, *right);
        }
        else if (result == 2)
        {
            if (const auto duplicate = safeThis->audioEngine.duplicateClip (
                    trackIndex, clipId, trackIndex, originalStart + duration))
                safeThis->selectClip (trackIndex, *duplicate);
        }
        else if (result == 3)
        {
            safeThis->selectClip (trackIndex, clipId);
            safeThis->deleteSelectedClip();
        }
        else if (result == 4)
        {
            safeThis->state.selectedTrack = trackIndex;
            safeThis->state.selectedClipId = clipId;
            safeThis->state.selectedSpatialRegionId = clickedRegionId;
            safeThis->deleteSelectedSpatialRegion();
        }
        else if (result >= 101 && result < 101 + safeThis->audioEngine.getTrackCount())
        {
            const auto destination = result - 101;
            if (safeThis->audioEngine.moveClip (trackIndex, clipId, destination, originalStart))
                safeThis->selectClip (destination, clipId);
        }
        safeThis->repaint();
    });
}

void TimelineComponent::resized()
{
    const auto headerWidth = trackHeaderWidth();
    const auto buttonSize = getWidth() < 600 || getHeight() < 360 ? 36 : 28;
    addTrackButton.setBounds (juce::jmax (2, headerWidth - buttonSize - 4),
                              juce::jmax (2, (rulerHeight() - buttonSize) / 2),
                              buttonSize, buttonSize);
}

void TimelineComponent::timerCallback()
{
    if (audioEngine.isPlaying())
        repaint();
}

void TimelineComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    addTrackButton.setEnabled (audioEngine.getTrackCount() < AudioEngine::maximumTrackCount);
    repaint();
}

bool TimelineComponent::isInterestedInDragSource (const SourceDetails& details)
{
    const auto file = juce::File (details.description.toString());
    return file.existsAsFile();
}

void TimelineComponent::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void TimelineComponent::itemDragMove (const SourceDetails& details)
{
    if (! isInterestedInDragSource (details))
        return;

    dragDropTrack = trackAtY (static_cast<float> (details.localPosition.y));
    dragDropTime = timeAtX (static_cast<float> (details.localPosition.x));
    repaint();
}

void TimelineComponent::itemDragExit (const SourceDetails&)
{
    dragDropTrack = -1;
    dragDropTime = 0.0;
    repaint();
}

void TimelineComponent::itemDropped (const SourceDetails& details)
{
    const auto file = juce::File (details.description.toString());
    const auto trackIndex = trackAtY (static_cast<float> (details.localPosition.y));
    if (file.existsAsFile() && trackIndex >= 0)
    {
        if (const auto clipId = audioEngine.addFileToTrack (file, trackIndex,
                                                              juce::jmax (0.0, timeAtX (static_cast<float> (details.localPosition.x)))))
            selectClip (trackIndex, *clipId);
    }

    dragDropTrack = -1;
    dragDropTime = 0.0;
    repaint();
}

SpatialCanvas::SpatialCanvas (Localizer& strings, AppState& appState, AudioEngine& engine)
    : localizer (strings), state (appState), audioEngine (engine)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

juce::Point<float> SpatialCanvas::sourcePoint (const SpatialParameters& parameters) const
{
    const auto area = positionArea();
    const auto centre = area.getCentre();
    const auto azimuth = parameters.azimuth
                       + parameters.orbitSpeed * static_cast<float> (audioEngine.getPosition());
    const auto angle = juce::degreesToRadians (azimuth - 90.0f);
    const auto distanceNorm = juce::jmap (parameters.distance, 0.5f, 12.0f, 0.15f, 0.92f);
    const auto radius = area.getWidth() * 0.5f * distanceNorm;
    return { centre.x + std::cos (angle) * radius,
             centre.y + std::sin (angle) * radius };
}

juce::Rectangle<float> SpatialCanvas::positionArea() const
{
    auto area = getLocalBounds().toFloat().reduced (20.0f);
    area.removeFromRight (64.0f);
    area.removeFromBottom (24.0f);
    const auto side = juce::jmax (1.0f, juce::jmin (area.getWidth(), area.getHeight()));
    return juce::Rectangle<float> (side, side).withCentre (area.getCentre());
}

juce::Rectangle<float> SpatialCanvas::elevationArea() const
{
    auto area = getLocalBounds().toFloat().reduced (20.0f);
    area.removeFromBottom (24.0f);
    return area.removeFromRight (42.0f).reduced (10.0f, 4.0f);
}

void SpatialCanvas::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.background);
    const auto area = positionArea();
    const auto elevationTrack = elevationArea();
    const auto centre = area.getCentre();
    const auto radius = area.getWidth() * 0.5f;

    g.setColour (c.lineSoft);
    for (const auto scale : { 1.0f, 0.7f, 0.38f })
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f * scale,
                                               radius * 2.0f * scale).withCentre (centre), 1.0f);
    g.setColour (c.faint);
    g.drawDashedLine ({ area.getX(), centre.y, area.getRight(), centre.y }, std::array<float, 2> { 4.0f, 5.0f }.data(), 2, 1.0f);
    g.drawDashedLine ({ centre.x, area.getY(), centre.x, area.getBottom() }, std::array<float, 2> { 4.0f, 5.0f }.data(), 2, 1.0f);

    g.setFont (juce::FontOptions (8.5f));
    g.drawText ("FRONT", juce::roundToInt (centre.x - 28.0f), juce::roundToInt (area.getY()),
                56, 14, juce::Justification::centred, false);
    g.drawText ("REAR", juce::roundToInt (centre.x - 28.0f), juce::roundToInt (area.getBottom() - 14.0f),
                56, 14, juce::Justification::centred, false);
    g.drawText ("L", juce::roundToInt (area.getX()), juce::roundToInt (centre.y - 8.0f),
                18, 16, juce::Justification::centredLeft, false);
    g.drawText ("R", juce::roundToInt (area.getRight() - 18.0f), juce::roundToInt (centre.y - 8.0f),
                18, 16, juce::Justification::centredRight, false);

    juce::Path orbitPath;
    const auto selectedDistance = juce::jmap (state.spatial.distance, 0.5f, 12.0f, 0.15f, 0.92f);
    const auto orbitRadius = radius * selectedDistance;
    orbitPath.addEllipse (juce::Rectangle<float> (orbitRadius * 2.0f,
                                                  orbitRadius * 2.0f).withCentre (centre));
    g.setColour (c.coral.withAlpha (0.62f));
    g.strokePath (orbitPath, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded),
                  juce::AffineTransform());

    g.setColour (c.raised);
    g.fillEllipse (centre.x - 16.0f, centre.y - 16.0f, 32.0f, 32.0f);
    g.setColour (c.text);
    g.drawEllipse (centre.x - 16.0f, centre.y - 16.0f, 32.0f, 32.0f, 2.0f);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (localizer.text (TextId::listener), juce::roundToInt (centre.x - 36.0f),
                juce::roundToInt (centre.y + 19.0f), 72, 16, juce::Justification::centred, false);

    const auto project = audioEngine.getProjectSnapshot();
    const std::array<TextId, AudioEngine::trackCount> trackNames {
        TextId::leadVocal, TextId::synth, TextId::drums, TextId::atmosphere, TextId::fxReturn
    };
    const std::array<juce::Colour, AudioEngine::trackCount> sourceColours {
        c.coral, c.blue, c.yellow, c.green, c.accent
    };
    for (int trackIndex = 0; trackIndex < audioEngine.getTrackCount(); ++trackIndex)
    {
        const auto isSelected = trackIndex == state.selectedTrack;
        const auto& parameters = isSelected
            ? state.spatial
            : project->tracks[static_cast<size_t> (trackIndex)].spatial;
        if (! isSelected && ! parameters.enabled)
            continue;

        const auto source = sourcePoint (parameters);
        const auto colour = trackIndex < AudioEngine::trackCount
            ? sourceColours[static_cast<size_t> (trackIndex)]
            : juce::Colour::fromHSV (std::fmod (0.08f + trackIndex * 0.113f, 1.0f),
                                     0.62f, 0.94f, 1.0f);
        const auto haloRadius = isSelected ? 16.0f + parameters.spread * 0.15f
                                           : 9.0f + parameters.spread * 0.06f;
        g.setColour (colour.withAlpha (isSelected ? 0.16f : 0.10f));
        g.fillEllipse (source.x - haloRadius, source.y - haloRadius, haloRadius * 2.0f, haloRadius * 2.0f);
        g.setColour (colour.withAlpha (isSelected ? 0.55f : 0.42f));
        g.drawEllipse (source.x - haloRadius, source.y - haloRadius, haloRadius * 2.0f, haloRadius * 2.0f, 1.0f);
        g.setColour (colour);
        const auto markerRadius = isSelected ? 8.0f : 5.5f;
        g.fillEllipse (source.x - markerRadius, source.y - markerRadius,
                       markerRadius * 2.0f, markerRadius * 2.0f);
        if (isSelected)
        {
            g.setColour (c.background);
            g.drawEllipse (source.x - markerRadius, source.y - markerRadius,
                           markerRadius * 2.0f, markerRadius * 2.0f, 2.0f);
        }
        g.setColour (c.muted);
        g.setFont (juce::FontOptions (10.0f));
        const auto trackLabel = project->tracks[static_cast<size_t> (trackIndex)].name;
        g.drawText (trackLabel.isNotEmpty() ? trackLabel
                                            : localizer.text (trackNames[static_cast<size_t> (trackIndex)]),
                    juce::roundToInt (source.x + markerRadius + 5.0f),
                    juce::roundToInt (source.y - 10.0f), 94, 18,
                    juce::Justification::centredLeft, false);
    }

    g.setColour (c.line);
    g.fillRoundedRectangle (elevationTrack.withWidth (4.0f).withCentre (elevationTrack.getCentre()), 2.0f);
    const auto elevationY = juce::jmap (juce::jlimit (-90.0f, 90.0f, state.spatial.elevation),
                                        -90.0f, 90.0f, elevationTrack.getBottom(), elevationTrack.getY());
    g.setColour (c.accent);
    g.fillRoundedRectangle (elevationTrack.withTop (elevationY).withWidth (4.0f)
                                          .withCentre ({ elevationTrack.getCentreX(),
                                                         (elevationY + elevationTrack.getBottom()) * 0.5f }),
                            2.0f);
    g.fillEllipse (elevationTrack.getCentreX() - 7.0f, elevationY - 7.0f, 14.0f, 14.0f);
    g.setColour (c.faint);
    g.setFont (juce::FontOptions (8.0f));
    g.drawText ("+90", juce::roundToInt (elevationTrack.getX() - 8.0f),
                juce::roundToInt (elevationTrack.getY() - 12.0f), 38, 12,
                juce::Justification::centred, false);
    g.drawText ("0", juce::roundToInt (elevationTrack.getX() - 8.0f),
                juce::roundToInt (juce::jmap (0.0f, -90.0f, 90.0f,
                                              elevationTrack.getBottom(), elevationTrack.getY()) - 6.0f),
                38, 12, juce::Justification::centred, false);
    g.drawText ("-90", juce::roundToInt (elevationTrack.getX() - 8.0f),
                juce::roundToInt (elevationTrack.getBottom()), 38, 12,
                juce::Justification::centred, false);

    g.setColour (c.faint);
    g.setFont (juce::FontOptions (9.0f));
    const auto degree = juce::String::fromUTF8 ("°");
    const auto signedDegrees = [&degree] (float value)
    {
        const auto rounded = juce::roundToInt (value);
        return juce::String (rounded >= 0 ? "+" : "") + juce::String (rounded) + degree;
    };
    const auto currentAzimuth = wrapDegreesForUi (
        state.spatial.azimuth
        + state.spatial.orbitSpeed * static_cast<float> (audioEngine.getPosition()));
    const auto orbitPeriod = std::abs (state.spatial.orbitSpeed) > 0.001f
                                 ? 360.0f / std::abs (state.spatial.orbitSpeed)
                                 : 0.0f;
    const auto outputChannels = audioEngine.getOutputChannelCount();
    const auto renderLayout = outputChannels >= 12 ? "7.1.4" : outputChannels >= 10 ? "5.1.4"
                              : outputChannels >= 8 ? "7.1" : outputChannels == 7 ? "6.1"
                              : outputChannels >= 6 ? "5.1"
                              : outputChannels >= 4 ? "Quad" : "Stereo";
    const auto orbitDetail = orbitPeriod > 0.0f
                                 ? juce::String ("   Orbit ") + signedDegrees (state.spatial.orbitSpeed)
                                       + "/s (" + juce::String (orbitPeriod, orbitPeriod < 10.0f ? 1 : 0) + " s/rev)"
                                 : juce::String ("   Orbit OFF");
    const auto stereoNotice = outputChannels <= 2
                                  ? (localizer.getLanguage() == Language::chinese
                                         ? juce::String::fromUTF8 (" · 近似双耳 ITD/ILD")
                                         : juce::String (" · Pseudo-binaural ITD/ILD"))
                                  : juce::String();
    const auto detail = juce::String (renderLayout) + stereoNotice
                      + "   Az " + signedDegrees (currentAzimuth)
                      + "   El " + signedDegrees (state.spatial.elevation)
                      + "   " + juce::String (state.spatial.distance, 1) + " m"
                      + orbitDetail
                      + (state.spatial.enabled ? "   ACTIVE" : "   BYPASSED");
    g.drawText (detail, getLocalBounds().removeFromBottom (20).reduced (8, 0), juce::Justification::centredRight, false);
}

void SpatialCanvas::mouseDown (const juce::MouseEvent& event)
{
    draggingPosition = false;
    draggingElevation = elevationArea().expanded (12.0f, 4.0f).contains (event.position);
    if (draggingElevation)
        updateElevationFromPoint (event.position);
    else if (positionArea().contains (event.position))
    {
        draggingPosition = true;
        updatePositionFromPoint (event.position);
    }
}

void SpatialCanvas::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingElevation)
        updateElevationFromPoint (event.position);
    else if (draggingPosition)
        updatePositionFromPoint (event.position);
}

void SpatialCanvas::mouseUp (const juce::MouseEvent&)
{
    draggingPosition = false;
    draggingElevation = false;
}

void SpatialCanvas::updatePositionFromPoint (juce::Point<float> point)
{
    const auto area = positionArea();
    const auto centre = area.getCentre();
    const auto delta = point - centre;
    const auto displayedAzimuth = wrapDegreesForUi (
        juce::radiansToDegrees (std::atan2 (delta.y, delta.x)) + 90.0f);
    state.spatial.azimuth = wrapDegreesForUi (
        displayedAzimuth - state.spatial.orbitSpeed * static_cast<float> (audioEngine.getPosition()));
    const auto norm = delta.getDistanceFromOrigin() / (area.getWidth() * 0.5f);
    state.spatial.distance = juce::jlimit (0.5f, 12.0f, juce::jmap (norm, 0.15f, 0.92f, 0.5f, 12.0f));
    state.spatial.enabled = true;
    if (onParametersChanged) onParametersChanged();
    repaint();
}

void SpatialCanvas::updateElevationFromPoint (juce::Point<float> point)
{
    const auto area = elevationArea();
    const auto y = juce::jlimit (area.getY(), area.getBottom(), point.y);
    state.spatial.elevation = juce::jmap (y, area.getBottom(), area.getY(), -90.0f, 90.0f);
    state.spatial.enabled = true;
    if (onParametersChanged) onParametersChanged();
    repaint();
}

ParameterRow::ParameterRow (Localizer& strings, TextId id, double minimum, double maximum,
                            double value, double step, juce::String valueSuffix, bool shouldSign)
    : localizer (strings), textId (id), suffix (std::move (valueSuffix)), signedValue (shouldSign)
{
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    valueEditor.setJustification (juce::Justification::centredRight);
    valueEditor.setInputRestrictions (12, "-+0123456789.");
    valueEditor.setSelectAllWhenFocused (true);
    valueEditor.setMultiLine (false);
    valueEditor.setReturnKeyStartsNewLine (false);
    valueEditor.setScrollbarsShown (false);
    valueEditor.onReturnKey = [this]
    {
        slider.setValue (valueEditor.getText().getDoubleValue(), juce::sendNotification);
        updateValueLabel();
        valueEditor.giveAwayKeyboardFocus();
    };
    valueEditor.onFocusLost = [this]
    {
        slider.setValue (valueEditor.getText().getDoubleValue(), juce::sendNotification);
        updateValueLabel();
    };
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (minimum, maximum, step);
    slider.setValue (value, juce::dontSendNotification);
    slider.onValueChange = [this]
    {
        updateValueLabel();
        if (onValueChanged) onValueChanged (slider.getValue());
    };
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (valueEditor);
    addAndMakeVisible (slider);
    refreshText();
    updateValueLabel();
}

void ParameterRow::refreshText()
{
    nameLabel.setText (localizer.text (textId), juce::dontSendNotification);
}

void ParameterRow::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (20);
    valueEditor.setBounds (header.removeFromRight (86).reduced (0, 1));
    nameLabel.setBounds (header);
    slider.setBounds (area.reduced (0, 1));
}

void ParameterRow::lookAndFeelChanged()
{
    valueEditor.applyColourToAllText (coloursOf (*this).text, true);
    repaint();
}

void ParameterRow::setValue (double value, juce::NotificationType notification)
{
    slider.setValue (value, notification);
    updateValueLabel();
}

void ParameterRow::updateValueLabel()
{
    const auto decimals = slider.getInterval() < 0.1 ? 2 : slider.getInterval() < 1.0 ? 1 : 0;
    auto value = juce::String (slider.getValue(), decimals);
    if (signedValue && slider.getValue() >= 0.0)
        value = "+" + value;
    const auto displayValue = value + suffix;
    valueEditor.setText (displayValue, false);
    valueEditor.setTooltip (displayValue);
}

InspectorPanel::InspectorPanel (Localizer& strings, AppState& appState, AudioEngine& engine)
    : localizer (strings), state (appState), audioEngine (engine),
      azimuth (strings, TextId::azimuth, -180.0, 180.0, appState.spatial.azimuth, 1.0, juce::String::fromUTF8 ("°"), true),
      elevation (strings, TextId::elevation, -90.0, 90.0, appState.spatial.elevation, 1.0, juce::String::fromUTF8 ("°"), true),
      distance (strings, TextId::distance, 0.5, 12.0, appState.spatial.distance, 0.1, " m"),
      orbitSpeed (strings, TextId::orbitSpeed, -360.0, 360.0, appState.spatial.orbitSpeed, 1.0, juce::String::fromUTF8 ("°/s"), true),
      spread (strings, TextId::sourceSpread, 0.0, 100.0, appState.spatial.spread, 1.0, "%"),
      directivity (strings, TextId::directivity, 0.0, 100.0, appState.spatial.directivity, 1.0, "%"),
      regionGain (strings, TextId::regionGain, -24.0, 12.0,
                  appState.selectedSpatialRegionGainDb, 0.1, " dB", true),
      regionTransition (strings, TextId::transitionTime, 0.01, 5.0,
                        appState.selectedSpatialRegionTransitionSeconds, 0.01, " s"),
      clipSpeed (strings, TextId::playbackSpeed, 0.5, 2.0, appState.playbackSpeed, 0.01, juce::String::fromUTF8 ("×")),
      transientProtection (strings, TextId::transientProtection, 0.0, 100.0, 72.0, 1.0, "%"),
      clipGain (strings, TextId::clipGain, -24.0, 12.0, 1.5, 0.1, " dB", true),
      clipPitch (strings, TextId::pitch, -12.0, 12.0, 0.0, 0.01, " st", true),
      threshold (strings, TextId::threshold, -60.0, 0.0, -18.0, 0.1, " dB"),
      ratio (strings, TextId::ratio, 1.0, 12.0, 3.2, 0.1, ":1"),
      eqGain (strings, TextId::gain, -18.0, 18.0, 2.8, 0.1, " dB", true),
      eqFrequency (strings, TextId::effects, 20.0, 20000.0, 2400.0, 1.0, " Hz"),
      trackVolume (strings, TextId::volume, -60.0, 12.0, -2.4, 0.1, " dB"),
      pan (strings, TextId::pan, -100.0, 100.0, 0.0, 1.0, "%", true),
      sendA (strings, TextId::sendA, -60.0, 6.0, -12.0, 0.1, " dB"),
      sendB (strings, TextId::sendB, -60.0, 6.0, -18.5, 0.1, " dB")
{
    for (int i = 0; i < 4; ++i)
    {
        configureTabButton (tabs[static_cast<size_t> (i)], static_cast<InspectorTab> (i));
        addAndMakeVisible (tabs[static_cast<size_t> (i)]);
    }

    selectionTitle.setFont (juce::FontOptions (12.0f));
    selectionTitle.setJustificationType (juce::Justification::centredLeft);
    selectionSubtitle.setJustificationType (juce::Justification::centredLeft);
    badge.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (selectionTitle);
    addAndMakeVisible (selectionSubtitle);
    addAndMakeVisible (badge);
    addAndMakeVisible (deleteSpatialRegionButton);
    deleteSpatialRegionButton.onClick = [this]
    {
        if (onDeleteSpatialRegion) onDeleteSpatialRegion();
    };

    parameterViewport.setViewedComponent (&parameterContent, false);
    parameterViewport.setScrollBarsShown (true, false);
    parameterViewport.setScrollBarThickness (6);
    parameterContent.setOpaque (false);
    addAndMakeVisible (parameterViewport);

    addSectionLabel (sectionOne);
    addSectionLabel (sectionTwo);

    for (auto* row : { &azimuth, &elevation, &distance, &orbitSpeed, &spread, &directivity,
                       &regionGain, &regionTransition,
                       &clipSpeed, &transientProtection, &clipGain, &clipPitch,
                       &threshold, &ratio, &eqGain, &eqFrequency,
                       &trackVolume, &pan, &sendA, &sendB })
        parameterContent.addAndMakeVisible (*row);

    attenuation.addItem ("Inverse Square", 1);
    attenuation.addItem ("Linear", 2);
    attenuation.addItem ("Custom Curve", 3);
    attenuation.setSelectedId (1);
    stretchMode.addItem ("Signalsmith | High Quality", 1);
    stretchMode.addItem ("Elastique Pro", 2);
    stretchMode.addItem ("Resample", 3);
    stretchMode.setSelectedId (1);
    input.addItem ("Input 1", 1); input.addItem ("Input 2", 2); input.setSelectedId (1);
    output.addItem ("Master", 1); output.addItem ("Bus A", 2); output.setSelectedId (1);

    for (auto* combo : { &attenuation, &stretchMode, &input, &output })
        parameterContent.addAndMakeVisible (*combo);

    for (auto* toggle : { &spatialEnabled, &airAbsorption, &preservePitch, &preserveFormants,
                          &phaseInvert, &monitor, &automationRead })
        parameterContent.addAndMakeVisible (*toggle);

    spatialEnabled.setClickingTogglesState (true);
    airAbsorption.setToggleState (state.spatial.airAbsorption, juce::dontSendNotification);
    preservePitch.setToggleState (true, juce::dontSendNotification);
    preserveFormants.setToggleState (true, juce::dontSendNotification);
    monitor.setToggleState (true, juce::dontSendNotification);
    automationRead.setToggleState (true, juce::dontSendNotification);

    for (size_t i = 0; i < inserts.size(); ++i)
    {
        inserts[i].setButtonText (i == 0 ? "01  Parametric EQ" : i == 1 ? "02  Studio Compressor" : "03  De-Esser");
        inserts[i].setClickingTogglesState (true);
        inserts[i].setToggleState (true, juce::dontSendNotification);
        parameterContent.addAndMakeVisible (inserts[i]);
    }

    const auto spatialChanged = [this]
    {
        state.spatial.azimuth = static_cast<float> (azimuth.getValue());
        state.spatial.elevation = static_cast<float> (elevation.getValue());
        state.spatial.distance = static_cast<float> (distance.getValue());
        state.spatial.orbitSpeed = static_cast<float> (orbitSpeed.getValue());
        state.spatial.spread = static_cast<float> (spread.getValue());
        state.spatial.directivity = static_cast<float> (directivity.getValue());
        if (onSpatialChanged) onSpatialChanged();
    };
    azimuth.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    elevation.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    distance.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    orbitSpeed.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    spread.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    directivity.onValueChanged = [spatialChanged] (double) { spatialChanged(); };
    const auto regionEnvelopeChanged = [this]
    {
        state.selectedSpatialRegionGainDb = static_cast<float> (regionGain.getValue());
        state.selectedSpatialRegionTransitionSeconds = regionTransition.getValue();
        if (onSpatialRegionEnvelopeChanged)
            onSpatialRegionEnvelopeChanged (state.selectedSpatialRegionGainDb,
                                              state.selectedSpatialRegionTransitionSeconds);
    };
    regionGain.onValueChanged = [regionEnvelopeChanged] (double) { regionEnvelopeChanged(); };
    regionTransition.onValueChanged = [regionEnvelopeChanged] (double) { regionEnvelopeChanged(); };
    attenuation.onChange = [this]
    {
        state.spatial.attenuation = static_cast<SpatialAttenuation> (
            juce::jlimit (0, 2, attenuation.getSelectedId() - 1));
        if (onSpatialChanged) onSpatialChanged();
    };
    clipSpeed.onValueChanged = [this] (double value)
    {
        state.playbackSpeed = value;
        if (onPlaybackSpeedChanged) onPlaybackSpeedChanged (value);
    };
    clipGain.onValueChanged = [this] (double value)
    {
        audioEngine.setClipGainDb (state.selectedTrack, state.selectedClipId,
                                   static_cast<float> (value));
    };
    trackVolume.onValueChanged = [this] (double value)
    {
        audioEngine.setTrackGainDb (state.selectedTrack, static_cast<float> (value));
        if (onTrackGainChanged) onTrackGainChanged (value);
    };
    pan.onValueChanged = [this] (double value)
    {
        audioEngine.setTrackPan (state.selectedTrack, static_cast<float> (value / 100.0));
        if (onTrackPanChanged) onTrackPanChanged (value);
    };
    spatialEnabled.onClick = [this]
    {
        state.spatial.enabled = spatialEnabled.getToggleState();
        if (onSpatialChanged) onSpatialChanged();
    };
    airAbsorption.onClick = [this]
    {
        state.spatial.airAbsorption = airAbsorption.getToggleState();
        if (onSpatialChanged) onSpatialChanged();
    };

    refreshText();
    setVisiblePage();
}

void InspectorPanel::configureTabButton (juce::TextButton& button, InspectorTab tab)
{
    configureTab (button);
    button.onClick = [this, tab] { setTab (tab); };
}

void InspectorPanel::addSectionLabel (juce::Label& label)
{
    label.setFont (juce::FontOptions (9.0f));
    label.setJustificationType (juce::Justification::centredLeft);
    parameterContent.addAndMakeVisible (label);
}

void InspectorPanel::refreshText()
{
    const std::array<TextId, 4> tabIds { TextId::clip, TextId::spatial, TextId::effects, TextId::track };
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setButtonText (localizer.text (tabIds[i]));

    for (auto* row : { &azimuth, &elevation, &distance, &orbitSpeed, &spread, &directivity,
                       &regionGain, &regionTransition,
                       &clipSpeed, &transientProtection, &clipGain, &clipPitch,
                       &threshold, &ratio, &eqGain, &eqFrequency,
                       &trackVolume, &pan, &sendA, &sendB })
        row->refreshText();

    spatialEnabled.setButtonText (localizer.getLanguage() == Language::chinese
                                      ? juce::String::fromUTF8 ("启用扬声器空间渲染")
                                      : "Enable speaker spatial render");
    spatialEnabled.setTooltip (localizer.getLanguage() == Language::chinese
                                   ? juce::String::fromUTF8 ("启用后，方位角、距离、仰角、旋转、扩散与指向性将影响此轨道的扬声器输出。")
                                   : "When enabled, position, distance, elevation, orbit, spread, and directivity affect this track's speaker output.");
    const auto chinese = localizer.getLanguage() == Language::chinese;
    attenuation.changeItemText (1, chinese ? juce::String::fromUTF8 ("反平方") : "Inverse Square");
    attenuation.changeItemText (2, chinese ? juce::String::fromUTF8 ("线性") : "Linear");
    attenuation.changeItemText (3, chinese ? juce::String::fromUTF8 ("自定义曲线") : "Custom Curve");
    attenuation.setSelectedId (static_cast<int> (state.spatial.attenuation) + 1,
                               juce::dontSendNotification);
    airAbsorption.setButtonText (localizer.text (TextId::airAbsorption));
    const auto deleteRegionText = chinese ? juce::String::fromUTF8 ("删除所选 3D 空间区间")
                                          : juce::String ("Delete selected 3D spatial region");
    deleteSpatialRegionButton.setTitle (deleteRegionText);
    deleteSpatialRegionButton.setTooltip (deleteRegionText);
    preservePitch.setButtonText (localizer.text (TextId::preservePitch));
    preserveFormants.setButtonText (localizer.text (TextId::preserveFormants));
    phaseInvert.setButtonText (localizer.text (TextId::phaseInvert));
    monitor.setButtonText (localizer.text (TextId::monitor));
    automationRead.setButtonText (localizer.text (TextId::automationRead));
    repaint();
    resized();
}

void InspectorPanel::syncFromState()
{
    azimuth.setValue (state.spatial.azimuth);
    elevation.setValue (state.spatial.elevation);
    distance.setValue (state.spatial.distance);
    orbitSpeed.setValue (state.spatial.orbitSpeed);
    spread.setValue (state.spatial.spread);
    directivity.setValue (state.spatial.directivity);
    regionGain.setValue (state.selectedSpatialRegionGainDb);
    regionTransition.setValue (state.selectedSpatialRegionTransitionSeconds);
    clipSpeed.setValue (state.playbackSpeed);
    spatialEnabled.setToggleState (state.spatial.enabled, juce::dontSendNotification);
    airAbsorption.setToggleState (state.spatial.airAbsorption, juce::dontSendNotification);
    attenuation.setSelectedId (static_cast<int> (state.spatial.attenuation) + 1,
                               juce::dontSendNotification);

    const auto project = audioEngine.getProjectSnapshot();
    if (state.selectedTrack >= 0 && state.selectedTrack < audioEngine.getTrackCount())
    {
        const auto& track = project->tracks[static_cast<size_t> (state.selectedTrack)];
        trackVolume.setValue (track.gainDb);
        pan.setValue (track.pan * 100.0f);
        const auto selectedClip = std::find_if (track.clips.begin(), track.clips.end(), [this] (const auto& clip)
        {
            return clip.id == state.selectedClipId;
        });
        if (selectedClip != track.clips.end())
            clipGain.setValue (selectedClip->gainDb);
    }
    repaint();
}

void InspectorPanel::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.panel);
    g.setColour (c.line);
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));
    g.drawHorizontalLine (35, 0.0f, static_cast<float> (getWidth()));
    g.setColour (c.accentSoft);
    g.fillRoundedRectangle (badge.getBounds().toFloat(), 3.0f);
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds();
    auto tabArea = area.removeFromTop (36).reduced (5, 4);
    const auto tabWidth = tabArea.getWidth() / 4;
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setBounds (i == tabs.size() - 1 ? tabArea : tabArea.removeFromLeft (tabWidth));

    area = area.reduced (9, 7);
    auto selection = area.removeFromTop (48);
    const auto showRegionDelete = state.inspectorTab == InspectorTab::spatial
                               && state.selectedSpatialRegionId != 0;
    deleteSpatialRegionButton.setVisible (showRegionDelete);
    if (showRegionDelete)
        deleteSpatialRegionButton.setBounds (selection.removeFromRight (28).reduced (2, 10));
    else
        deleteSpatialRegionButton.setBounds ({ });
    badge.setBounds (selection.removeFromRight (58).reduced (3, 12));
    selectionTitle.setBounds (selection.removeFromTop (23));
    selectionSubtitle.setBounds (selection);
    area.removeFromTop (2);
    parameterViewport.setBounds (area);

    const std::array<int, 4> contentHeights {
        321, state.selectedSpatialRegionId != 0 ? 513 : 399, 290, 343
    };
    const auto contentHeight = contentHeights[static_cast<size_t> (state.inspectorTab)];
    parameterContent.setSize (juce::jmax (0, parameterViewport.getMaximumVisibleWidth()),
                              juce::jmax (contentHeight, parameterViewport.getHeight()));
    layoutRows (parameterContent.getLocalBounds());
}

void InspectorPanel::layoutRows (juce::Rectangle<int> area)
{
    auto hideAll = [this]
    {
        juce::Component* const components[] {
            &azimuth, &elevation, &distance, &orbitSpeed, &spread, &directivity,
            &regionGain, &regionTransition,
            &clipSpeed, &transientProtection, &clipGain, &clipPitch,
            &threshold, &ratio, &eqGain, &eqFrequency,
            &trackVolume, &pan, &sendA, &sendB,
            &attenuation, &stretchMode, &input, &output,
            &spatialEnabled, &airAbsorption, &preservePitch, &preserveFormants,
            &phaseInvert, &monitor, &automationRead, &sectionOne, &sectionTwo
        };
        for (auto* component : components)
            component->setVisible (false);
        for (auto& insert : inserts) insert.setVisible (false);
    };
    hideAll();

    auto placeRow = [&area] (juce::Component& component, int height)
    {
        component.setVisible (true);
        component.setBounds (area.removeFromTop (height));
    };

    switch (state.inspectorTab)
    {
        case InspectorTab::spatial:
        {
            const auto project = audioEngine.getProjectSnapshot();
            const std::array<TextId, AudioEngine::trackCount> trackNames {
                TextId::leadVocal, TextId::synth, TextId::drums, TextId::atmosphere, TextId::fxReturn
            };
            const auto selectedTrack = juce::jlimit (0, audioEngine.getTrackCount() - 1, state.selectedTrack);
            if (state.selectedSpatialRegionId != 0)
            {
                selectionTitle.setText (
                    localizer.getLanguage() == Language::chinese
                        ? juce::String::fromUTF8 ("空间区间") : juce::String ("Spatial region"),
                    juce::dontSendNotification);
                selectionSubtitle.setText (
                    formatTime (state.selectedRangeStart) + " - " + formatTime (state.selectedRangeEnd),
                    juce::dontSendNotification);
                badge.setText ("3D SEG", juce::dontSendNotification);
            }
            else
            {
                selectionTitle.setText (project->tracks[static_cast<size_t> (selectedTrack)].name.isNotEmpty()
                                            ? project->tracks[static_cast<size_t> (selectedTrack)].name
                                            : localizer.text (trackNames[static_cast<size_t> (selectedTrack)]),
                                        juce::dontSendNotification);
                selectionSubtitle.setText (localizer.text (TextId::objectAudio), juce::dontSendNotification);
                badge.setText ("OBJ " + juce::String (selectedTrack + 1).paddedLeft ('0', 2),
                               juce::dontSendNotification);
            }
            sectionOne.setText (localizer.text (TextId::position).toUpperCase(), juce::dontSendNotification);
            placeRow (sectionOne, 20);
            placeRow (spatialEnabled, 31);
            for (auto* row : { &azimuth, &elevation, &distance, &orbitSpeed, &spread, &directivity }) placeRow (*row, 47);
            placeRow (attenuation, 31);
            placeRow (airAbsorption, 31);
            if (state.selectedSpatialRegionId != 0)
            {
                sectionTwo.setText (
                    localizer.getLanguage() == Language::chinese
                        ? juce::String::fromUTF8 ("局部覆盖与衔接")
                        : juce::String ("LOCAL OVERRIDE & TRANSITION"),
                    juce::dontSendNotification);
                placeRow (sectionTwo, 20);
                placeRow (regionGain, 47);
                placeRow (regionTransition, 47);
            }
            break;
        }
        case InspectorTab::clip:
        {
            const auto project = audioEngine.getProjectSnapshot();
            const auto& selectedTrack = project->tracks[static_cast<size_t> (
                juce::jlimit (0, audioEngine.getTrackCount() - 1, state.selectedTrack))];
            const auto selectedClip = std::find_if (selectedTrack.clips.begin(), selectedTrack.clips.end(),
                                                    [this] (const auto& clip)
            {
                return clip.id == state.selectedClipId;
            });
            if (selectedClip != selectedTrack.clips.end() && selectedClip->source != nullptr)
            {
                selectionTitle.setText (selectedClip->source->name, juce::dontSendNotification);
                selectionSubtitle.setText (juce::String (selectedClip->source->sampleRate / 1000.0, 1)
                                               + " kHz  |  " + juce::String (selectedClip->duration, 2) + " s",
                                           juce::dontSendNotification);
            }
            else
            {
                selectionTitle.setText (localizer.text (TextId::selection), juce::dontSendNotification);
                selectionSubtitle.setText (localizer.text (TextId::noAudioLoaded), juce::dontSendNotification);
            }
            badge.setText ("CLIP", juce::dontSendNotification);
            sectionOne.setText (localizer.text (TextId::timeAndPitch).toUpperCase(), juce::dontSendNotification);
            placeRow (sectionOne, 20);
            placeRow (clipSpeed, 47);
            placeRow (stretchMode, 31);
            placeRow (preservePitch, 31);
            placeRow (preserveFormants, 31);
            placeRow (transientProtection, 47);
            sectionTwo.setText (localizer.text (TextId::level).toUpperCase(), juce::dontSendNotification);
            placeRow (sectionTwo, 20);
            placeRow (clipGain, 47);
            placeRow (clipPitch, 47);
            break;
        }
        case InspectorTab::effects:
            selectionTitle.setText ("Lead Vox", juce::dontSendNotification);
            selectionSubtitle.setText (localizer.text (TextId::insertChain), juce::dontSendNotification);
            badge.setText ("3 FX", juce::dontSendNotification);
            for (auto& insert : inserts) placeRow (insert, 34);
            placeRow (eqFrequency, 47);
            placeRow (eqGain, 47);
            placeRow (threshold, 47);
            placeRow (ratio, 47);
            break;
        case InspectorTab::track:
        {
            const auto project = audioEngine.getProjectSnapshot();
            const std::array<TextId, AudioEngine::trackCount> trackNames {
                TextId::leadVocal, TextId::synth, TextId::drums, TextId::atmosphere, TextId::fxReturn
            };
            const auto selectedTrack = juce::jlimit (0, audioEngine.getTrackCount() - 1, state.selectedTrack);
            const auto& selectedTrackData = project->tracks[static_cast<size_t> (selectedTrack)];
            selectionTitle.setText (selectedTrackData.name.isNotEmpty()
                                        ? selectedTrackData.name
                                        : localizer.text (trackNames[static_cast<size_t> (selectedTrack)]),
                                    juce::dontSendNotification);
            selectionSubtitle.setText ("Track " + juce::String (state.selectedTrack + 1),
                                       juce::dontSendNotification);
            badge.setText ("MONO", juce::dontSendNotification);
            placeRow (input, 31);
            placeRow (output, 31);
            placeRow (trackVolume, 47);
            placeRow (pan, 47);
            placeRow (sendA, 47);
            placeRow (sendB, 47);
            placeRow (phaseInvert, 31);
            placeRow (monitor, 31);
            placeRow (automationRead, 31);
            break;
        }
    }
}

void InspectorPanel::setTab (InspectorTab tab)
{
    state.inspectorTab = tab;
    setVisiblePage();
    resized();
    repaint();
}

void InspectorPanel::setVisiblePage()
{
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setToggleState (static_cast<int> (state.inspectorTab) == static_cast<int> (i), juce::dontSendNotification);
}

MixerComponent::MixerComponent (Localizer& strings, AppState& appState, AudioEngine& engine)
    : localizer (strings), state (appState), audioEngine (engine)
{
    const std::array<double, 6> faderValues { -2.4, -5.1, -1.8, -8.6, -12.0, -0.8 };
    const std::array<float, 6> levels { 0.78f, 0.59f, 0.88f, 0.40f, 0.36f, 0.84f };

    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto& channel = channels[i];
        const auto initialFader = i < faderValues.size() ? faderValues[i] : -12.0;
        channel.value.setText (juce::String (initialFader, 1), juce::dontSendNotification);
        channel.value.setJustificationType (juce::Justification::centredRight);
        channel.value.setFont (juce::FontOptions (8.0f));
        channel.name.setJustificationType (juce::Justification::centredLeft);
        channel.name.setFont (juce::FontOptions (9.0f));
        channel.fader.setSliderStyle (juce::Slider::LinearVertical);
        channel.fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        channel.fader.setRange (-60.0, 12.0, 0.1);
        channel.fader.setValue (initialFader);
        channel.pan.setSliderStyle (juce::Slider::LinearHorizontal);
        channel.pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        channel.pan.setRange (-100.0, 100.0, 1.0);
        channel.pan.setValue (i == 1 ? -14.0 : i == 3 ? 28.0 : 0.0);
        channel.mute.setClickingTogglesState (true);
        channel.solo.setClickingTogglesState (true);
        channel.level = i < levels.size() ? levels[i] : 0.32f;
        channel.fader.onValueChange = [this, i]
        {
            const auto value = channels[i].fader.getValue();
            channels[i].value.setText (juce::String (value, 1), juce::dontSendNotification);
            if (i < AudioEngine::maximumTrackCount)
                audioEngine.setTrackGainDb (static_cast<int> (i), static_cast<float> (value));
            else
                audioEngine.setMasterGainDb (static_cast<float> (value));
        };
        channel.pan.onValueChange = [this, i]
        {
            if (i < AudioEngine::maximumTrackCount)
                audioEngine.setTrackPan (static_cast<int> (i),
                                         static_cast<float> (channels[i].pan.getValue() / 100.0));
        };
        channel.mute.onClick = [this, i]
        {
            if (i < AudioEngine::maximumTrackCount)
                audioEngine.setTrackMuted (static_cast<int> (i), channels[i].mute.getToggleState());
        };
        channel.solo.onClick = [this, i]
        {
            if (i < AudioEngine::maximumTrackCount)
                audioEngine.setTrackSolo (static_cast<int> (i), channels[i].solo.getToggleState());
        };
        const std::array<juce::Component*, 6> channelComponents {
            &channel.name, &channel.value, &channel.fader, &channel.pan, &channel.mute, &channel.solo
        };
        for (auto* component : channelComponents)
            addAndMakeVisible (*component);
    }

    refreshText();
}

void MixerComponent::refreshText()
{
    const auto project = audioEngine.getProjectSnapshot();
    const auto activeTracks = audioEngine.getTrackCount();
    for (size_t i = 0; i < channels.size(); ++i)
    {
        if (i < static_cast<size_t> (activeTracks))
            channels[i].name.setText (project->tracks[i].name.isNotEmpty()
                                           ? project->tracks[i].name
                                           : "Track " + juce::String (i + 1),
                                       juce::dontSendNotification);
        else if (i == channels.size() - 1)
            channels[i].name.setText (localizer.text (TextId::master), juce::dontSendNotification);
        else
            channels[i].name.setText ({}, juce::dontSendNotification);
    }
    repaint();
}

void MixerComponent::syncFromEngine()
{
    const auto project = audioEngine.getProjectSnapshot();
    const auto activeTrackCount = audioEngine.getTrackCount();
    if (laidOutTrackCount != activeTrackCount)
        resized();
    for (int i = 0; i < activeTrackCount; ++i)
    {
        const auto& track = project->tracks[static_cast<size_t> (i)];
        auto& channel = channels[static_cast<size_t> (i)];
        channel.fader.setValue (track.gainDb, juce::dontSendNotification);
        channel.pan.setValue (track.pan * 100.0f, juce::dontSendNotification);
        channel.mute.setToggleState (track.muted, juce::dontSendNotification);
        channel.solo.setToggleState (track.solo, juce::dontSendNotification);
        channel.value.setText (juce::String (track.gainDb, 1), juce::dontSendNotification);
    }
    channels.back().fader.setValue (project->masterGainDb, juce::dontSendNotification);
    channels.back().value.setText (juce::String (project->masterGainDb, 1),
                                   juce::dontSendNotification);
    repaint();
}

void MixerComponent::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.fillAll (c.panel);
    g.setColour (c.line);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

    const auto sideWidth = getWidth() < 600 ? 82 : 126;
    g.setColour (c.line);
    g.drawVerticalLine (sideWidth - 1, 0.0f, static_cast<float> (getHeight()));
    g.setColour (c.muted);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (localizer.text (TextId::mixer), 9, 5, sideWidth - 18, 20,
                juce::Justification::centredLeft, false);

    auto meterBounds = juce::Rectangle<float> (9.0f, 31.0f, static_cast<float> (sideWidth - 18),
                                                static_cast<float> (getHeight() - 40));
    g.setColour (c.background);
    g.fillRoundedRectangle (meterBounds, 3.0f);
    const auto masterLevel = audioEngine.hasFile() ? audioEngine.getMasterMeter() : 0.78f;
    const std::array<float, 2> levels { masterLevel, masterLevel * 0.92f };
    for (size_t i = 0; i < levels.size(); ++i)
    {
        auto column = juce::Rectangle<float> (meterBounds.getX() + 6.0f + static_cast<float> (i) * (meterBounds.getWidth() - 14.0f) * 0.5f,
                                              meterBounds.getY() + 4.0f,
                                              (meterBounds.getWidth() - 18.0f) * 0.5f,
                                              meterBounds.getHeight() - 8.0f);
        auto active = column.removeFromBottom (column.getHeight() * levels[i]);
        g.setGradientFill (juce::ColourGradient (c.red, active.getX(), active.getY(), c.green,
                                                 active.getX(), active.getBottom(), false));
        g.fillRoundedRectangle (active, 1.0f);
    }

    const auto channelArea = getLocalBounds().withTrimmedLeft (sideWidth);
    const auto channelCount = juce::jmax (1, audioEngine.getTrackCount() + 1);
    const auto width = channelArea.getWidth() / channelCount;
    for (int i = 0; i < channelCount; ++i)
    {
        const auto x = channelArea.getX() + i * width;
        if (i == channelCount - 1)
        {
            g.setColour (c.raised);
            g.fillRect (x, 0, getWidth() - x, getHeight());
        }
        if (i > 0)
        {
            g.setColour (c.lineSoft);
            g.drawVerticalLine (x, 0.0f, static_cast<float> (getHeight()));
        }

        auto meter = juce::Rectangle<float> (static_cast<float> (x + width - 11), 47.0f,
                                              5.0f, static_cast<float> (getHeight() - 76));
        g.setColour (c.background);
        g.fillRoundedRectangle (meter, 1.0f);
        const auto isMaster = i == channelCount - 1;
        const auto& channel = isMaster ? channels.back() : channels[static_cast<size_t> (i)];
        const auto level = audioEngine.hasFile()
            ? (isMaster ? audioEngine.getMasterMeter() : audioEngine.getTrackMeter (i))
            : channel.level;
        g.setColour (c.green);
        g.fillRoundedRectangle (meter.removeFromBottom (meter.getHeight() * juce::jlimit (0.0f, 1.0f, level)), 1.0f);
    }
}

void MixerComponent::resized()
{
    const auto sideWidth = getWidth() < 600 ? 82 : 126;
    auto area = getLocalBounds().withTrimmedLeft (sideWidth);
    const auto activeTrackCount = audioEngine.getTrackCount();
    laidOutTrackCount = activeTrackCount;
    const auto channelCount = juce::jmax (1, activeTrackCount + 1);
    const auto width = area.getWidth() / channelCount;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        const auto isMaster = i == channels.size() - 1;
        const auto visible = static_cast<int> (i) < activeTrackCount || isMaster;
        const std::array<juce::Component*, 6> channelComponents {
            &channels[i].name, &channels[i].value, &channels[i].fader,
            &channels[i].pan, &channels[i].mute, &channels[i].solo
        };
        for (auto* component : channelComponents)
            component->setVisible (visible);
        if (! visible)
            continue;
        const auto columnIndex = isMaster ? activeTrackCount : static_cast<int> (i);
        auto column = juce::Rectangle<int> (area.getX() + columnIndex * width, 0,
                                            isMaster ? getWidth() - area.getX() - columnIndex * width : width,
                                            getHeight()).reduced (7, 5);
        auto heading = column.removeFromTop (20);
        channels[i].value.setBounds (heading.removeFromRight (36));
        channels[i].name.setBounds (heading);
        channels[i].pan.setBounds (column.removeFromTop (20));
        auto actions = column.removeFromBottom (22);
        const auto half = actions.getWidth() / 2;
        channels[i].mute.setBounds (actions.removeFromLeft (half).reduced (1, 2));
        channels[i].solo.setBounds (actions.reduced (1, 2));
        channels[i].fader.setBounds (column.withTrimmedRight (8));
    }
}

LayoutPanel::LayoutPanel (Localizer& strings, AppState& appState)
    : localizer (strings), state (appState)
{
    title.setFont (juce::FontOptions (12.0f));
    title.setJustificationType (juce::Justification::centredLeft);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (title);
    addAndMakeVisible (closeButton);

    for (auto* toggle : { &browserVisible, &inspectorVisible, &mixerVisible })
    {
        toggle->onClick = [this]
        {
            state.layout.browserVisible = browserVisible.getToggleState();
            state.layout.inspectorVisible = inspectorVisible.getToggleState();
            state.layout.mixerVisible = mixerVisible.getToggleState();
            if (onLayoutChanged) onLayoutChanged();
        };
        addAndMakeVisible (*toggle);
    }

    for (auto* label : { &browserLabel, &inspectorLabel, &trackLabel, &mixerLabel, &densityLabel,
                         &browserValue, &inspectorValue, &trackValue, &mixerValue })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }
    for (auto* value : { &browserValue, &inspectorValue, &trackValue, &mixerValue })
        value->setJustificationType (juce::Justification::centredRight);

    configureSlider (browserWidth, 130, 260);
    configureSlider (inspectorWidth, 210, 380);
    configureSlider (trackHeight, 44, 104);
    configureSlider (mixerHeight, 110, 300);

    browserWidth.onValueChange = [this] { state.layout.browserWidth = juce::roundToInt (browserWidth.getValue()); updateOutputs(); if (onLayoutChanged) onLayoutChanged(); };
    inspectorWidth.onValueChange = [this] { state.layout.inspectorWidth = juce::roundToInt (inspectorWidth.getValue()); updateOutputs(); if (onLayoutChanged) onLayoutChanged(); };
    trackHeight.onValueChange = [this] { state.layout.trackHeight = juce::roundToInt (trackHeight.getValue()); updateOutputs(); if (onLayoutChanged) onLayoutChanged(); };
    mixerHeight.onValueChange = [this] { state.layout.mixerHeight = juce::roundToInt (mixerHeight.getValue()); updateOutputs(); if (onLayoutChanged) onLayoutChanged(); };

    for (size_t i = 0; i < densityButtons.size(); ++i)
    {
        configureTab (densityButtons[i]);
        densityButtons[i].onClick = [this, i]
        {
            state.layout.density = static_cast<int> (i);
            syncFromState();
            if (onLayoutChanged) onLayoutChanged();
        };
        addAndMakeVisible (densityButtons[i]);
    }

    resetButton.onClick = [this]
    {
        state.layout = {};
        syncFromState();
        if (onLayoutChanged) onLayoutChanged();
    };
    doneButton.getProperties().set ("accent", true);
    doneButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (resetButton);
    addAndMakeVisible (doneButton);

    refreshText();
    syncFromState();
}

void LayoutPanel::configureSlider (juce::Slider& slider, double min, double max)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (min, max, 1.0);
    addAndMakeVisible (slider);
}

void LayoutPanel::refreshText()
{
    title.setText (localizer.text (TextId::workspaceLayout), juce::dontSendNotification);
    browserVisible.setButtonText (localizer.text (TextId::media));
    inspectorVisible.setButtonText (localizer.text (TextId::inspector));
    mixerVisible.setButtonText (localizer.text (TextId::mixer));
    browserLabel.setText (localizer.text (TextId::browserWidth), juce::dontSendNotification);
    inspectorLabel.setText (localizer.text (TextId::inspectorWidth), juce::dontSendNotification);
    trackLabel.setText (localizer.text (TextId::trackHeight), juce::dontSendNotification);
    mixerLabel.setText (localizer.text (TextId::mixerHeight), juce::dontSendNotification);
    densityLabel.setText (localizer.text (TextId::density), juce::dontSendNotification);
    densityButtons[0].setButtonText (localizer.text (TextId::compact));
    densityButtons[1].setButtonText (localizer.text (TextId::comfortable));
    densityButtons[2].setButtonText (localizer.text (TextId::spacious));
    resetButton.setButtonText (localizer.text (TextId::reset));
    doneButton.setButtonText (localizer.text (TextId::done));
    repaint();
}

void LayoutPanel::syncFromState()
{
    browserVisible.setToggleState (state.layout.browserVisible, juce::dontSendNotification);
    inspectorVisible.setToggleState (state.layout.inspectorVisible, juce::dontSendNotification);
    mixerVisible.setToggleState (state.layout.mixerVisible, juce::dontSendNotification);
    browserWidth.setValue (state.layout.browserWidth, juce::dontSendNotification);
    inspectorWidth.setValue (state.layout.inspectorWidth, juce::dontSendNotification);
    trackHeight.setValue (state.layout.trackHeight, juce::dontSendNotification);
    mixerHeight.setValue (state.layout.mixerHeight, juce::dontSendNotification);
    for (size_t i = 0; i < densityButtons.size(); ++i)
        densityButtons[i].setToggleState (state.layout.density == static_cast<int> (i), juce::dontSendNotification);
    updateOutputs();
}

void LayoutPanel::updateOutputs()
{
    browserValue.setText (juce::String (state.layout.browserWidth) + " px", juce::dontSendNotification);
    inspectorValue.setText (juce::String (state.layout.inspectorWidth) + " px", juce::dontSendNotification);
    trackValue.setText (juce::String (state.layout.trackHeight) + " px", juce::dontSendNotification);
    mixerValue.setText (juce::String (state.layout.mixerHeight) + " px", juce::dontSendNotification);
}

void LayoutPanel::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    g.setColour (c.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (c.line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.drawHorizontalLine (38, 0.0f, static_cast<float> (getWidth()));
}

void LayoutPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (39).reduced (9, 4);
    closeButton.setBounds (top.removeFromRight (30));
    title.setBounds (top);
    area = area.reduced (10, 7);

    auto toggles = area.removeFromTop (35);
    const auto toggleWidth = toggles.getWidth() / 3;
    browserVisible.setBounds (toggles.removeFromLeft (toggleWidth));
    inspectorVisible.setBounds (toggles.removeFromLeft (toggleWidth));
    mixerVisible.setBounds (toggles);

    auto layoutSlider = [&area] (juce::Label& label, juce::Slider& slider, juce::Label& value)
    {
        auto row = area.removeFromTop (37);
        label.setBounds (row.removeFromLeft (112));
        value.setBounds (row.removeFromRight (50));
        slider.setBounds (row.reduced (4, 5));
    };
    layoutSlider (browserLabel, browserWidth, browserValue);
    layoutSlider (inspectorLabel, inspectorWidth, inspectorValue);
    layoutSlider (trackLabel, trackHeight, trackValue);
    layoutSlider (mixerLabel, mixerHeight, mixerValue);

    densityLabel.setBounds (area.removeFromTop (22));
    auto density = area.removeFromTop (32);
    const auto densityWidth = density.getWidth() / 3;
    densityButtons[0].setBounds (density.removeFromLeft (densityWidth).reduced (1, 2));
    densityButtons[1].setBounds (density.removeFromLeft (densityWidth).reduced (1, 2));
    densityButtons[2].setBounds (density.reduced (1, 2));

    auto actions = area.removeFromBottom (33);
    doneButton.setBounds (actions.removeFromRight (72).reduced (2));
    resetButton.setBounds (actions.removeFromRight (72).reduced (2));
}

ExportPanel::ExportPanel (Localizer& strings)
    : localizer (strings)
{
    const std::array<juce::Component*, 11> components {
        &title, &formatLabel, &sampleRateLabel, &bitDepthLabel, &channelLayoutLabel,
        &formatValue, &closeButton, &sampleRate, &bitDepth, &channelLayout, &exportAction
    };
    for (auto* component : components)
        addAndMakeVisible (*component);

    title.setFont (juce::FontOptions (14.0f));
    title.setJustificationType (juce::Justification::centredLeft);
    formatValue.setText ("WAV (PCM)", juce::dontSendNotification);
    formatValue.setJustificationType (juce::Justification::centredRight);

    sampleRate.addItem ("44.1 kHz", 1);
    sampleRate.addItem ("48 kHz", 2);
    sampleRate.addItem ("88.2 kHz", 3);
    sampleRate.addItem ("96 kHz", 4);
    sampleRate.setSelectedId (2, juce::dontSendNotification);

    bitDepth.addItem ("16-bit PCM", 1);
    bitDepth.addItem ("24-bit PCM", 2);
    bitDepth.addItem ("32-bit PCM", 3);
    bitDepth.setSelectedId (2, juce::dontSendNotification);

    channelLayout.addItem ("Stereo (2 ch)", 1);
    channelLayout.addItem ("5.1 (6 ch)", 2);
    channelLayout.addItem ("7.1 (8 ch)", 3);
    channelLayout.addItem ("5.1.4 (10 ch)", 4);
    channelLayout.addItem ("7.1.4 (12 ch)", 5);
    channelLayout.setSelectedId (1, juce::dontSendNotification);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    exportAction.onClick = [this]
    {
        if (onExport)
            onExport (getSettings());
    };
    refreshText();
}

void ExportPanel::refreshText()
{
    title.setText (localizer.text (TextId::exportAudio), juce::dontSendNotification);
    formatLabel.setText (localizer.text (TextId::exportFormat), juce::dontSendNotification);
    sampleRateLabel.setText (localizer.text (TextId::sampleRate), juce::dontSendNotification);
    bitDepthLabel.setText (localizer.text (TextId::bitDepth), juce::dontSendNotification);
    channelLayoutLabel.setText (localizer.text (TextId::channelLayout), juce::dontSendNotification);
    exportAction.setButtonText (localizer.text (TextId::exportProject));
    closeButton.setTitle (localizer.text (TextId::close));
    repaint();
}

void ExportPanel::paint (juce::Graphics& g)
{
    const auto& c = coloursOf (*this);
    auto bounds = getLocalBounds().toFloat();
    g.setColour (c.panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (c.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
    g.drawHorizontalLine (43, 0.0f, static_cast<float> (getWidth()));
    g.setColour (c.muted);
    g.setFont (juce::FontOptions (9.0f));
    const auto description = localizer.getLanguage() == Language::chinese
                               ? juce::String::fromUTF8 ("离线渲染完整时间线，包含多轨混音与扬声器空间参数。")
                               : juce::String ("Offline render of the full timeline, including multitrack and speaker spatial processing.");
    g.drawFittedText (description, 18, 51, getWidth() - 36, 34,
                      juce::Justification::topLeft, 2);
}

void ExportPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (44);
    closeButton.setBounds (header.removeFromRight (38).reduced (5));
    title.setBounds (header.reduced (17, 0));
    area.removeFromTop (47);

    const auto layoutRow = [this] (juce::Rectangle<int>& remaining, juce::Label& label,
                                   juce::Component& control)
    {
        auto row = remaining.removeFromTop (42).reduced (17, 4);
        label.setBounds (row.removeFromLeft (112));
        control.setBounds (row);
        remaining.removeFromTop (2);
    };
    layoutRow (area, formatLabel, formatValue);
    layoutRow (area, sampleRateLabel, sampleRate);
    layoutRow (area, bitDepthLabel, bitDepth);
    layoutRow (area, channelLayoutLabel, channelLayout);
    exportAction.setBounds (area.removeFromBottom (48).reduced (17, 7));
}

AudioEngine::ExportSettings ExportPanel::getSettings() const
{
    static constexpr std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
    static constexpr std::array<int, 3> depths { 16, 24, 32 };
    static constexpr std::array<int, 5> channels { 2, 6, 8, 10, 12 };
    AudioEngine::ExportSettings settings;
    settings.sampleRate = rates[static_cast<size_t> (juce::jlimit (1, 4, sampleRate.getSelectedId()) - 1)];
    settings.bitsPerSample = depths[static_cast<size_t> (juce::jlimit (1, 3, bitDepth.getSelectedId()) - 1)];
    settings.channelCount = channels[static_cast<size_t> (juce::jlimit (1, 5, channelLayout.getSelectedId()) - 1)];
    return settings;
}

MainComponent::MainComponent()
    : localizer (state.language), audioEngine (initialiseAudioDuringConstruction),
      browser (localizer, audioEngine),
      timeline (localizer, state, audioEngine), spatialCanvas (localizer, state, audioEngine),
      inspector (localizer, state, audioEngine), mixer (localizer, state, audioEngine),
      layoutPanel (localizer, state), exportPanel (localizer)
{
    setLookAndFeel (&studioLook);
    tooltipWindow.setMillisecondsBeforeTipAppears (450);
    setOpaque (true);

    brandLabel.setText ("0i  STUDIO", juce::dontSendNotification);
    brandLabel.setFont (juce::FontOptions (16.0f));
    brandLabel.setJustificationType (juce::Justification::centredLeft);
    projectLabel.setText ("Midnight Bloom", juce::dontSendNotification);
    projectLabel.setJustificationType (juce::Justification::centred);
    projectLabel.setFont (juce::FontOptions (12.0f));
    savedLabel.setJustificationType (juce::Justification::centredLeft);
    savedLabel.setFont (juce::FontOptions (9.0f));
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setFont (juce::FontOptions (13.0f));
    barsLabel.setText ("33.2.04", juce::dontSendNotification);
    barsLabel.setJustificationType (juce::Justification::centred);
    barsLabel.setFont (juce::FontOptions (8.0f));

    const std::array<juce::Component*, 36> mainComponents {
        &brandLabel, &projectLabel, &savedLabel, &timeLabel, &barsLabel,
        &fileMenu, &editMenu, &trackMenu, &clipMenu, &viewMenu,
        &chineseButton, &englishButton, &tempoButton, &speedButton, &pitchLockButton,
        &themeButton, &settingsButton, &moreButton, &previousButton, &playButton, &stopButton,
        &recordButton, &loopButton, &exportButton, &browserToggle, &mixerToggle,
        &inspectorToggle, &layoutButton, &browser, &timeline, &spatialCanvas,
        &inspector, &mixer, &statusLeft, &statusRight, &clipContextToolbar
    };
    for (auto* component : mainComponents)
        addAndMakeVisible (*component);
    clipContextToolbar.setVisible (false);

    fileMenu.onClick = [this] { showFileMenu(); };
    editMenu.onClick = [this] { showEditMenu(); };
    trackMenu.onClick = [this] { showTrackMenu(); };
    clipMenu.onClick = [this] { showClipMenu(); };
    viewMenu.onClick = [this] { showViewMenu(); };
    chineseButton.onClick = [this]
    {
        state.language = Language::chinese;
        localizer.setLanguage (state.language);
        refreshText();
    };
    englishButton.onClick = [this]
    {
        state.language = Language::english;
        localizer.setLanguage (state.language);
        refreshText();
    };
    themeButton.onClick = [this]
    {
        state.theme = state.theme == ThemeMode::dark ? ThemeMode::light : ThemeMode::dark;
        applyTheme();
    };
    settingsButton.onClick = [this] { showAudioDeviceSettings(); };
    moreButton.onClick = [this] { showMobileMenu(); };

    const std::array<Icon, 8> toolIcons { Icon::undo, Icon::redo, Icon::pointer, Icon::range,
                                          Icon::scissors, Icon::fade, Icon::magnet, Icon::search };
    for (size_t i = 0; i < toolButtons.size(); ++i)
    {
        toolButtons[i] = std::make_unique<IconButton> (toolIcons[i], "Tool");
        addAndMakeVisible (*toolButtons[i]);
    }
    toolButtons[0]->onClick = [this]
    {
        if (audioEngine.undo())
            resetSelectionAfterHistoryChange();
    };
    toolButtons[1]->onClick = [this]
    {
        if (audioEngine.redo())
            resetSelectionAfterHistoryChange();
    };
    toolButtons[2]->setToggleState (true, juce::dontSendNotification);
    toolButtons[6]->setToggleState (true, juce::dontSendNotification);
    for (size_t i = 2; i <= 6; ++i)
    {
        toolButtons[i]->onClick = [this, i]
        {
            if (i == 6)
            {
                timeline.setSnappingEnabled (! timeline.isSnappingEnabled());
                toolButtons[6]->setToggleState (timeline.isSnappingEnabled(),
                                                juce::dontSendNotification);
                return;
            }
            state.activeTool = static_cast<Tool> (i - 2);
            if (state.activeTool != Tool::range)
            {
                state.selectedSpatialRegionId = 0;
                state.selectedRangeStart = 0.0;
                state.selectedRangeEnd = 0.0;
                syncSpatialStateFromSelection();
                inspector.syncFromState();
            }
            for (size_t button = 2; button <= 5; ++button)
                toolButtons[button]->setToggleState (button == i, juce::dontSendNotification);
        };
    }

    clipContextToolbar.moveButton.onClick = [this]
    {
        toolButtons[2]->triggerClick();
    };
    clipContextToolbar.splitButton.onClick = [this]
    {
        toolButtons[4]->triggerClick();
    };
    clipContextToolbar.duplicateButton.onClick = [this]
    {
        duplicateSelectedClip();
    };
    clipContextToolbar.spatialButton.onClick = [this]
    {
        state.inspectorTab = InspectorTab::spatial;
        inspector.setTab (state.inspectorTab);
        syncSpatialStateFromSelection();
        inspector.syncFromState();
        setPanelVisible ("inspector", true);
    };
    clipContextToolbar.deleteButton.onClick = [this]
    {
        deleteSelectedClip();
    };

    playButton.setAccent (true);
    playButton.onClick = [this]
    {
        if (audioEngine.isPlaying()) audioEngine.stop(); else audioEngine.play();
        updateTransport();
    };
    stopButton.onClick = [this]
    {
        audioEngine.stop();
        audioEngine.setPosition (0.0);
        updateTransport();
    };
    previousButton.onClick = [this] { audioEngine.setPosition (0.0); };
    loopButton.onClick = [this]
    {
        state.looping = ! state.looping;
        audioEngine.setLooping (state.looping);
        loopButton.setToggleState (state.looping, juce::dontSendNotification);
    };
    exportButton.onClick = [this] { showExportPanel(); };
    pitchLockButton.setClickingTogglesState (true);
    pitchLockButton.setToggleState (state.pitchLocked, juce::dontSendNotification);
    pitchLockButton.onClick = [this] { state.pitchLocked = pitchLockButton.getToggleState(); };
    speedButton.onClick = [this]
    {
        const std::array<double, 5> speeds { 0.5, 0.75, 1.0, 1.25, 1.5 };
        const auto iterator = std::find_if (speeds.begin(), speeds.end(), [this] (double speed)
        {
            return std::abs (speed - state.playbackSpeed) < 0.001;
        });
        const auto index = iterator == speeds.end() ? 2u : static_cast<size_t> (iterator - speeds.begin());
        setPlaybackSpeed (speeds[(index + 1) % speeds.size()]);
    };

    for (size_t i = 0; i < workspaceButtons.size(); ++i)
    {
        configureTab (workspaceButtons[i]);
        workspaceButtons[i].onClick = [this, i] { applyWorkspace (static_cast<Workspace> (i)); };
        addAndMakeVisible (workspaceButtons[i]);
    }

    browserToggle.onClick = [this] { showMobilePanel ("browser"); };
    mixerToggle.onClick = [this] { showMobilePanel ("mixer"); };
    inspectorToggle.onClick = [this] { showMobilePanel ("inspector"); };
    layoutButton.onClick = [this]
    {
        exportPanel.setVisible (false);
        layoutPanel.setVisible (! layoutPanel.isVisible());
        if (layoutPanel.isVisible()) layoutPanel.toFront (true);
        resized();
    };

    layoutPanel.onClose = [this] { layoutPanel.setVisible (false); repaint(); };
    layoutPanel.onLayoutChanged = [this]
    {
        updateToggleStates();
        resized();
        repaint();
    };
    addAndMakeVisible (layoutPanel);
    layoutPanel.setVisible (false);

    exportPanel.onClose = [this] { exportPanel.setVisible (false); repaint(); };
    exportPanel.onExport = [this] (AudioEngine::ExportSettings settings)
    {
        exportPanel.setVisible (false);
        chooseExportDestination (settings);
    };
    addAndMakeVisible (exportPanel);
    exportPanel.setVisible (false);

    browser.onImportRequested = [this] { importMediaFiles(); };
    browser.onDirectoryRequested = [this] { chooseMediaDirectory(); };
    browser.onAudioFileRequested = [this] (const juce::File& file) { importAudioFile (file); };
    browser.onProjectClipSelected = [this] (int trackIndex, uint64_t clipId)
    {
        selectProjectClip (trackIndex, clipId);
    };
    browser.onSpatialPresetSelected = [this] (const SpatialParameters& preset)
    {
        state.spatial = preset;
        applySpatialStateToSelection();
        inspector.setTab (InspectorTab::spatial);
        inspector.syncFromState();
        spatialCanvas.repaint();
        timeline.repaint();
    };
    timeline.onTrackSelected = [this] (int trackIndex)
    {
        if (state.activeTool == Tool::range && state.selectedClipId != 0)
        {
            juce::ignoreUnused (trackIndex);
            return;
        }
        state.inspectorTab = InspectorTab::track;
        inspector.setTab (state.inspectorTab);
        syncSpatialStateFromSelection();
        inspector.syncFromState();
        juce::ignoreUnused (trackIndex);
    };
    timeline.onClipSelected = [this] (uint64_t clipId)
    {
        if (clipId != 0)
            inspector.setTab (InspectorTab::clip);
        if (isPhoneLandscapeLayout())
            resized();
    };
    timeline.onSpatialRangeSelected = [this]
    {
        syncSpatialStateFromSelection();
        inspector.setTab (InspectorTab::spatial);
        inspector.syncFromState();
        spatialCanvas.repaint();
    };
    timeline.onTrackControlsChanged = [this] { mixer.syncFromEngine(); };
    timeline.onAddTrackRequested = [this] { addTrack(); };
    timeline.onRenameTrackRequested = [this] (int trackIndex)
    {
        state.selectedTrack = trackIndex;
        renameSelectedTrack();
    };
    spatialCanvas.onParametersChanged = [this]
    {
        applySpatialStateToSelection();
        inspector.syncFromState();
        spatialCanvas.repaint();
    };
    inspector.onSpatialChanged = [this]
    {
        applySpatialStateToSelection();
        spatialCanvas.repaint();
    };
    inspector.onSpatialRegionEnvelopeChanged = [this] (float gainDb, double transitionSeconds)
    {
        if (state.selectedSpatialRegionId == 0)
            return;

        if (! audioEngine.setClipSpatialRegionEnvelope (
                state.selectedTrack, state.selectedClipId, state.selectedSpatialRegionId,
                gainDb, transitionSeconds))
        {
            state.selectedSpatialRegionId = 0;
            syncSpatialStateFromSelection();
            inspector.syncFromState();
        }
        timeline.repaint();
    };
    inspector.onDeleteSpatialRegion = [this] { timeline.deleteSelectedSpatialRegion(); };
    inspector.onPlaybackSpeedChanged = [this] (double speed) { setPlaybackSpeed (speed); };
    inspector.onTrackGainChanged = [this] (double) { mixer.syncFromEngine(); };
    inspector.onTrackPanChanged = [this] (double) { mixer.syncFromEngine(); };

    audioEngine.addChangeListener (this);
    statusLeft.setJustificationType (juce::Justification::centredLeft);
    statusRight.setJustificationType (juce::Justification::centredRight);
    statusLeft.setFont (juce::FontOptions (8.5f));
    statusRight.setFont (juce::FontOptions (8.5f));

    applyTheme();
    audioEngine.setPlaybackRate (state.playbackSpeed);
    syncSpatialStateFromSelection();
    mixer.syncFromEngine();
    applyWorkspace (Workspace::edit);
    refreshText();
    startTimerHz (30);
    setSize (1280, 800);

    const juce::Component::SafePointer<MainComponent> updateSafeThis (this);
    juce::Timer::callAfterDelay (1800, [updateSafeThis]
    {
        if (auto* component = updateSafeThis.getComponent())
            component->checkForUpdates (false);
    });

   #if JUCE_ANDROID
    juce::Logger::writeToLog ("0i Studio startup: UI constructed");
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (auto* component = safeThis.getComponent())
        {
            juce::Logger::writeToLog ("0i Studio startup: initialising audio output");
            const auto error = component->audioEngine.initialiseAudioDevice();
            juce::Logger::writeToLog (error.isEmpty()
                ? "0i Studio startup: audio output ready"
                : "0i Studio startup: audio output unavailable: " + error);
        }
    });
   #endif
}

MainComponent::~MainComponent()
{
    updateThread.removeAllJobs (true, 10000);
    mediaImportThread.removeAllJobs (true, 10000);
    exportThread.removeAllJobs (true, 10000);
    audioEngine.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto& c = studioLook.colours();
    g.fillAll (c.background);
    paintTitleBar (g);
    paintToolbar (g);
    paintWorkspaceBar (g);
    paintStatusBar (g);
}

MainComponent::ResponsiveMode MainComponent::responsiveMode() const noexcept
{
    if (getWidth() < 600)
        return ResponsiveMode::phonePortrait;
    if (getHeight() < 600)
        return ResponsiveMode::phoneLandscape;
    if (getWidth() < 1024)
        return ResponsiveMode::compact;
    return ResponsiveMode::full;
}

bool MainComponent::isPhoneLayout() const noexcept
{
    const auto mode = responsiveMode();
    return mode == ResponsiveMode::phonePortrait || mode == ResponsiveMode::phoneLandscape;
}

bool MainComponent::isPhoneLandscapeLayout() const noexcept
{
    return responsiveMode() == ResponsiveMode::phoneLandscape;
}

int MainComponent::titleBarHeight() const noexcept
{
    const auto mode = responsiveMode();
    return mode == ResponsiveMode::phonePortrait ? 48
         : mode == ResponsiveMode::phoneLandscape ? 44
         : mode == ResponsiveMode::compact ? 44 : 42;
}

int MainComponent::toolbarHeight() const noexcept
{
    const auto mode = responsiveMode();
    return mode == ResponsiveMode::phonePortrait ? 92
         : mode == ResponsiveMode::phoneLandscape ? 0
         : mode == ResponsiveMode::compact ? 60 : 51;
}

int MainComponent::workspaceBarHeight() const noexcept
{
    const auto mode = responsiveMode();
    return mode == ResponsiveMode::phonePortrait ? 48
         : mode == ResponsiveMode::phoneLandscape ? 0
         : mode == ResponsiveMode::compact ? 42 : 38;
}

int MainComponent::statusBarHeight() const noexcept
{
    return isPhoneLandscapeLayout() ? 0 : isPhoneLayout() ? 30 : 26;
}

void MainComponent::paintTitleBar (juce::Graphics& g)
{
    const auto& c = studioLook.colours();
    const auto height = titleBarHeight();
    g.setColour (c.bar);
    g.fillRect (0, 0, getWidth(), height);
    g.setColour (c.line);
    g.drawHorizontalLine (height - 1, 0.0f, static_cast<float> (getWidth()));
    if (projectLabel.isVisible() && projectLabel.getRight() + 12 < getWidth())
    {
        g.setColour (c.green);
        g.fillEllipse (projectLabel.getRight() + 5.0f,
                       static_cast<float> (height) * 0.5f - 3.0f, 6.0f, 6.0f);
    }
}

void MainComponent::paintToolbar (juce::Graphics& g)
{
    const auto& c = studioLook.colours();
    const auto top = titleBarHeight();
    const auto height = toolbarHeight();
    if (height <= 0)
        return;
    g.setColour (c.panel);
    g.fillRect (0, top, getWidth(), height);
    g.setColour (c.line);
    g.drawHorizontalLine (top + height - 1, 0.0f, static_cast<float> (getWidth()));
    auto timeBounds = timeLabel.getBounds().getUnion (barsLabel.getBounds()).toFloat().expanded (4.0f, 2.0f);
    g.setColour (c.background);
    g.fillRoundedRectangle (timeBounds, 3.0f);
    g.setColour (c.line);
    g.drawRoundedRectangle (timeBounds, 3.0f, 1.0f);
}

void MainComponent::paintWorkspaceBar (juce::Graphics& g)
{
    const auto& c = studioLook.colours();
    const auto top = titleBarHeight() + toolbarHeight();
    const auto height = workspaceBarHeight();
    if (height <= 0)
        return;
    g.setColour (c.bar);
    g.fillRect (0, top, getWidth(), height);
    g.setColour (c.line);
    g.drawHorizontalLine (top + height - 1, 0.0f, static_cast<float> (getWidth()));
}

void MainComponent::paintStatusBar (juce::Graphics& g)
{
    const auto& c = studioLook.colours();
    if (statusBarHeight() <= 0)
        return;
    const auto y = getHeight() - statusBarHeight();
    g.setColour (c.bar);
    g.fillRect (0, y, getWidth(), statusBarHeight());
    g.setColour (c.line);
    g.drawHorizontalLine (y, 0.0f, static_cast<float> (getWidth()));
}

void MainComponent::resized()
{
    const auto mode = responsiveMode();
    const auto phonePortrait = mode == ResponsiveMode::phonePortrait;
    const auto phoneLandscape = mode == ResponsiveMode::phoneLandscape;
    const auto phone = phonePortrait || phoneLandscape;
    const auto compact = mode == ResponsiveMode::compact;
    const auto showMenus = ! phone && (mode == ResponsiveMode::full || getWidth() >= 820);
    const auto showLanguages = mode == ResponsiveMode::full && getWidth() >= 900;
    brandLabel.setText (phoneLandscape ? "0i" : "0i  STUDIO",
                        juce::dontSendNotification);

    if (phone)
    {
        browserToggle.setToggleState (mobilePanel == "browser", juce::dontSendNotification);
        inspectorToggle.setToggleState (mobilePanel == "inspector", juce::dontSendNotification);
        mixerToggle.setToggleState (mobilePanel == "mixer", juce::dontSendNotification);
    }

    auto area = getLocalBounds();
    auto title = area.removeFromTop (titleBarHeight());
    auto toolbar = area.removeFromTop (toolbarHeight());
    auto workspaceBar = area.removeFromTop (workspaceBarHeight());
    auto status = area.removeFromBottom (statusBarHeight());

    const auto clearBounds = [] (juce::Component& component)
    {
        component.setBounds ({ });
    };

    if (phoneLandscape)
    {
        brandLabel.setBounds (title.removeFromLeft (88).reduced (8, 5));

        auto titleRight = title.removeFromRight (116);
        moreButton.setVisible (true);
        moreButton.setBounds (titleRight.removeFromRight (44).reduced (3, 5));
        toolButtons[1]->setBounds (titleRight.removeFromRight (36).reduced (2, 6));
        toolButtons[0]->setBounds (titleRight.removeFromRight (36).reduced (2, 6));

        settingsButton.setVisible (false);
        themeButton.setVisible (false);
        clearBounds (settingsButton);
        clearBounds (themeButton);

        const auto projectWidth = title.getWidth() >= 390
                                    ? juce::jmin (150, title.getWidth() / 3) : 0;
        if (projectWidth > 0)
        {
            projectLabel.setVisible (true);
            projectLabel.setBounds (title.removeFromLeft (projectWidth).reduced (2, 4));
        }
        else
        {
            projectLabel.setVisible (false);
            clearBounds (projectLabel);
        }

        auto transport = title.withSizeKeepingCentre (juce::jmin (250, title.getWidth()),
                                                       title.getHeight()).reduced (1, 3);
        previousButton.setBounds (transport.removeFromLeft (36).reduced (1, 3));
        playButton.setBounds (transport.removeFromLeft (42).reduced (1));
        stopButton.setBounds (transport.removeFromLeft (36).reduced (1, 3));
        timeLabel.setBounds (transport.reduced (3, 1));
        barsLabel.setVisible (false);
        clearBounds (barsLabel);
        savedLabel.setVisible (false);
    }
    else if (! showMenus)
    {
        brandLabel.setBounds (title.removeFromLeft (96).reduced (8, 5));
        projectLabel.setVisible (true);
        moreButton.setVisible (true);
        settingsButton.setVisible (true);
        themeButton.setVisible (true);
        auto titleRight = title.removeFromRight (phone ? 132 : 136);
        moreButton.setBounds (titleRight.removeFromRight (44).reduced (3, 4));
        settingsButton.setBounds (titleRight.removeFromRight (44).reduced (3, 4));
        themeButton.setBounds (titleRight.removeFromRight (44).reduced (3, 4));
        projectLabel.setBounds (title.reduced (3, 3));
        savedLabel.setVisible (false);
    }
    else
    {
        moreButton.setVisible (false);
        projectLabel.setVisible (true);
        auto titleLeft = title.removeFromLeft (juce::jmin (430, getWidth() / 3));
        brandLabel.setBounds (titleLeft.removeFromLeft (110).reduced (9, 5));
        const auto menuWidth = juce::jmax (42, titleLeft.getWidth() / 5);
        fileMenu.setBounds (titleLeft.removeFromLeft (menuWidth).reduced (1, 6));
        editMenu.setBounds (titleLeft.removeFromLeft (menuWidth).reduced (1, 6));
        trackMenu.setBounds (titleLeft.removeFromLeft (menuWidth).reduced (1, 6));
        clipMenu.setBounds (titleLeft.removeFromLeft (menuWidth).reduced (1, 6));
        viewMenu.setBounds (titleLeft.reduced (1, 6));
        auto titleRight = title.removeFromRight (showLanguages ? 132 : 68);
        settingsButton.setVisible (true);
        themeButton.setVisible (true);
        settingsButton.setBounds (titleRight.removeFromRight (34).reduced (2, 5));
        themeButton.setBounds (titleRight.removeFromRight (34).reduced (2, 5));
        chineseButton.setVisible (showLanguages);
        englishButton.setVisible (showLanguages);
        if (showLanguages)
        {
            englishButton.setBounds (titleRight.removeFromRight (32).reduced (0, 8));
            chineseButton.setBounds (titleRight.removeFromRight (32).reduced (0, 8));
        }
        auto projectArea = title.reduced (4, 4);
        projectLabel.setBounds (projectArea.withTrimmedRight (68));
        savedLabel.setVisible (true);
        savedLabel.setBounds (projectArea.removeFromRight (64));
    }

    if (! showMenus)
    {
        for (auto* menu : { &fileMenu, &editMenu, &trackMenu, &clipMenu, &viewMenu })
            menu->setBounds ({ });
        chineseButton.setVisible (false);
        englishButton.setVisible (false);
    }

    const auto useTwoRows = phonePortrait || (compact && getWidth() < 860);
    if (phoneLandscape)
    {
        juce::ignoreUnused (toolbar);
        for (size_t index = 2; index < toolButtons.size(); ++index)
            clearBounds (*toolButtons[index]);
        clearBounds (tempoButton);
        clearBounds (speedButton);
        clearBounds (pitchLockButton);
        clearBounds (recordButton);
        clearBounds (loopButton);
        clearBounds (exportButton);
    }
    else if (useTwoRows)
    {
        auto transportRow = toolbar.removeFromTop (48).reduced (4, 2);
        auto transport = transportRow.removeFromLeft (phonePortrait ? 184 : 210);
        previousButton.setBounds (transport.removeFromLeft (44).reduced (2));
        playButton.setBounds (transport.removeFromLeft (48).reduced (1));
        stopButton.setBounds (transport.removeFromLeft (44).reduced (2));
        loopButton.setBounds (transport.removeFromLeft (44).reduced (2));
        recordButton.setBounds ({ });
        auto timeBox = transportRow.reduced (2, 3);
        timeLabel.setBounds (timeBox.removeFromTop (23));
        barsLabel.setBounds (timeBox);

        auto tools = toolbar.reduced (4, 2);
        for (size_t i = 0; i < toolButtons.size(); ++i)
        {
            if (i == 7)
            {
                toolButtons[i]->setBounds ({ });
                continue;
            }
            toolButtons[i]->setBounds (tools.removeFromLeft (juce::jmin (44, tools.getWidth() / 7))
                                           .reduced (2));
        }
        tempoButton.setBounds ({ });
        speedButton.setBounds ({ });
        pitchLockButton.setBounds ({ });
        exportButton.setBounds ({ });
    }
    else
    {
        for (auto& button : toolButtons)
            button->setVisible (true);
        auto toolLeft = toolbar.removeFromLeft (juce::jmin (300, getWidth() / 3));
        for (auto& button : toolButtons)
            button->setBounds (toolLeft.removeFromLeft (32).reduced (1, 9));
        auto toolRight = toolbar.removeFromRight (juce::jmin (300, getWidth() / 3));
        exportButton.setBounds (toolRight.removeFromRight (34).reduced (2, 9));
        pitchLockButton.setBounds (toolRight.removeFromRight (88).reduced (2, 10));
        speedButton.setBounds (toolRight.removeFromRight (55).reduced (2, 10));
        tempoButton.setBounds (toolRight.removeFromRight (66).reduced (2, 10));
        auto transport = toolbar.withSizeKeepingCentre (246, toolbar.getHeight());
        previousButton.setBounds (transport.removeFromLeft (32).reduced (1, 8));
        playButton.setBounds (transport.removeFromLeft (38).reduced (1, 6));
        stopButton.setBounds (transport.removeFromLeft (32).reduced (1, 8));
        recordButton.setBounds (transport.removeFromLeft (32).reduced (1, 8));
        loopButton.setBounds (transport.removeFromLeft (32).reduced (1, 8));
        auto timeBox = transport.reduced (2, 6);
        timeLabel.setBounds (timeBox.removeFromTop (22));
        barsLabel.setBounds (timeBox);
    }

    if (showMenus)
    {
        for (auto* menu : { &fileMenu, &editMenu, &trackMenu, &clipMenu, &viewMenu })
            menu->setVisible (true);
    }
    else
    {
        for (auto* menu : { &fileMenu, &editMenu, &trackMenu, &clipMenu, &viewMenu })
            menu->setVisible (false);
    }
    if (phoneLandscape)
    {
        for (size_t i = 0; i < toolButtons.size(); ++i)
            toolButtons[i]->setVisible (i < 2);
        previousButton.setVisible (true);
        playButton.setVisible (true);
        stopButton.setVisible (true);
        loopButton.setVisible (false);
        timeLabel.setVisible (true);
        barsLabel.setVisible (false);
        recordButton.setVisible (false);
        tempoButton.setVisible (false);
        speedButton.setVisible (false);
        pitchLockButton.setVisible (false);
        exportButton.setVisible (false);
    }
    else if (useTwoRows)
    {
        for (auto& button : toolButtons)
            button->setVisible (true);
        toolButtons[7]->setVisible (false);
        recordButton.setVisible (false);
        tempoButton.setVisible (false);
        speedButton.setVisible (false);
        pitchLockButton.setVisible (false);
        exportButton.setVisible (false);
        previousButton.setVisible (true);
        playButton.setVisible (true);
        stopButton.setVisible (true);
        loopButton.setVisible (true);
        timeLabel.setVisible (true);
        barsLabel.setVisible (true);
    }
    else
    {
        recordButton.setVisible (true);
        tempoButton.setVisible (true);
        speedButton.setVisible (true);
        pitchLockButton.setVisible (true);
        exportButton.setVisible (true);
        previousButton.setVisible (true);
        playButton.setVisible (true);
        stopButton.setVisible (true);
        loopButton.setVisible (true);
        timeLabel.setVisible (true);
        barsLabel.setVisible (true);
    }

    if (phonePortrait)
    {
        auto presets = workspaceBar.reduced (5, 5);
        const auto presetWidth = presets.getWidth() / 3;
        for (auto& button : workspaceButtons)
            button.setVisible (true);
        workspaceButtons[0].setBounds (presets.removeFromLeft (presetWidth).reduced (1));
        workspaceButtons[1].setBounds (presets.removeFromLeft (presetWidth).reduced (1));
        workspaceButtons[2].setBounds (presets.reduced (1));
        for (auto* button : { &browserToggle, &mixerToggle, &inspectorToggle, &layoutButton })
        {
            button->setVisible (false);
            clearBounds (*button);
        }
    }
    else if (phoneLandscape)
    {
        for (auto& button : workspaceButtons)
        {
            button.setVisible (false);
            clearBounds (button);
        }
        for (auto* button : { &browserToggle, &mixerToggle, &inspectorToggle, &layoutButton })
        {
            button->setVisible (false);
            clearBounds (*button);
        }
    }
    else
    {
        auto workspaceLeft = workspaceBar.removeFromLeft (90);
        browserToggle.setVisible (true);
        mixerToggle.setVisible (true);
        browserToggle.setBounds (workspaceLeft.removeFromLeft (34).reduced (2, compact ? 3 : 5));
        mixerToggle.setBounds (workspaceLeft.removeFromLeft (34).reduced (2, compact ? 3 : 5));
        auto workspaceRight = workspaceBar.removeFromRight (compact ? 70 : 100);
        layoutButton.setVisible (true);
        layoutButton.setBounds (workspaceRight.removeFromRight (compact ? 34 : 46)
                                    .reduced (compact ? 2 : 6, compact ? 3 : 5));
        inspectorToggle.setVisible (true);
        inspectorToggle.setBounds (workspaceRight.removeFromRight (34).reduced (2, compact ? 3 : 5));
        auto presets = workspaceBar.withSizeKeepingCentre (compact ? 300 : 246,
                                                            workspaceBar.getHeight()).reduced (2, compact ? 3 : 5);
        const auto presetWidth = presets.getWidth() / 3;
        workspaceButtons[0].setBounds (presets.removeFromLeft (presetWidth).reduced (1));
        workspaceButtons[1].setBounds (presets.removeFromLeft (presetWidth).reduced (1));
        workspaceButtons[2].setBounds (presets.reduced (1));
    }

    statusLeft.setVisible (! phone);
    statusRight.setVisible (! phoneLandscape);
    if (phoneLandscape)
    {
        statusLeft.setBounds ({ });
        statusRight.setBounds ({ });
    }
    else if (phone)
    {
        statusRight.setBounds (status.reduced (8, 1));
        statusLeft.setBounds ({ });
    }
    else
    {
        statusLeft.setBounds (status.removeFromLeft (getWidth() * 2 / 3).reduced (9, 0));
        statusRight.setBounds (status.reduced (9, 0));
    }

    if (phone)
    {
        mixer.setVisible (mobilePanel == "mixer");
        browser.setVisible (mobilePanel == "browser");
        inspector.setVisible (mobilePanel == "inspector");
        if (phoneLandscape)
        {
            auto workspaceArea = area;
            if (state.workspace == Workspace::spatial)
            {
                const auto spatialWidth = juce::jlimit (
                    230, 340, juce::roundToInt (workspaceArea.getWidth() * 0.38f));
                auto spatialArea = workspaceArea.removeFromLeft (
                    juce::jmin (spatialWidth, workspaceArea.getWidth()));
                workspaceArea.removeFromLeft (1);
                spatialCanvas.setVisible (true);
                spatialCanvas.setBounds (spatialArea);
            }
            else
            {
                spatialCanvas.setVisible (false);
                spatialCanvas.setBounds ({ });
            }
            timeline.setBounds (workspaceArea);

            auto drawer = area.reduced (6, 4);
            const auto drawerRatio = mobilePanel == "mixer" ? 0.78f : 0.48f;
            const auto drawerWidth = juce::jmin (
                drawer.getWidth(),
                juce::jlimit (280, mobilePanel == "mixer" ? 700 : 440,
                              juce::roundToInt (area.getWidth() * drawerRatio)));
            drawer = drawer.removeFromRight (drawerWidth);
            if (mobilePanel == "mixer")
                mixer.setBounds (drawer);
            else if (mobilePanel == "browser")
                browser.setBounds (drawer);
            else if (mobilePanel == "inspector")
                inspector.setBounds (drawer);
        }
        else
        {
            // Portrait remains a bottom-sheet fallback for rotation transitions.
            auto workspaceArea = area;
            if (state.workspace == Workspace::spatial)
            {
                auto spatialArea = workspaceArea.removeFromTop (
                    juce::jmin (230, juce::jmax (170, workspaceArea.getHeight() / 2)));
                spatialCanvas.setVisible (true);
                spatialCanvas.setBounds (spatialArea);
            }
            else
            {
                spatialCanvas.setVisible (false);
                spatialCanvas.setBounds ({ });
            }
            timeline.setBounds (workspaceArea);
            auto sheet = workspaceArea.reduced (6, 6);
            if (mobilePanel == "mixer")
                mixer.setBounds (sheet.withHeight (juce::jmin (360, sheet.getHeight()))
                                      .withY (sheet.getBottom() - juce::jmin (360, sheet.getHeight())));
            else if (mobilePanel == "browser")
                browser.setBounds (sheet.withHeight (juce::jmin (460, sheet.getHeight()))
                                    .withY (sheet.getBottom() - juce::jmin (460, sheet.getHeight())));
            else if (mobilePanel == "inspector")
                inspector.setBounds (sheet.withHeight (juce::jmin (520, sheet.getHeight()))
                                      .withY (sheet.getBottom() - juce::jmin (520, sheet.getHeight())));
        }
        if (mobilePanel == "mixer") mixer.toFront (false);
        if (mobilePanel == "browser") browser.toFront (false);
        if (mobilePanel == "inspector") inspector.toFront (false);
    }
    else
    {
        auto mixerHeight = state.workspace == Workspace::mix
                               ? juce::jmax (compact ? 190 : 260, state.layout.mixerHeight)
                               : state.layout.mixerHeight;
        mixerHeight = juce::jmin (mixerHeight, juce::jmax (0, area.getHeight() / 2));
        auto mixerArea = state.layout.mixerVisible ? area.removeFromBottom (mixerHeight)
                                                     : juce::Rectangle<int>();
        mixer.setVisible (state.layout.mixerVisible);
        mixer.setBounds (mixerArea);

        const auto availableWidth = area.getWidth();
        const auto browserWidth = state.layout.browserVisible
                                      ? juce::jmin (state.layout.browserWidth,
                                                    compact ? juce::jmax (140, availableWidth / 3)
                                                            : state.layout.browserWidth)
                                      : 0;
        const auto inspectorWidth = state.layout.inspectorVisible
                                         ? juce::jmin (state.layout.inspectorWidth,
                                                       compact ? juce::jmax (190, availableWidth / 3)
                                                               : state.layout.inspectorWidth)
                                         : 0;
        browser.setVisible (state.layout.browserVisible);
        inspector.setVisible (state.layout.inspectorVisible);
        browser.setBounds (area.removeFromLeft (juce::jmin (browserWidth, area.getWidth())));
        inspector.setBounds (area.removeFromRight (juce::jmin (inspectorWidth, area.getWidth())));

        if (state.workspace == Workspace::spatial)
        {
            auto spatialArea = area.removeFromTop (juce::jmin (250, juce::jmax (190, area.getHeight() / 2)));
            spatialCanvas.setVisible (true);
            spatialCanvas.setBounds (spatialArea);
        }
        else
        {
            spatialCanvas.setVisible (false);
            spatialCanvas.setBounds ({ });
        }
        timeline.setBounds (area);
    }

    if (state.workspace == Workspace::spatial)
        spatialCanvas.repaint();

    const auto showClipContext = phoneLandscape && mobilePanel.isEmpty()
                              && state.selectedClipId != 0
                              && ! layoutPanel.isVisible() && ! exportPanel.isVisible();
    clipContextToolbar.setVisible (showClipContext);
    if (showClipContext)
    {
        const auto availableWidth = juce::jmax (0, timeline.getWidth() - 116);
        const auto contextWidth = juce::jmin (190, availableWidth);
        clipContextToolbar.setBounds (timeline.getX() + 108,
                                      timeline.getY() + 32,
                                      contextWidth, 38);
        clipContextToolbar.toFront (false);
    }
    else
    {
        clipContextToolbar.setBounds ({ });
    }

    if (layoutPanel.isVisible())
    {
        const auto preferredWidth = phoneLandscape ? 420 : phone ? getWidth() - 16 : 360;
        const auto panelWidth = juce::jmin (preferredWidth, getWidth() - 16);
        const auto panelHeight = juce::jmin (phoneLandscape ? 320 : phone ? 520 : 390,
                                             juce::jmax (120, getHeight() - titleBarHeight()
                                                              - toolbarHeight() - 12));
        layoutPanel.setBounds (getWidth() - panelWidth - 8,
                               titleBarHeight() + toolbarHeight() + 6,
                               panelWidth, panelHeight);
        layoutPanel.toFront (false);
    }
    if (exportPanel.isVisible())
    {
        const auto preferredWidth = phoneLandscape ? 420 : phone ? getWidth() - 16 : 390;
        const auto panelWidth = juce::jmin (preferredWidth, getWidth() - 24);
        const auto panelHeight = juce::jmin (phoneLandscape ? 300 : phone ? 420 : 320,
                                             juce::jmax (120, getHeight() - titleBarHeight()
                                                              - toolbarHeight() - 16));
        exportPanel.setBounds (getWidth() - panelWidth - 8,
                               titleBarHeight() + toolbarHeight() + 8,
                               panelWidth, panelHeight);
        exportPanel.toFront (false);
    }
}

void MainComponent::timerCallback()
{
    updateTransport();
    mixer.repaint();
    if (state.workspace == Workspace::spatial && audioEngine.isPlaying())
        spatialCanvas.repaint();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateTransport();
    mixer.syncFromEngine();
    browser.repaint();
    timeline.repaint();
}

void MainComponent::refreshText()
{
    fileMenu.setButtonText (localizer.text (TextId::file));
    editMenu.setButtonText (localizer.text (TextId::edit));
    trackMenu.setButtonText (localizer.text (TextId::track));
    clipMenu.setButtonText (localizer.text (TextId::clip));
    viewMenu.setButtonText (localizer.text (TextId::view));
    savedLabel.setText (localizer.text (TextId::saved), juce::dontSendNotification);
    chineseButton.setButtonText (juce::String::fromUTF8 ("中"));
    englishButton.setButtonText ("EN");
    tempoButton.setButtonText ("96 BPM");
    speedButton.setButtonText (juce::String (state.playbackSpeed, 2) + juce::String::fromUTF8 ("×"));
    pitchLockButton.setButtonText (localizer.text (TextId::pitchLock));
    workspaceButtons[0].setButtonText (localizer.text (TextId::workspaceEdit));
    workspaceButtons[1].setButtonText (localizer.text (TextId::workspaceSpatial));
    workspaceButtons[2].setButtonText (localizer.text (TextId::workspaceMix));
    const auto outputChannels = audioEngine.getOutputChannelCount();
    const auto renderLayout = outputChannels >= 12 ? "7.1.4" : outputChannels >= 10 ? "5.1.4"
                              : outputChannels >= 8 ? "7.1" : outputChannels == 7 ? "6.1"
                              : outputChannels >= 6 ? "5.1"
                              : outputChannels >= 4 ? "Quad" : "Stereo";
    statusLeft.setText ("48 kHz | 24 bit    Buffer 256 | 5.3 ms    " + localizer.text (TextId::realtimeRender), juce::dontSendNotification);
    statusRight.setText (juce::String (renderLayout) + "    Peak -0.8 dBFS",
                         juce::dontSendNotification);

    browser.refreshText();
    timeline.refreshText();
    inspector.refreshText();
    mixer.refreshText();
    layoutPanel.refreshText();
    exportPanel.refreshText();
    const auto chinese = localizer.getLanguage() == Language::chinese;
    struct ToolHelp { const char* chineseText; const char* englishText; };
    static constexpr std::array<ToolHelp, 8> toolHelp {{
        { "撤销上一步操作", "Undo the last edit" },
        { "重做上一步操作", "Redo the last edit" },
        { "选择工具：选择并移动音频剪辑", "Select tool: select and move audio clips" },
        { "范围工具：拖拽创建空间与音量自动化区间，并以平滑过渡衔接整轨 3D", "Range tool: drag to create spatial and volume automation with smooth transitions over the track's 3D bed" },
        { "分割工具：点击时间位置分割音频剪辑", "Split tool: click a time position to split an audio clip" },
        { "淡入淡出工具", "Fade tool" },
        { "时间线吸附工具", "Timeline snap tool" },
        { "时间线缩放工具", "Timeline zoom tool" }
    }};
    for (size_t i = 0; i < toolButtons.size(); ++i)
    {
        const auto help = juce::String::fromUTF8 (
            chinese ? toolHelp[i].chineseText : toolHelp[i].englishText);
        toolButtons[i]->setTitle (help);
        toolButtons[i]->setTooltip (help);
    }

    const auto setIconHelp = [chinese] (IconButton& button, const char* chineseText,
                                        const char* englishText)
    {
        const auto help = juce::String::fromUTF8 (chinese ? chineseText : englishText);
        button.setTitle (help);
        button.setTooltip (help);
    };
    setIconHelp (themeButton, "切换明暗主题", "Toggle light and dark theme");
    setIconHelp (settingsButton, "音频设备设置", "Audio device settings");
    setIconHelp (moreButton, "打开移动端工具和面板", "Open mobile tools and panels");
    setIconHelp (previousButton, "返回时间线开头", "Return to the start of the timeline");
    setIconHelp (playButton, "播放或暂停", "Play or pause");
    setIconHelp (stopButton, "停止并返回开头", "Stop and return to the start");
    setIconHelp (recordButton, "录音", "Record");
    setIconHelp (loopButton, "循环播放", "Loop playback");
    setIconHelp (exportButton, "导出音频", "Export audio");
    setIconHelp (browserToggle, "显示或隐藏素材库", "Show or hide the media browser");
    setIconHelp (mixerToggle, "显示或隐藏混音器", "Show or hide the mixer");
    setIconHelp (inspectorToggle, "显示或隐藏检查器", "Show or hide the inspector");
    setIconHelp (layoutButton, "工作区布局设置", "Workspace layout settings");
    setIconHelp (clipContextToolbar.moveButton, "移动选中片段", "Move the selected clip");
    setIconHelp (clipContextToolbar.splitButton, "分割工具：点击片段内的时间位置", "Split tool: tap a time inside the clip");
    setIconHelp (clipContextToolbar.duplicateButton, "复制选中片段", "Duplicate the selected clip");
    setIconHelp (clipContextToolbar.spatialButton, "打开选中片段的 3D 空间参数", "Open spatial settings for the selected clip");
    setIconHelp (clipContextToolbar.deleteButton, "删除选中片段", "Delete the selected clip");
    tempoButton.setTooltip (chinese ? juce::String::fromUTF8 ("工程速度") : juce::String ("Project tempo"));
    speedButton.setTooltip (chinese ? juce::String::fromUTF8 ("切换播放倍速") : juce::String ("Cycle playback speed"));
    pitchLockButton.setTooltip (chinese ? juce::String::fromUTF8 ("变速时保持音高") : juce::String ("Preserve pitch while changing speed"));
    updateToggleStates();
    resized();
    repaint();
}

void MainComponent::applyTheme()
{
    studioLook.setTheme (state.theme);
    themeButton.setIcon (state.theme == ThemeMode::dark ? Icon::moon : Icon::sun);
    sendLookAndFeelChange();
    repaint();
}

void MainComponent::applyWorkspace (Workspace workspace)
{
    state.workspace = workspace;
    for (size_t i = 0; i < workspaceButtons.size(); ++i)
        workspaceButtons[i].setToggleState (static_cast<int> (workspace) == static_cast<int> (i), juce::dontSendNotification);
    if (workspace == Workspace::spatial)
        inspector.setTab (InspectorTab::spatial);
    else if (workspace == Workspace::mix)
        inspector.setTab (InspectorTab::track);

    if (getWidth() > 0 && isPhoneLayout())
    {
        if (workspace == Workspace::mix)
            mobilePanel = "mixer";
        else if (mobilePanel == "mixer")
            mobilePanel.clear();

        state.layout.browserVisible = mobilePanel == "browser";
        state.layout.inspectorVisible = mobilePanel == "inspector";
        state.layout.mixerVisible = mobilePanel == "mixer";
    }
    updateToggleStates();
    resized();
    repaint();
}

void MainComponent::updateToggleStates()
{
    chineseButton.setToggleState (state.language == Language::chinese, juce::dontSendNotification);
    englishButton.setToggleState (state.language == Language::english, juce::dontSendNotification);
    const auto phone = getWidth() > 0 && isPhoneLayout();
    browserToggle.setToggleState (phone ? mobilePanel == "browser" : state.layout.browserVisible,
                                  juce::dontSendNotification);
    inspectorToggle.setToggleState (phone ? mobilePanel == "inspector" : state.layout.inspectorVisible,
                                    juce::dontSendNotification);
    mixerToggle.setToggleState (phone ? mobilePanel == "mixer" : state.layout.mixerVisible,
                                juce::dontSendNotification);
    layoutPanel.syncFromState();
}

void MainComponent::showMobilePanel (const juce::String& panel)
{
    if (! isPhoneLayout())
    {
        if (panel == "browser") setPanelVisible (panel, ! state.layout.browserVisible);
        else if (panel == "inspector") setPanelVisible (panel, ! state.layout.inspectorVisible);
        else if (panel == "mixer") setPanelVisible (panel, ! state.layout.mixerVisible);
        return;
    }

    if (mobilePanel == panel)
    {
        mobilePanel.clear();
        if (panel == "browser") state.layout.browserVisible = false;
        else if (panel == "inspector") state.layout.inspectorVisible = false;
        else if (panel == "mixer") state.layout.mixerVisible = false;
    }
    else
    {
        mobilePanel = panel;
        state.layout.browserVisible = panel == "browser";
        state.layout.inspectorVisible = panel == "inspector";
        state.layout.mixerVisible = panel == "mixer";
    }
    updateToggleStates();
    resized();
    repaint();
}

void MainComponent::showMobileMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto label = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (label ("快速操作", "Quick actions"));
    menu.addItem (1, label ("导入音频", "Import audio..."));
    menu.addItem (2, label ("导出工程", "Export project..."), audioEngine.hasFile() && ! exportInProgress);
    menu.addItem (3, label ("撤销", "Undo"), audioEngine.canUndo());
    menu.addItem (4, label ("重做", "Redo"), audioEngine.canRedo());
    menu.addItem (5, state.selectedSpatialRegionId != 0
                         ? label ("删除空间区间", "Delete spatial region")
                         : label ("删除选中片段", "Delete selected clip"),
                  state.selectedClipId != 0);
    menu.addSeparator();
    menu.addSectionHeader (label ("编辑工具", "Edit tools"));
    menu.addItem (6, label ("移动与选择", "Move and select"), true,
                  state.activeTool == Tool::select);
    menu.addItem (7, label ("空间与音量范围", "Spatial and volume range"),
                  state.selectedClipId != 0, state.activeTool == Tool::range);
    menu.addItem (8, label ("分割", "Split"), state.selectedClipId != 0,
                  state.activeTool == Tool::split);
    menu.addItem (9, label ("自动吸附", "Snapping"), true,
                  timeline.isSnappingEnabled());
    menu.addSeparator();
    menu.addSectionHeader (label ("工作区", "Workspace"));
    menu.addItem (10, label ("剪辑", "Edit"), true, state.workspace == Workspace::edit);
    menu.addItem (11, label ("空间", "Spatial"), true, state.workspace == Workspace::spatial);
    menu.addItem (12, label ("混音", "Mix"), true, state.workspace == Workspace::mix);
    menu.addSeparator();
    menu.addSectionHeader (label ("面板", "Panels"));
    menu.addItem (20, label ("素材库", "Media"), true, mobilePanel == "browser");
    menu.addItem (21, label ("检查器", "Inspector"), true, mobilePanel == "inspector");
    menu.addItem (22, label ("混音器", "Mixer"), true, mobilePanel == "mixer");
    menu.addItem (23, label ("布局设置", "Layout settings"));
    menu.addSeparator();
    menu.addItem (30, label ("切换明暗主题", "Toggle light/dark theme"));
    menu.addItem (31, label ("中文", "Chinese"), true, state.language == Language::chinese);
    menu.addItem (32, label ("English", "English"), true, state.language == Language::english);
    menu.addItem (33, label ("音频设备", "Audio device settings"));
    menu.addItem (34, label ("检查更新...", "Check for updates..."), ! updateCheckInProgress);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                        [safeThis] (int result)
    {
        if (auto* component = safeThis.getComponent())
        {
            if (result == 1) component->openAudioFile();
            else if (result == 2) component->showExportPanel();
            else if (result == 3 && component->audioEngine.undo()) component->resetSelectionAfterHistoryChange();
            else if (result == 4 && component->audioEngine.redo()) component->resetSelectionAfterHistoryChange();
            else if (result == 5) component->deleteSelectedClip();
            else if (result >= 6 && result <= 8)
            {
                const auto tool = static_cast<Tool> (result - 6);
                component->state.activeTool = tool;
                for (size_t index = 2; index <= 5; ++index)
                    component->toolButtons[index]->setToggleState (
                        index == static_cast<size_t> (result - 4),
                        juce::dontSendNotification);
                if (tool != Tool::range)
                {
                    component->state.selectedSpatialRegionId = 0;
                    component->state.selectedRangeStart = 0.0;
                    component->state.selectedRangeEnd = 0.0;
                    component->syncSpatialStateFromSelection();
                    component->inspector.syncFromState();
                }
                component->timeline.repaint();
            }
            else if (result == 9)
            {
                component->timeline.setSnappingEnabled (
                    ! component->timeline.isSnappingEnabled());
                component->toolButtons[6]->setToggleState (
                    component->timeline.isSnappingEnabled(),
                    juce::dontSendNotification);
            }
            else if (result >= 10 && result <= 12) component->applyWorkspace (static_cast<Workspace> (result - 10));
            else if (result == 20) component->showMobilePanel ("browser");
            else if (result == 21) component->showMobilePanel ("inspector");
            else if (result == 22) component->showMobilePanel ("mixer");
            else if (result == 23)
            {
                component->exportPanel.setVisible (false);
                component->layoutPanel.setVisible (true);
                component->resized();
                component->layoutPanel.toFront (true);
            }
            else if (result == 30)
            {
                component->state.theme = component->state.theme == ThemeMode::dark
                                            ? ThemeMode::light : ThemeMode::dark;
                component->applyTheme();
            }
            else if (result == 31 || result == 32)
            {
                component->state.language = result == 31 ? Language::chinese : Language::english;
                component->localizer.setLanguage (component->state.language);
                component->refreshText();
            }
            else if (result == 33) component->showAudioDeviceSettings();
            else if (result == 34) component->checkForUpdates (true);
        }
    });
}

void MainComponent::showAudioDeviceSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (audioEngine.getDeviceManager(),
                                                              0, 2, 0, AudioEngine::maximumOutputChannels,
                                                              false, false, false, false);
    selector->setSize (520, 420);
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (selector),
                                            settingsButton.getScreenBounds(), nullptr);
}

void MainComponent::checkForUpdates (bool showFeedback)
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    if (updateCheckInProgress)
    {
        if (showFeedback)
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                chinese ? juce::String::fromUTF8 ("检查更新") : juce::String ("Check for updates"),
                chinese ? juce::String::fromUTF8 ("正在检查，请稍候。")
                        : juce::String ("An update check is already in progress."),
                {}, this);
        return;
    }

    updateCheckInProgress = true;
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    const juce::String currentVersion (JUCE_APPLICATION_VERSION_STRING);
    updateThread.addJob ([safeThis, currentVersion, showFeedback]
    {
        auto checkResult = UpdateService::checkLatest (currentVersion);
        juce::MessageManager::callAsync (
            [safeThis, currentVersion, showFeedback, result = std::move (checkResult)]
        {
            auto* component = safeThis.getComponent();
            if (component == nullptr)
                return;

            component->updateCheckInProgress = false;
            const auto isChinese = component->localizer.getLanguage() == Language::chinese;
            const auto title = isChinese ? juce::String::fromUTF8 ("检查更新")
                                         : juce::String ("Check for updates");

            if (result.state == UpdateCheckResult::State::failed)
            {
                if (showFeedback)
                {
                    const auto message = isChinese
                        ? juce::String::fromUTF8 ("暂时无法连接 GitHub 获取版本信息。\n\n") + result.error
                        : juce::String ("Could not retrieve release information from GitHub.\n\n") + result.error;
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon, title, message, {}, component);
                }
                return;
            }

            if (result.state == UpdateCheckResult::State::upToDate)
            {
                if (showFeedback)
                {
                    const auto message = isChinese
                        ? juce::String::fromUTF8 ("当前已是最新版本：") + currentVersion
                        : juce::String ("You are using the latest version: ") + currentVersion;
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon, title, message, {}, component);
                }
                return;
            }

            auto message = isChinese
                ? juce::String::fromUTF8 ("发现新版本 ") + result.latestVersion
                    + juce::String::fromUTF8 ("（当前版本 ") + currentVersion
                    + juce::String::fromUTF8 ("）。\n\n下载完成后，请确认安装或运行新版客户端。")
                : juce::String ("Version ") + result.latestVersion
                    + " is available (current version " + currentVersion
                    + ").\n\nAfter downloading, confirm installation or run the new client.";
            const auto downloadUrl = result.downloadUrl.isNotEmpty()
                                           ? result.downloadUrl : result.releaseUrl;

            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                isChinese ? juce::String::fromUTF8 ("发现新版本")
                          : juce::String ("Update available"),
                message,
                isChinese ? juce::String::fromUTF8 ("下载更新") : juce::String ("Download"),
                isChinese ? juce::String::fromUTF8 ("稍后") : juce::String ("Later"),
                component,
                juce::ModalCallbackFunction::create (
                    [safeThis, downloadUrl] (int selected)
                {
                    if (selected == 1 && ! juce::URL (downloadUrl).launchInDefaultBrowser())
                    {
                        if (auto* owner = safeThis.getComponent())
                        {
                            const auto ownerChinese = owner->localizer.getLanguage() == Language::chinese;
                            juce::AlertWindow::showMessageBoxAsync (
                                juce::MessageBoxIconType::WarningIcon,
                                ownerChinese ? juce::String::fromUTF8 ("无法打开下载页面")
                                             : juce::String ("Could not open download"),
                                downloadUrl, {}, owner);
                        }
                    }
                }));
        });
    });
}

void MainComponent::showFileMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto text = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };
    juce::PopupMenu menu;
    menu.addItem (1, text ("导入音频...", "Import audio..."));
    menu.addItem (2, text ("选择素材目录...", "Choose media folder..."));
    menu.addSeparator();
    menu.addItem (3, text ("导出工程音频...", "Export project audio..."),
                  audioEngine.hasFile() && ! exportInProgress);
    menu.addSeparator();
    menu.addItem (4, text ("音频设备设置...", "Audio device settings..."));
    menu.addItem (5, text ("检查更新...", "Check for updates..."), ! updateCheckInProgress);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileMenu),
                        [safeThis] (int result)
    {
        if (auto* component = safeThis.getComponent())
        {
            if (result == 1) component->openAudioFile();
            else if (result == 2) component->chooseMediaDirectory();
            else if (result == 3) component->showExportPanel();
            else if (result == 4) component->showAudioDeviceSettings();
            else if (result == 5) component->checkForUpdates (true);
        }
    });
}

void MainComponent::showEditMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto text = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };
    const auto clip = selectedClip();
    const auto canSplit = clip.has_value()
                       && audioEngine.getPosition() > clip->timelineStart + 0.001
                       && audioEngine.getPosition() < clip->timelineStart + clip->duration - 0.001;
    juce::PopupMenu menu;
    menu.addItem (1, text ("撤销", "Undo"), audioEngine.canUndo());
    menu.addItem (2, text ("重做", "Redo"), audioEngine.canRedo());
    menu.addSeparator();
    menu.addItem (3, text ("复制所选剪辑", "Duplicate selected clip"), clip.has_value());
    menu.addItem (4, state.selectedSpatialRegionId != 0
                         ? text ("删除所选空间区间", "Delete selected spatial region")
                         : text ("删除所选剪辑", "Delete selected clip"),
                  clip.has_value());
    menu.addItem (5, text ("在播放头处分割", "Split at playhead"), canSplit);
    menu.addSeparator();
    menu.addItem (10, text ("选择工具", "Select tool"), true, state.activeTool == Tool::select);
    menu.addItem (11, text ("范围自动化工具", "Range automation tool"), true,
                  state.activeTool == Tool::range);
    menu.addItem (12, text ("分割工具", "Split tool"), true, state.activeTool == Tool::split);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editMenu),
                        [safeThis] (int result)
    {
        auto* component = safeThis.getComponent();
        if (component == nullptr) return;
        if (result == 1 && component->audioEngine.undo()) component->resetSelectionAfterHistoryChange();
        else if (result == 2 && component->audioEngine.redo()) component->resetSelectionAfterHistoryChange();
        else if (result == 3) component->duplicateSelectedClip();
        else if (result == 4) component->deleteSelectedClip();
        else if (result == 5) component->splitSelectedClipAtPlayhead();
        else if (result >= 10 && result <= 12)
        {
            component->state.activeTool = static_cast<Tool> (result - 10);
            if (component->state.activeTool != Tool::range)
            {
                component->state.selectedSpatialRegionId = 0;
                component->state.selectedRangeStart = 0.0;
                component->state.selectedRangeEnd = 0.0;
                component->syncSpatialStateFromSelection();
                component->inspector.syncFromState();
            }
            for (size_t button = 2; button <= 5; ++button)
                component->toolButtons[button]->setToggleState (
                    button == static_cast<size_t> (result - 8), juce::dontSendNotification);
        }
    });
}

void MainComponent::showTrackMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto text = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };
    const auto trackCount = audioEngine.getTrackCount();
    const auto trackIndex = juce::jlimit (0, juce::jmax (0, trackCount - 1), state.selectedTrack);
    const auto project = audioEngine.getProjectSnapshot();
    const auto& track = project->tracks[static_cast<size_t> (trackIndex)];
    juce::PopupMenu menu;
    const auto addTrackLabel = chinese ? juce::String::fromUTF8 ("\u65b0\u5efa\u8f68\u9053")
                                       : juce::String ("Add new track");
    const auto renameTrackLabel = chinese ? juce::String::fromUTF8 ("\u91cd\u547d\u540d\u5f53\u524d\u8f68\u9053")
                                          : juce::String ("Rename selected track");
    menu.addItem (6, addTrackLabel, trackCount < AudioEngine::maximumTrackCount);
    menu.addItem (7, renameTrackLabel, trackCount > 0);
    menu.addSectionHeader ((chinese ? juce::String::fromUTF8 ("当前轨道 ") : juce::String ("Current track "))
                           + juce::String (trackIndex + 1));
    menu.addItem (1, text ("导入音频到当前轨道...", "Import audio to current track..."));
    menu.addSeparator();
    menu.addItem (2, text ("静音", "Mute"), true, track.muted);
    menu.addItem (3, text ("独奏", "Solo"), true, track.solo);
    menu.addItem (4, text ("启用 3D 空间渲染", "Enable 3D spatial render"), true,
                  track.spatial.enabled);
    menu.addItem (5, text ("重置轨道混音", "Reset track mix"));
    menu.addSeparator();
    juce::PopupMenu tracksMenu;
    const std::array<TextId, AudioEngine::trackCount> names {
        TextId::leadVocal, TextId::synth, TextId::drums, TextId::atmosphere, TextId::fxReturn
    };
    for (int index = 0; index < trackCount; ++index)
        tracksMenu.addItem (100 + index,
                            (chinese ? juce::String::fromUTF8 ("轨道 ") : juce::String ("Track "))
                                + juce::String (index + 1) + "  "
                                + (index < AudioEngine::trackCount
                                       ? localizer.text (names[static_cast<size_t> (index)])
                                       : project->tracks[static_cast<size_t> (index)].name),
                            true, index == trackIndex);
    menu.addSubMenu (text ("选择轨道", "Select track"), tracksMenu);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trackMenu),
                        [safeThis] (int result)
    {
        auto* component = safeThis.getComponent();
        if (component == nullptr) return;
        const auto selectedTrack = juce::jlimit (0, juce::jmax (0, component->audioEngine.getTrackCount() - 1),
                                                 component->state.selectedTrack);
        if (result == 1) component->openAudioFile();
        else if (result == 6) component->addTrack();
        else if (result == 7) component->renameSelectedTrack();
        else if (result == 2)
        {
            const auto snapshot = component->audioEngine.getProjectSnapshot();
            component->audioEngine.setTrackMuted (
                selectedTrack, ! snapshot->tracks[static_cast<size_t> (selectedTrack)].muted);
            component->mixer.syncFromEngine();
        }
        else if (result == 3)
        {
            const auto snapshot = component->audioEngine.getProjectSnapshot();
            component->audioEngine.setTrackSolo (
                selectedTrack, ! snapshot->tracks[static_cast<size_t> (selectedTrack)].solo);
            component->mixer.syncFromEngine();
        }
        else if (result == 4)
        {
            auto parameters = component->audioEngine.getTrackSpatialParameters (selectedTrack);
            parameters.enabled = ! parameters.enabled;
            component->audioEngine.setTrackSpatialParameters (selectedTrack, parameters);
            component->syncSpatialStateFromSelection();
            component->inspector.syncFromState();
            component->spatialCanvas.repaint();
        }
        else if (result == 5)
        {
            component->audioEngine.setTrackGainDb (selectedTrack, 0.0f);
            component->audioEngine.setTrackPan (selectedTrack, 0.0f);
            component->audioEngine.setTrackMuted (selectedTrack, false);
            component->audioEngine.setTrackSolo (selectedTrack, false);
            component->mixer.syncFromEngine();
            component->inspector.syncFromState();
        }
        else if (result >= 100 && result < 100 + component->audioEngine.getTrackCount())
            component->selectTrack (result - 100);
    });
}

#if 0
void MainComponent::addTrack()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    auto* dialog = new juce::AlertWindow (
        chinese ? juce::String::fromUTF8 ("鏂板缓杞ㄩ亾") : juce::String ("New track"),
        chinese ? juce::String::fromUTF8 ("璇疯緭鍏杞ㄩ亾鍚嶇О") : juce::String ("Enter a track name"),
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", chinese ? juce::String::fromUTF8 ("鏂板缓杞ㄩ亾") : "Track");
    dialog->addButton (chinese ? juce::String::fromUTF8 ("鍒涘缓") : "Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (chinese ? juce::String::fromUTF8 ("鍙栨秷") : "Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->centreAroundComponent (this, 360, 210);
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    dialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, dialog] (int result)
        {
            if (result == 1 && safeThis != nullptr)
            {
                if (safeThis->audioEngine.addTrack (dialog->getTextEditorContents ("name")))
                {
                    safeThis->state.selectedTrack = safeThis->audioEngine.getTrackCount() - 1;
                    safeThis->mixer.syncFromEngine();
                    safeThis->timeline.repaint();
                    safeThis->inspector.syncFromState();
                    safeThis->resized();
                }
            }
        }), true);
}

void MainComponent::renameSelectedTrack()
{
    const auto trackIndex = juce::jlimit (0, audioEngine.getTrackCount() - 1, state.selectedTrack);
    if (trackIndex < 0)
        return;

    const auto snapshot = audioEngine.getProjectSnapshot();
    const auto currentName = snapshot->tracks[static_cast<size_t> (trackIndex)].name;
    const auto chinese = localizer.getLanguage() == Language::chinese;
    auto* dialog = new juce::AlertWindow (
        chinese ? juce::String::fromUTF8 ("閲嶅懡鍚嶈建閬?) : juce::String ("Rename track"),
        chinese ? juce::String::fromUTF8 ("璇疯緭鍏ユ柊鐨勮建閬撳悕绉?") : juce::String ("Enter a new track name"),
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", currentName);
    dialog->addButton (chinese ? juce::String::fromUTF8 ("淇濆瓨") : "Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (chinese ? juce::String::fromUTF8 ("鍙栨秷") : "Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->centreAroundComponent (this, 360, 210);
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    dialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, dialog, trackIndex] (int result)
        {
            if (result == 1 && safeThis != nullptr
                && safeThis->audioEngine.renameTrack (trackIndex, dialog->getTextEditorContents ("name")))
            {
                safeThis->browser.refreshContent();
                safeThis->mixer.refreshText();
                safeThis->mixer.syncFromEngine();
                safeThis->timeline.repaint();
                safeThis->inspector.syncFromState();
            }
        }), true);
}

#endif

void MainComponent::addTrack()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto title = chinese ? juce::String::fromUTF8 ("\u65b0\u5efa\u8f68\u9053") : juce::String ("New track");
    const auto message = chinese ? juce::String::fromUTF8 ("\u8bf7\u8f93\u5165\u8f68\u9053\u540d\u79f0")
                                 : juce::String ("Enter a track name");
    const auto defaultName = chinese ? juce::String::fromUTF8 ("\u65b0\u5efa\u8f68\u9053") : juce::String ("Track");
    auto* dialog = new juce::AlertWindow (title, message,
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", defaultName);
    dialog->addButton (chinese ? juce::String::fromUTF8 ("\u521b\u5efa") : juce::String ("Create"),
                       1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (chinese ? juce::String::fromUTF8 ("\u53d6\u6d88") : juce::String ("Cancel"),
                       0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->centreAroundComponent (this, 360, 210);
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    dialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, dialog] (int result)
        {
            if (result == 1 && safeThis != nullptr
                && safeThis->audioEngine.addTrack (dialog->getTextEditorContents ("name")))
            {
                safeThis->state.selectedTrack = safeThis->audioEngine.getTrackCount() - 1;
                safeThis->mixer.refreshText();
                safeThis->mixer.syncFromEngine();
                safeThis->timeline.repaint();
                safeThis->inspector.syncFromState();
                safeThis->resized();
            }
        }), true);
}

void MainComponent::renameSelectedTrack()
{
    const auto trackCount = audioEngine.getTrackCount();
    if (trackCount <= 0)
        return;
    const auto trackIndex = juce::jlimit (0, trackCount - 1, state.selectedTrack);
    const auto snapshot = audioEngine.getProjectSnapshot();
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto title = chinese ? juce::String::fromUTF8 ("\u91cd\u547d\u540d\u8f68\u9053") : juce::String ("Rename track");
    const auto message = chinese ? juce::String::fromUTF8 ("\u8bf7\u8f93\u5165\u65b0\u7684\u8f68\u9053\u540d\u79f0")
                                 : juce::String ("Enter a new track name");
    auto* dialog = new juce::AlertWindow (title, message,
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", snapshot->tracks[static_cast<size_t> (trackIndex)].name);
    dialog->addButton (chinese ? juce::String::fromUTF8 ("\u4fdd\u5b58") : juce::String ("Save"),
                       1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (chinese ? juce::String::fromUTF8 ("\u53d6\u6d88") : juce::String ("Cancel"),
                       0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->centreAroundComponent (this, 360, 210);
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    dialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, dialog, trackIndex] (int result)
        {
            if (result == 1 && safeThis != nullptr
                && safeThis->audioEngine.renameTrack (trackIndex,
                                                       dialog->getTextEditorContents ("name")))
            {
                safeThis->browser.refreshContent();
                safeThis->mixer.refreshText();
                safeThis->mixer.syncFromEngine();
                safeThis->timeline.repaint();
                safeThis->inspector.syncFromState();
            }
        }), true);
}

void MainComponent::showClipMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto text = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };
    const auto clip = selectedClip();
    const auto canSplit = clip.has_value()
                       && audioEngine.getPosition() > clip->timelineStart + 0.001
                       && audioEngine.getPosition() < clip->timelineStart + clip->duration - 0.001;
    juce::PopupMenu menu;
    if (clip.has_value() && clip->source != nullptr)
    {
        auto clipName = clip->source->name;
        if (clipName.length() > 42)
            clipName = clipName.substring (0, 27) + "..." + clipName.substring (clipName.length() - 12);
        menu.addSectionHeader (text ("当前剪辑: ", "Current clip: ") + clipName);
    }
    menu.addItem (1, text ("在播放头处分割", "Split at playhead"), canSplit);
    menu.addItem (2, text ("创建副本", "Duplicate"), clip.has_value());
    menu.addItem (3, text ("删除剪辑", "Delete clip"), clip.has_value());
    menu.addItem (4, text ("重置剪辑增益", "Reset clip gain"), clip.has_value());
    menu.addItem (5, text ("删除所选空间区间", "Delete selected spatial region"),
                  state.selectedSpatialRegionId != 0);
    menu.addSeparator();
    juce::PopupMenu moveMenu;
    for (int destination = 0; destination < audioEngine.getTrackCount(); ++destination)
        moveMenu.addItem (100 + destination,
                          (chinese ? juce::String::fromUTF8 ("轨道 ") : juce::String ("Track "))
                              + juce::String (destination + 1),
                          clip.has_value() && destination != state.selectedTrack);
    menu.addSubMenu (text ("移动到轨道", "Move to track"), moveMenu, clip.has_value());

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clipMenu),
                        [safeThis] (int result)
    {
        auto* component = safeThis.getComponent();
        if (component == nullptr) return;
        if (result == 1) component->splitSelectedClipAtPlayhead();
        else if (result == 2) component->duplicateSelectedClip();
        else if (result == 3) component->deleteSelectedClip();
        else if (result == 4 && component->state.selectedClipId != 0)
        {
            component->audioEngine.setClipGainDb (component->state.selectedTrack,
                                                   component->state.selectedClipId, 0.0f);
            component->inspector.syncFromState();
        }
        else if (result == 5) component->timeline.deleteSelectedSpatialRegion();
        else if (result >= 100 && result < 100 + component->audioEngine.getTrackCount())
            component->moveSelectedClipToTrack (result - 100);
    });
}

void MainComponent::showViewMenu()
{
    const auto chinese = localizer.getLanguage() == Language::chinese;
    const auto text = [chinese] (const char* zh, const char* en)
    {
        return juce::String::fromUTF8 (chinese ? zh : en);
    };
    juce::PopupMenu menu;
    juce::PopupMenu workspaces;
    workspaces.addItem (1, localizer.text (TextId::workspaceEdit), true,
                        state.workspace == Workspace::edit);
    workspaces.addItem (2, localizer.text (TextId::workspaceSpatial), true,
                        state.workspace == Workspace::spatial);
    workspaces.addItem (3, localizer.text (TextId::workspaceMix), true,
                        state.workspace == Workspace::mix);
    menu.addSubMenu (text ("工作区", "Workspace"), workspaces);

    juce::PopupMenu panels;
    panels.addItem (10, localizer.text (TextId::media), true, state.layout.browserVisible);
    panels.addItem (11, localizer.text (TextId::inspector), true, state.layout.inspectorVisible);
    panels.addItem (12, localizer.text (TextId::mixer), true, state.layout.mixerVisible);
    menu.addSubMenu (text ("面板", "Panels"), panels);
    menu.addItem (20, localizer.text (TextId::workspaceLayout));
    menu.addSeparator();

    juce::PopupMenu themes;
    themes.addItem (30, text ("暗色", "Dark"), true, state.theme == ThemeMode::dark);
    themes.addItem (31, text ("亮色", "Light"), true, state.theme == ThemeMode::light);
    menu.addSubMenu (text ("主题", "Theme"), themes);
    juce::PopupMenu languages;
    languages.addItem (40, juce::String::fromUTF8 ("中文"), true,
                       state.language == Language::chinese);
    languages.addItem (41, "English", true, state.language == Language::english);
    menu.addSubMenu (text ("语言", "Language"), languages);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewMenu),
                        [safeThis] (int result)
    {
        auto* component = safeThis.getComponent();
        if (component == nullptr) return;
        if (result >= 1 && result <= 3)
            component->applyWorkspace (static_cast<Workspace> (result - 1));
        else if (result == 10)
            component->setPanelVisible ("browser", ! component->state.layout.browserVisible);
        else if (result == 11)
            component->setPanelVisible ("inspector", ! component->state.layout.inspectorVisible);
        else if (result == 12)
            component->setPanelVisible ("mixer", ! component->state.layout.mixerVisible);
        else if (result == 20)
            component->layoutButton.triggerClick();
        else if (result == 30 || result == 31)
        {
            component->state.theme = result == 30 ? ThemeMode::dark : ThemeMode::light;
            component->applyTheme();
        }
        else if (result == 40 || result == 41)
        {
            component->state.language = result == 40 ? Language::chinese : Language::english;
            component->localizer.setLanguage (component->state.language);
            component->refreshText();
        }
    });
}

void MainComponent::selectTrack (int trackIndex)
{
    state.selectedTrack = juce::jlimit (0, juce::jmax (0, audioEngine.getTrackCount() - 1), trackIndex);
    state.selectedClipId = 0;
    state.selectedSpatialRegionId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    inspector.setTab (InspectorTab::track);
    syncSpatialStateFromSelection();
    inspector.syncFromState();
    mixer.syncFromEngine();
    timeline.repaint();
    if (isPhoneLandscapeLayout())
        resized();
}

void MainComponent::selectProjectClip (int trackIndex, uint64_t clipId)
{
    if (trackIndex < 0 || trackIndex >= audioEngine.getTrackCount() || clipId == 0)
        return;
    const auto project = audioEngine.getProjectSnapshot();
    const auto& clips = project->tracks[static_cast<size_t> (trackIndex)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(), [clipId] (const auto& clip)
    {
        return clip.id == clipId;
    });
    if (iterator == clips.end())
        return;

    state.selectedTrack = trackIndex;
    state.selectedClipId = clipId;
    state.selectedSpatialRegionId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    audioEngine.setPosition (iterator->timelineStart);
    inspector.setTab (InspectorTab::clip);
    syncSpatialStateFromSelection();
    inspector.syncFromState();
    timeline.repaint();
    if (isPhoneLandscapeLayout())
        resized();
}

std::optional<AudioEngine::Clip> MainComponent::selectedClip() const
{
    if (state.selectedTrack < 0 || state.selectedTrack >= audioEngine.getTrackCount()
        || state.selectedClipId == 0)
        return std::nullopt;
    const auto project = audioEngine.getProjectSnapshot();
    const auto& clips = project->tracks[static_cast<size_t> (state.selectedTrack)].clips;
    const auto iterator = std::find_if (clips.begin(), clips.end(), [this] (const auto& clip)
    {
        return clip.id == state.selectedClipId;
    });
    return iterator != clips.end() ? std::optional<AudioEngine::Clip> (*iterator) : std::nullopt;
}

void MainComponent::duplicateSelectedClip()
{
    const auto clip = selectedClip();
    if (! clip.has_value()) return;
    if (const auto duplicate = audioEngine.duplicateClip (
            state.selectedTrack, clip->id, state.selectedTrack,
            clip->timelineStart + clip->duration))
        selectProjectClip (state.selectedTrack, *duplicate);
}

void MainComponent::splitSelectedClipAtPlayhead()
{
    const auto clip = selectedClip();
    if (! clip.has_value()) return;
    if (const auto right = audioEngine.splitClipAt (
            state.selectedTrack, clip->id, audioEngine.getPosition()))
        selectProjectClip (state.selectedTrack, *right);
}

void MainComponent::deleteSelectedClip()
{
    if (state.selectedSpatialRegionId != 0)
    {
        timeline.deleteSelectedSpatialRegion();
        return;
    }
    if (state.selectedClipId == 0
        || ! audioEngine.removeClip (state.selectedTrack, state.selectedClipId))
        return;
    state.selectedClipId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    inspector.setTab (InspectorTab::track);
    syncSpatialStateFromSelection();
    inspector.syncFromState();
    timeline.repaint();
    if (isPhoneLandscapeLayout())
        resized();
}

void MainComponent::moveSelectedClipToTrack (int destinationTrack)
{
    const auto clip = selectedClip();
    if (! clip.has_value() || destinationTrack < 0
        || destinationTrack >= audioEngine.getTrackCount() || destinationTrack == state.selectedTrack)
        return;
    const auto sourceTrack = state.selectedTrack;
    if (audioEngine.moveClip (sourceTrack, clip->id, destinationTrack, clip->timelineStart))
        selectProjectClip (destinationTrack, clip->id);
}

void MainComponent::resetSelectionAfterHistoryChange()
{
    state.selectedClipId = 0;
    state.selectedSpatialRegionId = 0;
    state.selectedRangeStart = 0.0;
    state.selectedRangeEnd = 0.0;
    inspector.setTab (InspectorTab::track);
    syncSpatialStateFromSelection();
    inspector.syncFromState();
    mixer.syncFromEngine();
    browser.refreshContent();
    timeline.repaint();
    if (isPhoneLandscapeLayout())
        resized();
}

void MainComponent::openAudioFile()
{
    if (! lastMediaDirectory.isDirectory())
        lastMediaDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = std::make_unique<juce::FileChooser> (localizer.text (TextId::chooseFile),
                                                        lastMediaDirectory,
                                                        "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    fileChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (auto* component = safeThis.getComponent())
        {
            const auto url = chooser.getURLResult();
            if (url.toString (false).isNotEmpty())
            {
                juce::Array<juce::URL> urls;
                urls.add (url);
                component->importAudioUrls (std::move (urls), true);
            }
        }
    });
}

void MainComponent::importMediaFiles()
{
    if (! lastMediaDirectory.isDirectory())
        lastMediaDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser> (
        localizer.text (TextId::importAudio), lastMediaDirectory,
        "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems;
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    fileChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (auto* component = safeThis.getComponent())
            component->importAudioUrls (chooser.getURLResults(), false);
    });
}

void MainComponent::importAudioUrls (juce::Array<juce::URL> urls, bool addToTimeline)
{
    if (urls.isEmpty())
        return;

    statusLeft.setText (localizer.getLanguage() == Language::chinese
                            ? juce::String::fromUTF8 ("正在导入音频...")
                            : juce::String ("Importing audio..."),
                        juce::dontSendNotification);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    mediaImportThread.addJob ([safeThis, jobUrls = std::move (urls), addToTimeline]
    {
        juce::Array<juce::File> importedFiles;
        std::vector<MediaImportFailure> importFailures;

        for (const auto& url : jobUrls)
        {
            auto item = materialiseMediaUrl (url);
            if (item.file.existsAsFile())
                importedFiles.add (item.file);
            else
                importFailures.push_back (std::move (item.failure));
        }

        juce::MessageManager::callAsync (
            [safeThis, messageFiles = std::move (importedFiles),
             messageFailures = std::move (importFailures), addToTimeline]
        {
            auto* component = safeThis.getComponent();
            if (component == nullptr)
                return;

            if (addToTimeline)
            {
                if (! messageFiles.isEmpty())
                    component->importAudioFile (messageFiles.getFirst());
            }
            else if (! messageFiles.isEmpty())
            {
                component->lastMediaDirectory = messageFiles.getFirst().getParentDirectory();
                component->browser.addImportedFiles (messageFiles);
                component->statusLeft.setText (
                    component->localizer.getLanguage() == Language::chinese
                        ? juce::String::fromUTF8 ("素材已导入，可拖到时间线")
                        : juce::String ("Media imported. Drag it to the timeline."),
                    juce::dontSendNotification);
                component->repaint();
            }

            if (! messageFailures.empty())
            {
                const auto chinese = component->localizer.getLanguage() == Language::chinese;
                juce::StringArray details;
                for (const auto& failure : messageFailures)
                    details.add (describeMediaImportFailure (failure, chinese));

                const auto heading = chinese
                    ? (messageFiles.isEmpty()
                           ? juce::String::fromUTF8 ("无法读取所选音频文件。请重新选择；如果文件来自云盘，请先下载到设备。")
                           : juce::String::fromUTF8 ("部分音频文件导入失败。"))
                    : (messageFiles.isEmpty()
                           ? juce::String ("The selected audio could not be read. Select it again, or download cloud files to the device first.")
                           : juce::String ("Some audio files could not be imported."));

                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    component->localizer.text (TextId::importAudio),
                    heading + "\n\n" + details.joinIntoString ("\n"), {}, component);
            }
        });
    });
}

void MainComponent::chooseMediaDirectory()
{
    if (! lastMediaDirectory.isDirectory())
        lastMediaDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = std::make_unique<juce::FileChooser> (
        localizer.getLanguage() == Language::chinese
            ? juce::String::fromUTF8 ("选择本地素材目录") : juce::String ("Choose local media folder"),
        lastMediaDirectory);
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& chooser)
    {
        const auto directory = chooser.getResult();
        if (directory.isDirectory())
        {
            lastMediaDirectory = directory;
            browser.setLibraryDirectory (directory);
        }
    });
}

void MainComponent::importAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    lastMediaDirectory = file.getParentDirectory();
    const auto newClip = audioEngine.addFileToTrack (
        file, juce::jlimit (0, juce::jmax (0, audioEngine.getTrackCount() - 1), state.selectedTrack),
        audioEngine.getPosition());
    if (! newClip.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            localizer.text (TextId::importAudio),
            localizer.getLanguage() == Language::chinese
                ? juce::String::fromUTF8 ("无法读取这个音频文件或格式不受支持。")
                : juce::String ("The audio file could not be read or its format is unsupported."),
            {}, this);
        return;
    }

    projectLabel.setText (file.getFileNameWithoutExtension(), juce::dontSendNotification);
    state.selectedClipId = *newClip;
    state.selectedSpatialRegionId = 0;
    inspector.setTab (InspectorTab::clip);
    inspector.syncFromState();
    mixer.syncFromEngine();
    browser.refreshContent();
    timeline.repaint();
    repaint();
}

void MainComponent::showExportPanel()
{
    if (exportInProgress)
        return;

    if (! audioEngine.hasFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                localizer.text (TextId::exportAudio),
                                                localizer.text (TextId::noAudioToExport),
                                                {}, this);
        return;
    }

    layoutPanel.setVisible (false);
    exportPanel.setVisible (! exportPanel.isVisible());
    resized();
    repaint();
}

void MainComponent::chooseExportDestination (AudioEngine::ExportSettings settings)
{
    auto suggestedName = projectLabel.getText().trim();
    if (suggestedName.isEmpty())
        suggestedName = "0i Studio Mix";
    suggestedName = juce::File::createLegalFileName (suggestedName) + ".wav";

    fileChooser = std::make_unique<juce::FileChooser> (localizer.text (TextId::exportAudio),
                                                        juce::File::getSpecialLocation (
                                                            juce::File::userMusicDirectory)
                                                            .getChildFile (suggestedName),
                                                        "*.wav");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser->launchAsync (chooserFlags, [this, settings] (const juce::FileChooser& chooser)
    {
        auto destination = chooser.getResult();
        if (destination != juce::File())
        {
            if (! destination.hasFileExtension ("wav"))
                destination = destination.withFileExtension ("wav");
            startExport (destination, settings);
        }
    });
}

void MainComponent::startExport (juce::File destination,
                                 AudioEngine::ExportSettings settings)
{
    if (exportInProgress)
        return;

    exportInProgress = true;
    exportButton.setEnabled (false);
    statusLeft.setText (localizer.text (TextId::exporting), juce::dontSendNotification);
    const auto projectSnapshot = audioEngine.getProjectSnapshot();
    const juce::Component::SafePointer<MainComponent> safeThis (this);
    exportThread.addJob ([safeThis, projectSnapshot, destination, settings]
    {
        const auto result = AudioEngine::exportProjectWav (projectSnapshot, destination, settings);

        juce::MessageManager::callAsync ([safeThis, result, destination]
        {
            if (auto* component = safeThis.getComponent())
            {
                component->exportInProgress = false;
                component->exportButton.setEnabled (true);
                component->showExportMessage (result.wasOk(), result.wasOk()
                    ? destination.getFullPathName() : result.getErrorMessage());
                component->updateTransport();
            }
        });
    });
}

void MainComponent::showExportMessage (bool success, const juce::String& detail)
{
    const auto title = localizer.text (success ? TextId::exportComplete : TextId::exportFailed);
    const auto message = success
        ? (localizer.getLanguage() == Language::chinese
               ? juce::String::fromUTF8 ("WAV 文件已写入：\n") + detail
               : juce::String ("WAV file written to:\n") + detail)
        : detail;
    juce::AlertWindow::showMessageBoxAsync (success ? juce::MessageBoxIconType::InfoIcon
                                                    : juce::MessageBoxIconType::WarningIcon,
                                            title, message, {}, this);
}

void MainComponent::updateTransport()
{
    timeLabel.setText (formatTime (audioEngine.getPosition()), juce::dontSendNotification);
    playButton.setIcon (audioEngine.isPlaying() ? Icon::pause : Icon::play);
    toolButtons[0]->setEnabled (audioEngine.canUndo());
    toolButtons[1]->setEnabled (audioEngine.canRedo());
    const auto clipCount = audioEngine.getClipCount();
    if (exportInProgress)
    {
        statusLeft.setText (localizer.text (TextId::exporting), juce::dontSendNotification);
    }
    else
    {
        statusLeft.setText ("48 kHz | 24 bit    Buffer 256 | 5.3 ms    "
                                + localizer.text (TextId::realtimeRender),
                            juce::dontSendNotification);
    }
    statusRight.setText (juce::String (clipCount) + " " + localizer.text (TextId::clip)
                             + "    " + juce::String (state.playbackSpeed, 2) + "x    Peak "
                             + juce::String (juce::Decibels::gainToDecibels (
                                                 juce::jmax (0.000001f, audioEngine.getMasterMeter())),
                                             1)
                             + " dBFS",
                         juce::dontSendNotification);
    playButton.repaint();
    timeline.repaint();
}

void MainComponent::setPlaybackSpeed (double speed)
{
    state.playbackSpeed = juce::jlimit (0.5, 2.0, speed);
    audioEngine.setPlaybackRate (state.playbackSpeed);
    speedButton.setButtonText (juce::String (state.playbackSpeed, 2)
                               + juce::String::fromUTF8 ("×"));
    inspector.syncFromState();
    updateTransport();
}

void MainComponent::syncSpatialStateFromSelection()
{
    if (state.selectedTrack >= 0 && state.selectedTrack < audioEngine.getTrackCount())
    {
        if (state.selectedSpatialRegionId != 0)
        {
            if (const auto region = audioEngine.getClipSpatialRegion (
                    state.selectedTrack, state.selectedClipId, state.selectedSpatialRegionId))
            {
                state.spatial = region->spatial;
                state.selectedSpatialRegionGainDb = region->gainDb;
                state.selectedSpatialRegionTransitionSeconds = region->transitionSeconds;
                return;
            }
            state.selectedSpatialRegionId = 0;
        }
        state.selectedSpatialRegionGainDb = 0.0f;
        state.selectedSpatialRegionTransitionSeconds
            = AudioEngine::defaultSpatialRegionTransitionSeconds;
        state.spatial = audioEngine.getTrackSpatialParameters (state.selectedTrack);
    }
}

void MainComponent::applySpatialStateToSelection()
{
    if (state.selectedTrack >= 0 && state.selectedTrack < audioEngine.getTrackCount())
    {
        if (state.selectedSpatialRegionId != 0)
        {
            if (audioEngine.setClipSpatialRegionParameters (
                    state.selectedTrack, state.selectedClipId,
                    state.selectedSpatialRegionId, state.spatial))
                return;
            state.selectedSpatialRegionId = 0;
        }
        audioEngine.setTrackSpatialParameters (state.selectedTrack, state.spatial);
    }
}

void MainComponent::setPanelVisible (const juce::String& panel, bool visible)
{
    if (isPhoneLayout())
    {
        if (visible)
        {
            mobilePanel = panel;
            state.layout.browserVisible = panel == "browser";
            state.layout.inspectorVisible = panel == "inspector";
            state.layout.mixerVisible = panel == "mixer";
        }
        else
        {
            if (mobilePanel == panel)
                mobilePanel.clear();
            if (panel == "browser") state.layout.browserVisible = false;
            else if (panel == "inspector") state.layout.inspectorVisible = false;
            else if (panel == "mixer") state.layout.mixerVisible = false;
        }
        updateToggleStates();
        resized();
        return;
    }
    if (panel == "browser") state.layout.browserVisible = visible;
    else if (panel == "inspector") state.layout.inspectorVisible = visible;
    else if (panel == "mixer") state.layout.mixerVisible = visible;
    updateToggleStates();
    resized();
}
} // namespace oi
