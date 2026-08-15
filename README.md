# 0i-Studio

**English** | [简体中文](README.zh-CN.md)

Native C++20/JUCE audio editor prototype for Windows and Android. The current
build focuses on multitrack clip editing, adjustable spatial-audio experiments,
playback controls, and WAV export.

Current release: **0.1.11** (`versionCode 12` on Android).

## Current features

- Edit, Spatial, and Mix workspace views
- Dark/light themes and runtime Chinese/English switching
- Adjustable browser, inspector, track, and mixer layout dimensions
- One empty starting track, with user-added, nameable, and renameable tracks (up to 12), waveform thumbnails, and clip metadata
- Explicit multi-file/folder media import, media-browser drag-and-drop to any track, time-ruler seeking, clip selection, splitting, drag/cross-track move, duplication, deletion, undo, and redo
- Same-track clip-edge snapping with a visible guide and temporary `Alt` bypass
- Timeline navigation: on touch screens, drag the ruler or empty lane with one finger to pan and pinch with two fingers to zoom; on desktop, `Ctrl+wheel` zooms, `Alt+wheel` scrolls horizontally, and `Space` toggles play/pause
- The timeline header `+` button opens the same named-track dialog as the Track menu
- Basic multitrack mixing controls for per-track gain, pan, mute, solo, and meters
- Clip gain, master gain, transport seek/loop, and variable-speed playback
- Experimental per-track spatial controls and rendering for enable/bypass, radius, azimuth, elevation, orbit speed, spread, directivity, attenuation curves, and optional air absorption
- Clip-range spatial and relative-volume adjustments with configurable entry/exit transitions and timeline envelopes; on landscape phones, the range tool remains in the fixed top toolbar instead of covering the waveform
- Mobile parameter values are display-only and parameter rows do not accept text focus, so touching labels, values, or sliders cannot open the soft keyboard
- PCM WAV export options for 44.1/48/88.2/96 kHz, 16/24/32-bit, and Stereo, 5.1, 7.1, 5.1.4, or 7.1.4 layouts
- Chinese/English GitHub Release update checks on Windows and Android, with in-app APK download and package validation on Android

### Online updates

Both clients check the repository's latest GitHub Release shortly after startup.
The check runs on a background thread and stays silent when the installed version
is current or the network is unavailable. **File > Check for updates** on Windows,
or **More > Check for updates** on Android, performs a manual check with feedback.

Updates are user-confirmed: Windows opens the published `.exe` download. Android
downloads the `.apk` into private app storage, verifies its release digest, package,
version, and signing certificate, then opens the system installer. Android may ask
the user to allow 0i-Studio to install unknown apps; silent installation is not
permitted by the platform. Release assets must use these names so each client
selects its own package:

- `0i-Studio-<version>-windows-x64.exe`
- `0i-Studio-<version>-android.apk`

### Menu and media browser

The menus currently expose these actions:

- **File** imports audio, changes the media folder, opens export/audio-device settings, checks for updates, and shows contact information.
- **Edit** provides undo/redo, clip duplication/deletion, playhead splitting, and select/range/split tools.
- **Track** imports to the selected track, toggles mute/solo/3D rendering, resets track mix, and selects a track.
- **Clip** splits, duplicates, deletes, resets clip gain, removes a selected spatial region, and moves a clip to another track.
- **View** switches Edit/Spatial/Mix workspaces, toggles panels, opens layout controls, and changes theme/language.

The left media browser has Project, Local, and Spatial Presets pages. It starts empty and only lists files or folders explicitly imported by the user; it never scans a fixed drive. Imported media can be dragged directly to a chosen track or double-clicked to add it at the playhead. Project items locate clips in the timeline, while presets apply a saved spatial parameter set to the selected track or spatial region. Search and refresh apply to the active page.

The current JUCE reader can import WAV, AIFF, FLAC, OGG, and MP3. M4A files can be listed by the browser, but AAC/M4A decoding requires an additional codec backend; those files currently show the normal unsupported-format warning on import.

Contact is available in **File > About & contact** on Windows and **More > About & contact** on Android: QQ 2224248204.

The spatial-audio implementation is experimental. It is a speaker-object panner,
not a measured binaural engine. On stereo devices it uses simplified interaural
delay, head-shadow filtering, and rear/elevation spectral cues. These cues may
create a sense of movement, but without a measured HRTF/SOFA dataset they cannot
guarantee front/rear or vertical localisation. On 4.0, 5.0, 5.1, 6.1, 7.1,
5.1.4, and 7.1.4 outputs, the panner distributes the source across the available
speaker field. Distance, elevation, orbit speed, directivity, and spread adjust
that distribution. The range tool applies local spatial and relative-gain values,
with a smoothstep crossfade between DSP paths. Orbit follows timeline time, so
playback speed also changes the wall-clock rotation rate. WAV export reuses the
same renderer. Playback speed currently uses linear resampling and therefore also
changes pitch. Pitch locking, recording, effects, pitch-preserving time stretch,
Doppler processing, head tracking, and measured HRTF rendering are not implemented.

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
