#pragma once

#include <JuceHeader.h>

#include "SpatialParameters.h"

#include <cstdint>

namespace oi
{
enum class ThemeMode { dark, light };
enum class Language { chinese, english };
enum class Workspace { edit, spatial, mix };
enum class InspectorTab { clip, spatial, effects, track };
enum class Tool { select, range, split, fade, snap };

enum class TextId
{
    file, edit, track, clip, view, saved, media, effects, presets, projectFiles,
    collections, fieldRecordings, workspaceEdit, workspaceSpatial, workspaceMix,
    layout, inspector, mixer, tracks, leadVocal, synth, drums, atmosphere,
    fxReturn, spatial, objectAudio, azimuth, elevation, distance, orbitSpeed,
    spread, doppler, attenuation, airAbsorption, headTracking, regionGain,
    transitionTime, start, duration,
    fadeIn, fadeOut, timeAndPitch, playbackSpeed, stretchMode, preservePitch,
    preserveFormants, transientProtection, level, clipGain, pitch, insertChain,
    threshold, ratio, input, output, volume, pan, sendA, sendB, phaseInvert,
    monitor, automationRead, master, realtimeRender, workspaceLayout,
    browserWidth, inspectorWidth, trackHeight, mixerHeight, density, compact,
    comfortable, spacious, reset, done, search, importAudio, noAudioLoaded,
    audioFiles, chooseFile, play, stop, record, loop, settings, selection,
    sourceSpread, renderMode, directivity, roomSend, earlyReflections,
    pitchLock, tempo, speed, cpu, sampleRate, bufferSize, audioDevice,
    position, fades, algorithm, gain, objects, listener, close, exportAudio,
    exportFormat, channelLayout, bitDepth, exportProject, exporting,
    exportComplete, exportFailed, noAudioToExport
};

class Localizer
{
public:
    explicit Localizer (Language initial = Language::chinese) : language (initial) {}

    void setLanguage (Language value) noexcept { language = value; }
    [[nodiscard]] Language getLanguage() const noexcept { return language; }

    [[nodiscard]] juce::String text (TextId id) const
    {
        struct Entry { TextId id; const char* zh; const char* en; };
        static constexpr Entry entries[] {
            { TextId::file, "文件", "File" }, { TextId::edit, "编辑", "Edit" },
            { TextId::track, "轨道", "Track" }, { TextId::clip, "剪辑", "Clip" },
            { TextId::view, "视图", "View" }, { TextId::saved, "已保存", "Saved" },
            { TextId::media, "素材库", "Media" }, { TextId::effects, "效果器", "Effects" },
            { TextId::presets, "预设", "Presets" }, { TextId::projectFiles, "工程文件", "Project files" },
            { TextId::collections, "收藏", "Collections" }, { TextId::fieldRecordings, "环境录音", "Field recordings" },
            { TextId::workspaceEdit, "剪辑", "Edit" }, { TextId::workspaceSpatial, "空间", "Spatial" },
            { TextId::workspaceMix, "混音", "Mix" }, { TextId::layout, "布局", "Layout" },
            { TextId::inspector, "检查器", "Inspector" }, { TextId::mixer, "混音器", "Mixer" },
            { TextId::tracks, "轨道", "Tracks" }, { TextId::leadVocal, "主唱", "Lead Vocal" },
            { TextId::synth, "合成器", "Synth" }, { TextId::drums, "鼓组", "Drums" },
            { TextId::atmosphere, "环境声", "Atmosphere" }, { TextId::fxReturn, "效果返回", "FX Return" },
            { TextId::spatial, "空间", "Spatial" }, { TextId::objectAudio, "对象音频 · 扬声器空间渲染", "Object audio · speaker spatial render" },
            { TextId::azimuth, "方位角", "Azimuth" }, { TextId::elevation, "垂直仰角", "Elevation" },
            { TextId::distance, "距离", "Distance" }, { TextId::orbitSpeed, "旋转速度", "Orbit speed" },
            { TextId::spread, "声源扩散", "Source spread" }, { TextId::doppler, "多普勒", "Doppler" },
            { TextId::attenuation, "距离衰减", "Attenuation" }, { TextId::airAbsorption, "空气吸收", "Air absorption" },
            { TextId::headTracking, "头部追踪", "Head tracking" }, { TextId::regionGain, "局部音量", "Region volume" },
            { TextId::transitionTime, "平滑过渡", "Smooth transition" }, { TextId::start, "开始", "Start" },
            { TextId::duration, "时长", "Duration" }, { TextId::fadeIn, "淡入", "Fade in" },
            { TextId::fadeOut, "淡出", "Fade out" }, { TextId::timeAndPitch, "时间与音高", "Time and pitch" },
            { TextId::playbackSpeed, "播放速度", "Playback speed" }, { TextId::stretchMode, "拉伸算法", "Stretch mode" },
            { TextId::preservePitch, "保持音高", "Preserve pitch" }, { TextId::preserveFormants, "保持共振峰", "Preserve formants" },
            { TextId::transientProtection, "瞬态保护", "Transient protection" }, { TextId::level, "电平", "Level" },
            { TextId::clipGain, "剪辑增益", "Clip gain" }, { TextId::pitch, "音高", "Pitch" },
            { TextId::insertChain, "插入效果链", "Insert chain" }, { TextId::threshold, "压缩阈值", "Threshold" },
            { TextId::ratio, "压缩比", "Ratio" }, { TextId::input, "输入", "Input" },
            { TextId::output, "输出", "Output" }, { TextId::volume, "音量", "Volume" },
            { TextId::pan, "声像", "Pan" }, { TextId::sendA, "发送 A · Hall", "Send A · Hall" },
            { TextId::sendB, "发送 B · Delay", "Send B · Delay" }, { TextId::phaseInvert, "反相", "Invert phase" },
            { TextId::monitor, "输入监听", "Input monitor" }, { TextId::automationRead, "读取自动化", "Read automation" },
            { TextId::master, "主控", "Master" }, { TextId::realtimeRender, "实时渲染 · 扬声器空间", "Realtime render · speaker spatial" },
            { TextId::workspaceLayout, "工作区布局", "Workspace layout" }, { TextId::browserWidth, "素材库宽度", "Media width" },
            { TextId::inspectorWidth, "检查器宽度", "Inspector width" }, { TextId::trackHeight, "轨道高度", "Track height" },
            { TextId::mixerHeight, "混音器高度", "Mixer height" }, { TextId::density, "界面密度", "UI density" },
            { TextId::compact, "紧凑", "Compact" }, { TextId::comfortable, "标准", "Standard" },
            { TextId::spacious, "宽松", "Spacious" }, { TextId::reset, "重置", "Reset" },
            { TextId::done, "完成", "Done" }, { TextId::search, "搜索素材、效果器", "Search media and effects" },
            { TextId::importAudio, "导入音频", "Import audio" }, { TextId::noAudioLoaded, "尚未载入音频", "No audio loaded" },
            { TextId::audioFiles, "音频文件", "Audio files" }, { TextId::chooseFile, "选择文件", "Choose file" },
            { TextId::play, "播放", "Play" }, { TextId::stop, "停止", "Stop" },
            { TextId::record, "录音", "Record" }, { TextId::loop, "循环", "Loop" },
            { TextId::settings, "设置", "Settings" }, { TextId::selection, "选择", "Selection" },
            { TextId::sourceSpread, "声源扩散", "Source spread" }, { TextId::renderMode, "渲染模式", "Render mode" },
            { TextId::directivity, "指向性", "Directivity" }, { TextId::roomSend, "房间发送", "Room send" },
            { TextId::earlyReflections, "早期反射", "Early reflections" }, { TextId::pitchLock, "锁定音高", "Pitch lock" },
            { TextId::tempo, "速度", "Tempo" }, { TextId::speed, "倍速", "Speed" },
            { TextId::cpu, "处理器", "CPU" }, { TextId::sampleRate, "采样率", "Sample rate" },
            { TextId::bufferSize, "缓冲区", "Buffer" }, { TextId::audioDevice, "音频设备", "Audio device" },
            { TextId::position, "位置", "Position" }, { TextId::fades, "淡化", "Fades" },
            { TextId::algorithm, "算法", "Algorithm" }, { TextId::gain, "增益", "Gain" },
            { TextId::objects, "对象", "Objects" }, { TextId::listener, "监听点", "Listener" },
            { TextId::close, "关闭", "Close" }, { TextId::exportAudio, "导出音频", "Export audio" },
            { TextId::exportFormat, "导出格式", "Export format" }, { TextId::channelLayout, "声道布局", "Channel layout" },
            { TextId::bitDepth, "位深", "Bit depth" }, { TextId::exportProject, "导出工程", "Export project" },
            { TextId::exporting, "正在导出音频...", "Exporting audio..." }, { TextId::exportComplete, "导出完成", "Export complete" },
            { TextId::exportFailed, "导出失败", "Export failed" }, { TextId::noAudioToExport, "请先导入音频，再导出工程。", "Import audio before exporting the project." }
        };

        for (const auto& entry : entries)
            if (entry.id == id)
                return juce::String::fromUTF8 (language == Language::chinese ? entry.zh : entry.en);

        return {};
    }

private:
    Language language;
};

struct LayoutSettings
{
    int browserWidth = 182;
    int inspectorWidth = 268;
    int trackHeight = 64;
    int mixerHeight = 166;
    int density = 1;
    bool browserVisible = true;
    bool inspectorVisible = true;
    bool mixerVisible = true;
};

struct AppState
{
    ThemeMode theme = ThemeMode::dark;
    Language language = Language::chinese;
    Workspace workspace = Workspace::edit;
    InspectorTab inspectorTab = InspectorTab::spatial;
    Tool activeTool = Tool::select;
    LayoutSettings layout;
    SpatialParameters spatial;
    double playbackSpeed = 1.0;
    bool pitchLocked = true;
    bool looping = false;
    int selectedTrack = 0;
    uint64_t selectedClipId = 0;
    uint64_t selectedSpatialRegionId = 0;
    double selectedRangeStart = 0.0;
    double selectedRangeEnd = 0.0;
    float selectedSpatialRegionGainDb = 0.0f;
    double selectedSpatialRegionTransitionSeconds = 0.35;
};
} // namespace oi
