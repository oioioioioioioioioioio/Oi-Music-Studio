#include <JuceHeader.h>
#include "StudioComponents.h"

namespace
{
#if JUCE_ANDROID || JUCE_IOS
class OiStudioSafeArea final : public juce::Component
{
public:
    explicit OiStudioSafeArea (std::unique_ptr<juce::Component> contentToOwn)
        : content (std::move (contentToOwn))
    {
        addAndMakeVisible (*content);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        if (const auto* display = juce::Desktop::getInstance().getDisplays()
                                      .getDisplayForRect (bounds))
        {
            const auto insets = display->safeAreaInsets;
            if (bounds.getWidth() > bounds.getHeight())
            {
                // Android already lays the JUCE peer edge-to-edge. In landscape,
                // only reserve the short-edge cutout instead of adding a second
                // status/navigation-bar strip above and below the editor.
                bounds = bounds.withTrimmedLeft (insets.getLeft())
                               .withTrimmedRight (insets.getRight());
            }
            else
            {
                bounds = insets.subtractedFrom (bounds);
            }
        }
        content->setBounds (bounds);
    }

private:
    std::unique_ptr<juce::Component> content;
};
#endif
}

class OiStudioApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "0i-Studio"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::Logger::writeToLog ("0i-Studio startup: application initialise");
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        juce::Logger::writeToLog ("0i-Studio startup: main window ready");
    }

    void shutdown() override
    {
       #if JUCE_ANDROID
        juce::Desktop::getInstance().setKioskModeComponent (nullptr);
       #endif
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (std::move (name), juce::Colour (0xff0f1313),
                              DocumentWindow::allButtons)
        {
#if JUCE_ANDROID || JUCE_IOS
            setUsingNativeTitleBar (false);
            setTitleBarHeight (0);
            setTitleBarButtonsRequired (0, false);
            setContentOwned (new OiStudioSafeArea (std::make_unique<oi::MainComponent>()), true);
            setResizable (false, false);
#else
            setUsingNativeTitleBar (true);
            setContentOwned (new oi::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (980, 640, 2560, 1600);
            centreWithSize (1280, 800);
#endif
            setVisible (true);

           #if JUCE_ANDROID
            juce::Desktop::getInstance().setKioskModeComponent (this, false);
           #elif JUCE_IOS
            setFullScreen (true);
           #endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (OiStudioApplication)
