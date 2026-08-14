# 0i-Studio

Native C++20/JUCE desktop and Android application for a professional multitrack
and spatial audio editor.

Current release: **0.1.6** (`versionCode 7` on Android).

## Current milestone

- Professional Edit, Spatial, and Mix workspaces
- Dark/light themes and runtime Chinese/English switching
- Adjustable browser, inspector, track, and mixer layout dimensions
- One empty starting track, with user-added, nameable, and renameable tracks (up to 12), real waveform thumbnails, and clip metadata
- Explicit multi-file/folder media import, media-browser drag-and-drop to any track, time-ruler seeking, clip selection, splitting, drag/cross-track move, duplication, deletion, undo, and redo
- Same-track clip-edge snapping with a visible guide and temporary `Alt` bypass
- Timeline navigation: on touch screens, drag the ruler or empty lane with one finger to pan and pinch with two fingers to zoom; on desktop, `Ctrl+wheel` zooms, `Alt+wheel` scrolls horizontally, and `Space` toggles play/pause
- The timeline header `+` button opens the same named-track dialog as the Track menu
- Real-time multitrack mixing with per-track gain, pan, mute, solo, and meters
- Clip gain, master gain, transport seek/loop, and variable-speed playback
- Per-track spatial rendering with enable/bypass, radius, azimuth, elevation, orbit speed, spread, directivity, attenuation curves, and optional air absorption
- Clip-local spatial and relative-volume automation over a continuous track-level 3D bed, with adjustable smooth entry/exit transitions and timeline envelopes
- Offline PCM WAV export at 44.1/48/88.2/96 kHz and 16/24/32-bit, with Stereo, 5.1, 7.1, 5.1.4, and 7.1.4 speaker layouts
- Bilingual GitHub Release update checks on Windows and Android, with platform-specific downloads and a manual menu action

### Online updates

Both clients check the repository's latest GitHub Release shortly after startup.
The check runs on a background thread and stays silent when the installed version
is current or the network is unavailable. **File > Check for updates** on Windows,
or **More > Check for updates** on Android, performs a manual check with feedback.

Updates are user-confirmed: Windows opens the published `.exe` download, while
Android opens the published `.apk` download and lets the operating system handle
installation permission and package verification. Release assets must use these
names so each client selects its own package:

- `0i-Studio-<version>-windows-x64.exe`
- `0i-Studio-<version>-android.apk`

### Menu and media browser

The top menus are functional editing entry points rather than placeholders:

- **File** imports audio, changes the media folder, opens export/audio-device settings, checks for updates, and shows contact information.
- **Edit** provides undo/redo, clip duplication/deletion, playhead splitting, and select/range/split tools.
- **Track** imports to the selected track, toggles mute/solo/3D rendering, resets track mix, and selects a track.
- **Clip** splits, duplicates, deletes, resets clip gain, removes a selected spatial region, and moves a clip to another track.
- **View** switches Edit/Spatial/Mix workspaces, toggles panels, opens layout controls, and changes theme/language.

The left media browser has Project, Local, and Spatial Presets pages. It starts empty and only lists files or folders explicitly imported by the user; it never scans a fixed drive. Imported media can be dragged directly to a chosen track or double-clicked to add it at the playhead. Project items locate clips in the timeline, while presets apply a complete spatial parameter set to the selected track or spatial region. Search and refresh apply to the active page.

The current JUCE reader path is verified for WAV, AIFF, FLAC, OGG, and MP3. M4A files can be listed by the browser, but AAC/M4A decoding requires an additional codec backend; those files currently show the normal unsupported-format warning on import.

Contact is available in **File > About & contact** on Windows and **More > About & contact** on Android: QQ 2224248204.

The spatial renderer is a real-time speaker-object panner. On stereo devices, it uses deterministic pseudo-binaural interaural delay and head-shadow filtering, with rear/elevation spectral cues; without a measured HRTF/SOFA dataset, stereo still cannot guarantee reliable front/rear or vertical localisation. On 4.0, 5.0, 5.1, 6.1, 7.1, 5.1.4, and 7.1.4 output configurations, object position is distributed across the corresponding speaker field. Distance attenuates level, elevation selects height speakers when available, orbit speed supports -360 to +360 degrees per timeline second, directivity narrows the speaker distribution, and spread retains controlled source width in stereo. The range tool creates local spatial and relative-gain overrides; independent DSP paths are crossfaded sample by sample with a smoothstep envelope while sharing the timeline orbit phase, so the base 3D render remains continuous outside each region. The canvas displays the active device layout, current orbit phase, speed, and seconds per revolution. Orbit follows timeline time, so changing playback speed changes its wall-clock rotation rate. Offline WAV export shares this renderer and writes the selected speaker layout without changing live playback state. The current engine decodes imported audio into memory and uses linear resampling for playback speed, so speed changes also change pitch. Pitch-lock controls, recording, effects, pitch-preserving time stretch, Doppler processing, listener head tracking, and measured HRTF rendering still require dedicated DSP backends.

## Build on Windows

Requirements:

- Visual Studio 2022 or newer with Desktop development with C++
- CMake 3.22 or newer
- JUCE 8.0.13 at `external/JUCE`

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

The executable is generated under `build/OiMusicEditor_artefacts/Release/`.

## Android phone and tablet build

The Android Studio project is in `Builds/Android`. It targets Android 7.0/API 24
and newer, and produces `armeabi-v7a`, `arm64-v8a`, and `x86_64` libraries. The
same JUCE component tree is used on phones and tablets; the UI selects three
responsive modes at runtime:

- phone: less than 600 dp, with touch-sized controls and drawer panels;
- compact tablet: 600-1023 dp, with condensed multi-column panels;
- full: 1024 dp and wider, with the desktop-style workspace.

Open `Builds/Android` in Android Studio, or build from PowerShell:

```powershell
cd Builds/Android
.\gradlew.bat assembleDebug --offline --no-daemon --console=plain
```

The debug APK is written to `Builds/Android/app/build/outputs/apk/debug/app-debug.apk`.
Use `adb install -r` with a connected phone or tablet to install it. The app uses
the Android system file picker for explicit audio imports; it does not scan a
fixed drive or require access to `E:\`.

JUCE's `juceaide` is a host executable and cannot be built with the Android NDK.
The CMake file automatically reuses the helper from the normal Windows desktop
build (`build/external/JUCE/tools/.../juceaide_artefacts/Custom`). If that build
tree is elsewhere, pass its containing directory explicitly:

```powershell
.\gradlew.bat assembleDebug --offline --no-daemon --console=plain `
  -PoiJuceHostToolsDir=C:/path/to/juceaide_artefacts/Custom
```

Alternatively, configure/build the desktop target once so JUCE generates the
host helper, then rebuild Android. `ANDROID_HOME` or `ANDROID_SDK_ROOT` should
point to the installed SDK. For an offline SDK install, pass
`-Pandroid.aapt2FromMavenOverride=C:/path/to/Android-SDK/build-tools/<version>/aapt2.exe`
to Gradle. Do not commit a machine-specific SDK path.

## Tests

The test suite covers update-version comparison, multitrack summing, mute, pan, splitting, cross-track move, duplication, deletion, shortened-timeline playhead clamping, undo/redo, playback-rate advancement, region spatial/volume automation, boundary continuity, deterministic spatial rendering across stereo, 5.1 surround, and 5.1.4 height layouts, plus 24-bit stereo/5.1 WAV export metadata, duration, samples, routing, automation preservation, and live transport isolation.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Dependency license

JUCE is dual-licensed. Confirm the appropriate JUCE or AGPLv3 terms before distributing binaries.
