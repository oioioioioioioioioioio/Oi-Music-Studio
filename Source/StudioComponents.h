#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "AppState.h"
#include "AudioEngine.h"
#include "StudioLookAndFeel.h"

namespace oi
{
enum class Icon
{
    logo, undo, redo, pointer, range, scissors, fade, magnet, previous, play,
    pause, stop, record, loop, waveform, orbit, mixer, panelLeft, panelRight,
    panelBottom, layout, sun, moon, settings, import, search, plus, exportFile,
    close, copy, trash, speaker, folder, music, chevronDown, more
};

class IconButton final : public juce::Button
{
public:
    IconButton (Icon icon, const juce::String& name);
    void setIcon (Icon value) noexcept { icon = value; repaint(); }
    void setAccent (bool value) { getProperties().set ("accent", value); repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};

class PanelHeader final : public juce::Component
{
public:
    PanelHeader (Localizer&, TextId title);
    void setTitle (TextId value) { titleId = value; repaint(); }
    void paint (juce::Graphics&) override;

private:
    Localizer& localizer;
    TextId titleId;
};

class ClipContextToolbar final : public juce::Component
{
public:
    ClipContextToolbar();
    void paint (juce::Graphics&) override;
    void resized() override;

    IconButton moveButton { Icon::pointer, "Move clip" };
    IconButton rangeButton { Icon::range, "Spatial range" };
    IconButton splitButton { Icon::scissors, "Split clip" };
    IconButton duplicateButton { Icon::copy, "Duplicate clip" };
    IconButton spatialButton { Icon::orbit, "Spatial settings" };
    IconButton deleteButton { Icon::trash, "Delete clip" };
};

class MediaBrowser final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener
{
public:
    MediaBrowser (Localizer&, AudioEngine&);
    ~MediaBrowser() override;
    void refreshText();
    void refreshContent();
    void addImportedFiles (const juce::Array<juce::File>& files);
    void setLibraryDirectory (const juce::File&);
    [[nodiscard]] juce::File getLibraryDirectory() const { return libraryDirectory; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    std::function<void()> onImportRequested;
    std::function<void()> onDirectoryRequested;
    std::function<void(const juce::File&)> onAudioFileRequested;
    std::function<void(int, uint64_t)> onProjectClipSelected;
    std::function<void(const SpatialParameters&)> onSpatialPresetSelected;

private:
    enum class Page { project, library, presets };
    enum class ItemKind { projectClip, audioFile, spatialPreset };

    struct Item
    {
        ItemKind kind = ItemKind::audioFile;
        juce::String title;
        juce::String detail;
        juce::File file;
        int trackIndex = -1;
        uint64_t clipId = 0;
        SpatialParameters preset;
    };

    int getNumRows() override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rowsToDescribe) override;
    juce::String getNameForRow (int rowNumber) override;
    void paintListBoxItem (int rowNumber, juce::Graphics&, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int lastRowSelected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setPage (Page);
    void scanLibrary();
    void rebuildItems();
    void activateItem (int row);

    Localizer& localizer;
    AudioEngine& audioEngine;
    PanelHeader header;
    IconButton importButton { Icon::plus, "Import" };
    IconButton directoryButton { Icon::folder, "Choose media folder" };
    IconButton refreshButton { Icon::redo, "Refresh" };
    juce::TextEditor search;
    juce::TextButton filesTab, effectsTab, presetsTab;
    juce::ListBox itemList { "Media browser items", this };
    Page page = Page::project;
    juce::File libraryDirectory;
    std::vector<juce::File> libraryFiles;
    std::vector<Item> items;
};

class TimelineComponent final : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::Timer,
                                private juce::ChangeListener,
                                public juce::DragAndDropTarget
{
public:
    TimelineComponent (Localizer&, AppState&, AudioEngine&);
    ~TimelineComponent() override;

    void refreshText();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;
    bool deleteSelectedSpatialRegion();
    void setSnappingEnabled (bool enabled) noexcept { snappingEnabled = enabled; repaint(); }
    [[nodiscard]] bool isSnappingEnabled() const noexcept { return snappingEnabled; }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    std::function<void(int)> onTrackSelected;
    std::function<void(uint64_t)> onClipSelected;
    std::function<void()> onSpatialRangeSelected;
    std::function<void()> onTrackControlsChanged;
    std::function<void()> onAddTrackRequested;
    std::function<void(int)> onRenameTrackRequested;

private:
    struct TrackData
    {
        TextId name = TextId::tracks;
        juce::String role { "ST" };
        juce::Colour colour { 0xff6b9ff1 };
    };

    struct TouchPoint
    {
        int sourceIndex = -1;
        juce::Point<float> position;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void drawClipThumbnail (juce::Graphics&, juce::Rectangle<float>,
                            const AudioEngine::Clip&) const;
    [[nodiscard]] double timelineExtent() const noexcept;
    [[nodiscard]] double visibleLength() const noexcept;
    [[nodiscard]] juce::Rectangle<int> timelineArea() const;
    [[nodiscard]] int trackHeaderWidth() const noexcept;
    [[nodiscard]] int rulerHeight() const noexcept;
    [[nodiscard]] int trackHeight() const noexcept;
    [[nodiscard]] int visibleTrackCount() const noexcept;
    [[nodiscard]] double timeAtX (float x) const noexcept;
    [[nodiscard]] float xAtTime (double seconds) const noexcept;
    [[nodiscard]] int trackAtY (float y) const noexcept;
    [[nodiscard]] std::optional<AudioEngine::Clip> clipAt (int trackIndex,
                                                           double timelineSeconds) const;
    [[nodiscard]] juce::Rectangle<float> spatialRegionBounds (
        const AudioEngine::Clip&, const AudioEngine::SpatialRegion&, int trackIndex) const;
    [[nodiscard]] juce::Rectangle<float> spatialRegionDeleteBounds (
        const AudioEngine::Clip&, const AudioEngine::SpatialRegion&, int trackIndex) const;
    void setPlayheadFromX (float x);
    void selectClip (int trackIndex, uint64_t clipId);
    void deleteSelectedClip();
    void showClipMenu (juce::Point<int> position, int trackIndex,
                       uint64_t clipId, double clickedTime);
    void paintClip (juce::Graphics&, const AudioEngine::Clip&, int trackIndex,
                    juce::Rectangle<float> lane, juce::Colour trackColour,
                    double timelineLength, bool dragged);
    [[nodiscard]] double snapTimeForDrag (double desiredStart, int destinationTrack,
                                          uint64_t clipId, double duration) const noexcept;
    [[nodiscard]] bool hasSnapForDrag (double desiredStart, int destinationTrack,
                                       uint64_t clipId, double duration,
                                       double& snappedStart) const noexcept;
    void updateDropPreview (juce::Point<float> localPosition);
    void updateTouchPoint (const juce::MouseEvent&, bool addIfMissing);
    void removeTouchPoint (int sourceIndex) noexcept;
    [[nodiscard]] int activeTouchCount() const noexcept;
    void cancelEditingGesture() noexcept;
    void beginPinchGesture();
    void updatePinchGesture();
    void clampTimelineViewStart() noexcept;

    Localizer& localizer;
    AppState& state;
    AudioEngine& audioEngine;
    IconButton addTrackButton { Icon::plus, "Add track" };
    std::array<TrackData, AudioEngine::maximumTrackCount> tracks;
    bool draggingPlayhead = false;
    bool draggingClip = false;
    bool draggingRange = false;
    bool clipDragMoved = false;
    bool rangeDragMoved = false;
    int dragSourceTrack = -1;
    int dragTargetTrack = -1;
    uint64_t draggedClipId = 0;
    uint64_t hoveredClipId = 0;
    double dragClipOriginalStart = 0.0;
    double dragOffsetSeconds = 0.0;
    double dragPreviewStart = 0.0;
    double snapGuideTime = -1.0;
    double rangeAnchor = 0.0;
    double rangePreviewStart = 0.0;
    double rangePreviewEnd = 0.0;
    uint64_t rangeHitRegionId = 0;
    double timelineZoom = 1.0;
    double timelineViewStart = 0.0;
    std::array<TouchPoint, 2> activeTouches;
    int primaryTouchSource = -1;
    bool touchPanPending = false;
    bool panningTimeline = false;
    bool touchTapSetsPlayhead = false;
    bool pinchingTimeline = false;
    bool touchGestureConsumed = false;
    juce::Point<float> touchPanOrigin;
    double touchPanViewStart = 0.0;
    float pinchInitialDistance = 1.0f;
    double pinchInitialZoom = 1.0;
    double pinchAnchorTime = 0.0;
    double pinchAnchorNormal = 0.5;
    bool snappingEnabled = true;
    int dragDropTrack = -1;
    double dragDropTime = 0.0;
};

class SpatialCanvas final : public juce::Component
{
public:
    SpatialCanvas (Localizer&, AppState&, AudioEngine&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onParametersChanged;

private:
    void updatePositionFromPoint (juce::Point<float> point);
    void updateElevationFromPoint (juce::Point<float> point);
    [[nodiscard]] juce::Point<float> sourcePoint (const SpatialParameters&) const;
    [[nodiscard]] juce::Rectangle<float> positionArea() const;
    [[nodiscard]] juce::Rectangle<float> elevationArea() const;

    Localizer& localizer;
    AppState& state;
    AudioEngine& audioEngine;
    bool draggingPosition = false;
    bool draggingElevation = false;
};

class ParameterRow final : public juce::Component
{
public:
    ParameterRow (Localizer&, TextId, double minimum, double maximum, double value,
                  double step, juce::String suffix, bool signedValue = false);

    void refreshText();
    void resized() override;
    void lookAndFeelChanged() override;
    void setValue (double value, juce::NotificationType notification = juce::dontSendNotification);
    [[nodiscard]] double getValue() const { return slider.getValue(); }
    juce::Slider& getSlider() noexcept { return slider; }
    std::function<void(double)> onValueChanged;

private:
    void updateValueLabel();

    Localizer& localizer;
    TextId textId;
    juce::String suffix;
    bool signedValue;
    juce::Label nameLabel;
    juce::TextEditor valueEditor;
    juce::Slider slider;
};

class InspectorPanel final : public juce::Component
{
public:
    InspectorPanel (Localizer&, AppState&, AudioEngine&);
    void refreshText();
    void syncFromState();
    void paint (juce::Graphics&) override;
    void resized() override;
    void setTab (InspectorTab);

    std::function<void()> onSpatialChanged;
    std::function<void(double)> onPlaybackSpeedChanged;
    std::function<void(double)> onTrackGainChanged;
    std::function<void(double)> onTrackPanChanged;
    std::function<void(float, double)> onSpatialRegionEnvelopeChanged;
    std::function<void()> onDeleteSpatialRegion;

private:
    void configureTabButton (juce::TextButton&, InspectorTab);
    void addSectionLabel (juce::Label&);
    void layoutRows (juce::Rectangle<int>);
    void setVisiblePage();

    Localizer& localizer;
    AppState& state;
    AudioEngine& audioEngine;
    std::array<juce::TextButton, 4> tabs;
    juce::Label selectionTitle, selectionSubtitle, badge;
    IconButton deleteSpatialRegionButton { Icon::close, "Delete spatial region" };
    juce::Viewport parameterViewport { "Inspector parameters" };
    juce::Component parameterContent;

    ParameterRow azimuth, elevation, distance, orbitSpeed, spread, directivity;
    ParameterRow regionGain, regionTransition;
    juce::ComboBox attenuation;
    juce::ToggleButton spatialEnabled, airAbsorption;

    ParameterRow clipSpeed, transientProtection, clipGain, clipPitch;
    juce::ComboBox stretchMode;
    juce::ToggleButton preservePitch, preserveFormants;

    ParameterRow threshold, ratio, eqGain, eqFrequency;
    std::array<juce::TextButton, 3> inserts;

    ParameterRow trackVolume, pan, sendA, sendB;
    juce::ComboBox input, output;
    juce::ToggleButton phaseInvert, monitor, automationRead;

    juce::Label sectionOne, sectionTwo;
};

class MixerComponent final : public juce::Component
{
public:
    MixerComponent (Localizer&, AppState&, AudioEngine&);
    void refreshText();
    void syncFromEngine();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Channel
    {
        juce::Label name, value;
        juce::Slider fader, pan;
        juce::TextButton mute { "M" }, solo { "S" };
    };

    Localizer& localizer;
    AppState& state;
    AudioEngine& audioEngine;
    std::array<Channel, AudioEngine::maximumTrackCount + 1> channels;
    int laidOutTrackCount = -1;
};

class LayoutPanel final : public juce::Component
{
public:
    LayoutPanel (Localizer&, AppState&);
    void refreshText();
    void syncFromState();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLayoutChanged;
    std::function<void()> onClose;

private:
    void configureSlider (juce::Slider&, double min, double max);
    void updateOutputs();

    Localizer& localizer;
    AppState& state;
    juce::Label title;
    IconButton closeButton { Icon::close, "Close" };
    juce::ToggleButton browserVisible, inspectorVisible, mixerVisible;
    juce::Label browserLabel, inspectorLabel, trackLabel, mixerLabel, densityLabel;
    juce::Label browserValue, inspectorValue, trackValue, mixerValue;
    juce::Slider browserWidth, inspectorWidth, trackHeight, mixerHeight;
    std::array<juce::TextButton, 3> densityButtons;
    juce::TextButton resetButton, doneButton;
};

class ExportPanel final : public juce::Component
{
public:
    explicit ExportPanel (Localizer&);
    void refreshText();
    void paint (juce::Graphics&) override;
    void resized() override;

    [[nodiscard]] AudioEngine::ExportSettings getSettings() const;
    std::function<void(AudioEngine::ExportSettings)> onExport;
    std::function<void()> onClose;

private:
    Localizer& localizer;
    juce::Label title, formatLabel, sampleRateLabel, bitDepthLabel, channelLayoutLabel;
    juce::Label formatValue;
    IconButton closeButton { Icon::close, "Close" };
    juce::ComboBox sampleRate, bitDepth, channelLayout;
    juce::TextButton exportAction;
};

class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshText();
    void applyTheme();
    void applyWorkspace (Workspace);
    void updateToggleStates();
    void openAudioFile();
    void importMediaFiles();
    void importAudioUrls (juce::Array<juce::URL>, bool addToTimeline);
    void chooseMediaDirectory();
    void importAudioFile (const juce::File&);
    void showAudioDeviceSettings();
    void showAboutAndContact();
    void checkForUpdates (bool showFeedback);
    void showFileMenu();
    void showEditMenu();
    void showTrackMenu();
    void addTrack();
    void renameSelectedTrack();
    void showClipMenu();
    void showViewMenu();
    void selectTrack (int trackIndex);
    void selectProjectClip (int trackIndex, uint64_t clipId);
    [[nodiscard]] std::optional<AudioEngine::Clip> selectedClip() const;
    void duplicateSelectedClip();
    void splitSelectedClipAtPlayhead();
    void deleteSelectedClip();
    void moveSelectedClipToTrack (int destinationTrack);
    void resetSelectionAfterHistoryChange();
    void showExportPanel();
    void chooseExportDestination (AudioEngine::ExportSettings);
    void startExport (juce::File, AudioEngine::ExportSettings);
    void showExportMessage (bool success, const juce::String& detail = {});
    void updateTransport();
    void setPlaybackSpeed (double);
    void syncSpatialStateFromSelection();
    void applySpatialStateToSelection();
    void setPanelVisible (const juce::String& panel, bool visible);
    void paintTitleBar (juce::Graphics&);
    void paintToolbar (juce::Graphics&);
    void paintWorkspaceBar (juce::Graphics&);
    void paintStatusBar (juce::Graphics&);
    enum class ResponsiveMode { phonePortrait, phoneLandscape, compact, full };
    [[nodiscard]] ResponsiveMode responsiveMode() const noexcept;
    [[nodiscard]] bool isPhoneLayout() const noexcept;
    [[nodiscard]] bool isPhoneLandscapeLayout() const noexcept;
    [[nodiscard]] int titleBarHeight() const noexcept;
    [[nodiscard]] int toolbarHeight() const noexcept;
    [[nodiscard]] int workspaceBarHeight() const noexcept;
    [[nodiscard]] int statusBarHeight() const noexcept;
    void showMobileMenu();
    void showMobilePanel (const juce::String& panel);

    StudioLookAndFeel studioLook;
    AppState state;
    Localizer localizer;
    AudioEngine audioEngine;
    juce::TooltipWindow tooltipWindow;

    juce::TextButton fileMenu, editMenu, trackMenu, clipMenu, viewMenu;
    juce::Label brandLabel, projectLabel, savedLabel, timeLabel, barsLabel;
    juce::TextButton chineseButton, englishButton, tempoButton, speedButton, pitchLockButton;
    IconButton themeButton { Icon::moon, "Theme" };
    IconButton settingsButton { Icon::settings, "Settings" };
    IconButton moreButton { Icon::more, "More" };

    std::array<std::unique_ptr<IconButton>, 8> toolButtons;
    IconButton previousButton { Icon::previous, "Previous" };
    IconButton playButton { Icon::play, "Play" };
    IconButton stopButton { Icon::stop, "Stop" };
    IconButton recordButton { Icon::record, "Record" };
    IconButton loopButton { Icon::loop, "Loop" };
    IconButton exportButton { Icon::exportFile, "Export" };

    std::array<juce::TextButton, 3> workspaceButtons;
    IconButton browserToggle { Icon::panelLeft, "Media" };
    IconButton mixerToggle { Icon::panelBottom, "Mixer" };
    IconButton inspectorToggle { Icon::panelRight, "Inspector" };
    IconButton layoutButton { Icon::layout, "Layout" };
    ClipContextToolbar clipContextToolbar;

    MediaBrowser browser;
    TimelineComponent timeline;
    SpatialCanvas spatialCanvas;
    InspectorPanel inspector;
    MixerComponent mixer;
    LayoutPanel layoutPanel;
    ExportPanel exportPanel;

    juce::Label statusLeft, statusRight;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastMediaDirectory;
    juce::ThreadPool mediaImportThread { juce::ThreadPool::Options().withNumberOfThreads (1)
                                                                    .withThreadName ("0i Media Import") };
    juce::ThreadPool exportThread { juce::ThreadPool::Options().withNumberOfThreads (1)
                                                               .withThreadName ("0i WAV Export") };
    juce::ThreadPool updateThread { juce::ThreadPool::Options().withNumberOfThreads (1)
                                                               .withThreadName ("0i Update Check") };
    bool exportInProgress = false;
    bool updateCheckInProgress = false;
    juce::String mobilePanel;
};
} // namespace oi
