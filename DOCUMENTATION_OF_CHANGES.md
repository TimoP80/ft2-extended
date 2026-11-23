# Documentation of Changes vs. Upstream FT2-Clone 2.28

**Baseline:** upstream `ft2-clone-2.28` (as shipped in `ft2-clone-2.28.zip`).
**This tree:** `ft2-clone-2.28/` in this workspace (Windows-focused fork).
**Method:** `diff -u` of every file against a fresh unzip of `ft2-clone-2.28.zip`
(`E:/FT2WIN64/ft2-clone-2.28.zip`). Build-output directories (`build/`, `obj/`,
`third_party/vst3sdk/build`, `*.obj`, `*.tlog`, built `release/win32|win64/*.exe`)
were excluded. Timestamps were also used as a cross-check: unmodified upstream
files still carry `2026-10-29 19:44`, modified/added files carry `2026-11-05` … `2026-11-18`.

> Scope note: upstream README behavior (“XM player ported from original FT2”,
> improvements list, etc.) is unchanged. All changes below are additive to that
> baseline; no upstream feature was removed.

---

## 0. Change summary

| Area | What changed |
|---|---|
| Plug-in hosting (major) | New VST3 + VST2 hosts, per-instrument VST instruments, 4-slot master VST effect chain, editors, scanning, caching, `.xmv` song chunk. Windows-only; stub on other OSes. |
| True stereo samples (major) | `SAMPLE_STEREO` is now honored end-to-end instead of being downmixed: interleaved L/R storage, stereo mixer path, stereo waveform, stereo-aware fix/convert/delta, stereo sample loaders with “Keep stereo”. |
| Song format | New `.xmv` = plain `.xm` + trailing `FT2VSTXM` chunk with plug-in refs + presets. Disk Op save-mode, loader, saver, `xmv` extension registration. |
| Sample editor feature | New “Loop tempo” / “Loop beats (song tempo)” time-stretch tool (`Loop Stretch` box). |
| Config/UI | New `CONFIG_SCREEN_VST3`, VST3 scan list + progress + skipped-report + master-FX panel, instrument-editor VST assignment popup, new pushbuttons/radiobuttons/scrollbars, mouse-wheel/click routing, keyboard shortcut remap. Scopes draw live VST output. WAV renderer solos per-channel VST. |
| Build system | `CMakeLists.txt` conditional VST sources, new `make-windows.bat` (LLVM-MinGW), `vs2026_project` updated for `third_party/vst3sdk`, `NOMINMAX`, new files. New `third_party/vst3sdk/` checkout. |
| Portability/small fixes | `SMP_DAT_OFFSET`/`SAMPLE_PAD_LENGTH` doubled for interleaved taps, `PATH_MAX` guard, `--ft2-vst3-scan-worker` early CLI path, Ctrl+F11/F12 shortcut reshuffle, stereo sysreq “Keep stereo” option. |
| Sample/mod loaders | S3M stereo planar→interleaved fix, XM stereo-length fix (no more flag strip), WAV/AIFF/FLAC/MP3/OGG keep-stereo paths, `STEREO_SAMPLE_KEEP_STEREO=4`. |

Files changed (35): `CMakeLists.txt`, `make-windows.bat` (new), `vs2026_project/...vcxproj{,.filters}`,
`src/ft2_header.h`, `ft2_main.c`, `ft2_config.{c,h}`, `ft2_audio.{c,h}`, `ft2_replayer.{c,h}`,
`mixer/ft2_mix.{c,h}`, `ft2_diskop.{c,h}`, `ft2_module_loader.c`, `ft2_module_saver.{c,h}`,
`ft2_inst_ed.{c,h}`, `ft2_sample_ed.{c,h}`, `ft2_sample_ed_features.c`,
`ft2_sample_loader.h`, `ft2_trim.c`, `ft2_video.c`, `ft2_keyboard.c`, `ft2_mouse.c`,
`ft2_pushbuttons.{c,h}`, `ft2_radiobuttons.{c,h}`, `ft2_scrollbars.{c,h}`,
`ft2_sysreqs.c`, `ft2_wav_renderer.c`, `scopes/ft2_scopes.{c,h}`,
`modloaders/ft2_load_{xm,s3m}.c`, `smploaders/ft2_load_{wav,aiff,flac,mp3,ogg}.c`.

Files added (7): `src/ft2_vst3.{cpp,h}` (~3362/169 lines), `src/ft2_vst2.{cpp,h}`
(~990/421 lines), `src/ft2_plugin_instance.h` (80 lines), `src/ft2_vst3_stub.{c,cpp}`,
`third_party/vst3sdk/` (full SDK), `make-windows.bat`, `release/win32/tmp/`,
`ft2_vst3_latest.zip` artifacts.

---

## 1. Build system and dependencies

### 1.1 `CMakeLists.txt`
```cmake
if(WIN32)
  list(APPEND ft2-clone_SRC ".../src/ft2_vst3.cpp")
  list(APPEND ft2-clone_SRC ".../src/ft2_vst2.cpp")
else()
  list(APPEND ft2-clone_SRC ".../src/ft2_vst3_stub.c")
endif()
```
Windows builds compile both plug-in hosts; non-Windows builds compile only the
no-op `ft2_vst3_stub.c` so all `vst3*()` calls still link.

### 1.2 `make-windows.bat` (new, ~6400 bytes)
Standalone LLVM-MinGW build script (hard-coded `MINGW_PATH` to a WinGet
LLVM-MinGW install). It compiles every `src/*.c`, `gfxdata`, `mixer`, `scopes`,
`modloaders`, `smploaders`, plus `ft2_vst3.cpp` / `ft2_vst2.cpp` (`-fexceptions`,
`-isystem third_party/vst3sdk`), `RtMidi.cpp` / `rtmidi_c.cpp`, `windres`
resource, then links statically (`-static -lmingw32 -lSDL2 -mwindows ... -lwinmm
-limm32 ...`) to `release/win32/ft2-clone-win32.exe`. Note: include paths still
point at `../../ft2-clone-1.09_fix/vs2019_project/.../sdl`, i.e. machine-specific.

### 1.3 `vs2026_project/ft2-clone/ft2-clone.vcxproj{,.filters}`
- `IncludePath` += `../../third_party/vst3sdk` (all 4 configs).
- `PreprocessorDefinitions` += `NOMINMAX` (all configs; required by VST3 SDK + `<windows.h>`).
- New compile items: `src/ft2_vst2.cpp`, `src/ft2_vst3.cpp`.
- New header items: `src/ft2_vst2.h`, `src/ft2_vst3.h`, `src/ft2_plugin_instance.h`.

### 1.4 `third_party/vst3sdk/` (new)
Full Steinberg VST3 SDK checkout (`base/`, `pluginterfaces/`, `public.sdk/`,
`vstgui4/`, `cmake/`, etc.). Only headers + linked sources are used by
`ft2_vst3.cpp`; the directory also contains CMake build leftovers
(`CMakeFiles/`, `build/`) that are not part of the source change.

---

## 2. New plug-in hosting code

### 2.1 `src/ft2_plugin_instance.h` (new)
Format-agnostic C++ interface implemented by both hosts so tracker code does not
branch on VST2 vs VST3:

- `PluginHostTransport { playing, sampleRate, tempo, projectTimeSamples, projectTimeMusic }`
- `PluginLoadRequest { name, vendor, classID, modulePath, kind, effectMode, sampleRate }`
  (`classID` = VST3 class UID hex or VST2 unique-ID hex).
- `PluginHostInstance` virtuals: `initialize`, `setSampleRate`, `noteOn/noteOff/allNotesOff`,
  `process` (instruments, with scope ring buffer), `processEffect` (in-place L/R),
  `captureState/applyState`, `openEditor/closeEditor`, plus `latencySamples`,
  `outputChannelCount`, `renderedLeft()/renderedRight()`, `lastError()`.
- Constants: `PLUGIN_SCOPE_BUFFER_SIZE=2048`, `MAX_PLUGIN_LATENCY_SAMPLES=8192`,
  `MAX_WAV_RENDER_SAMPLES_PER_TICK=4096` fallback.

### 2.2 `src/ft2_vst3.h` (public C API used by the C tracker)
- Plug-in taxonomy: `VST3_PLUGIN_{EFFECT,INSTRUMENT,OTHER}`,
  `VST3_PLUGIN_FORMAT_{VST3,VST2}`; `vst3PluginInfo_t` (name/vendor/category/
  subCategories/version/classID/modulePath/kind/format); scan progress
  `vst3ScanProgress_t`; skipped-report `vst3SkippedPlugin_t` (max 256).
- Cache: `vst3{AddSearchPath,ScanDirectory,Scan,GetPluginCount/GetPluginInfo,Write/ReadPluginCache}`,
  instrument/effect filtered counts/names/labels (`"VST2  Name"` label prefix).
- Instruments: `vst3{GetAssignedInstrument,AssignInstrument,OpenInstrumentEditor}`,
  `vst3InstrumentNoteOn(note→noteId)/NoteOff/AllNotesOff`, `vst3ProcessInstruments`,
  scope taps `vst3GetScopeSamples/GetScopePeak`, solo filter `vst3SetRenderSoloInstrument`
  (`VST3_RENDER_ALL=-1`, `VST3_RENDER_NONE=-2`), `vst3SetSampleRate`.
- Master FX: `MAX_VST3_MASTER_EFFECTS=4`, `vst3{ProcessEffects,GetMasterEffectCount,
  GetMasterEffectInfo,Add/RemoveMasterEffect,SetMasterEffectBypass,OpenMasterEffectEditor}`.
- Song chunk: `VST3_XM_CHUNK_MAGIC="FT2VSTXM"`, `VERSION=1`, packed `vst3SongRef_t
  { instrument, stateSize, name[128], vendor[128], classID[64], modulePath[512] }`;
  `vst3{Write,Read,Apply,Discard}SongVstChunk()`.
- Scan infra: `vst3{GetScanProgress,ResetScanProgress,GetSkippedPluginCount/Info,
  GetLastScanError/GetLastLoadError}`, `vst3ScannerWorkerMain()`, `vst3Cleanup()`.

### 2.3 `src/ft2_vst3.cpp` (~3362 lines, `_WIN32` only)
The orchestrator. Key design points (read from source):

- Hosts both formats through `PluginHostInstance`; VST2 instances are created via
  `ft2_vst2.h` when `info.format==VST3_PLUGIN_FORMAT_VST2`.
- Up to `MAX_VST3_INSTRUMENTS=131` instrument slots (mirrors FT2’s 1..130 + 0=none);
  per-slot `assignedInstrumentPlugins`, `instrumentInstances`, stereo delay lines for
  latency compensation, 2048-sample scope ring buffers.
- Process-global unique `nextNoteId` so a stale note-off can never kill a new instance’s
  note after reassignment.
- Latency model: dry mix delayed by longest plug-in latency; each instrument delayed by
  the difference (fixed stereo delay lines, capped at 8192).
- Scanning is crash-isolated: main process spawns `--ft2-vst3-scan-worker` children
  (`FT2CLONE_VST3_SCAN_MODULE/RESULT` env vars), one module per worker; VST3 bundles
  (64-bit only) + plain VST2 DLLs (incl. `HKLM/HKCU\SOFTWARE\VST` registry + extra paths).
  Uninstantiable classes go to the skipped-report, not the list.
- Transport → VST3 process context (playing/tempo/sampleRate/project time); sample-rate
  changes propagated while audio is paused; WAV render uses solo filter + per-track passes.

### 2.4 `src/ft2_vst2.{cpp,h}` (~990 + 421 lines, `_WIN32` only)
Self-contained VST 2.4 host (SDK not redistributable, so `aeffect.h` ABI is
re-declared: `AEffect`, `ERect`, `VstEvents/MidiEvent/TimeInfo`, opcodes, flags,
categories). Features: `vst2EnumerateModule()` (safe open/close scan incl. shell DLLs
via `effShellGetNextPlugin`), `Vst2Instance : PluginHostInstance` (MIDI queue ×256,
`processReplacing`, chunk/bank state, latency via `effGetTailSize`/I/O props,
HWND editor with idle timer `FT2CloneVST2EditorWindow`, UTF-8 path handling).

### 2.5 `src/ft2_vst3_stub.{c,cpp}` (new)
No-op implementations of the whole `ft2_vst3.h` API for non-Windows builds
(all return false/0; `WritePluginCache` returns true so config save is not an error;
`WriteSongVstChunk` still writes a valid empty `FT2VSTXM` header with count=0).

---

## 3. Core audio / mixer / replayer integration

### 3.1 `src/ft2_header.h`
- `#include "ft2_vst3.h"` on `_WIN32`.
- Interpolation tap padding doubled for interleaved stereo:
  `SMP_DAT_OFFSET ((MAX_LEFT_TAPS*2*2)+1)`, `SAMPLE_PAD_LENGTH (...+MAX_RIGHT_TAPS*2*2)`.
- `PATH_MAX` now guarded by `#ifndef PATH_MAX` (toolchain already defines it).

### 3.2 `src/ft2_main.c`
- First thing in `main()`: `--ft2-vst3-scan-worker` → `vst3ScannerWorkerMain(); return 0;`
  before SDL/config/window/single-instance setup so workers never touch user state.
- Shutdown: `vst3Cleanup()` before `closeAudio()/closeReplayer()/closeVideo()` so editors
  and message loop still exist while plug-ins unload.

### 3.3 `src/ft2_audio.{c,h}`
- `voice_t` += `isStereo`, `is16Bit` (interleaved L/R frames).
- `setVoiceSamplePosition()`: `revBase`/`leftEdgeTaps` scaled by `channelStride` (2 if stereo);
  stores flags on the voice.
- `doChannelMixing()`: stereo voices bypass `mixFuncTab` → `mixStereoVoice()` (both normal
  and fadeout voices).
- `mixReplayerTickToBuffer()` + audio-callback inner loop: after `doChannelMixing()` call
  `vst3ProcessInstruments()` then `vst3ProcessEffects()` on `fMixBufferL/R`.
- `setAudioFreq()` / `setBackOldAudioFreq()`: `vst3SetSampleRate()` while callback paused.

### 3.4 `src/mixer/ft2_mix.{c,h}` (+415 lines in `.c`)
New `mixStereoVoice()` + `stReadTap/stSincTap/stRenderSample` helpers and
`mixStereoVoiceNoLoop/Loop/PingPong` variants. Each L/R channel mixed to its own side;
runtime `audio.interpolationType` switch covers disabled/linear/quadratic/cubic/sinc8/sinc16.
Declared in `ft2_mix.h`.

### 3.5 `src/ft2_replayer.{c,h}`
- `sample_t.leftEdgeTapSamples8/16`: `MAX_TAPS*2` → `MAX_TAPS*3` (interleaved taps fit).
- `channel_t` += `vst3NoteId/vst3InstrNum/vst3NoteNum` (note-off routed to the plug-in+id
  that started it even if the channel changed instrument).
- New `vst3KeyOffChannel()` / `vst3NoteOnChannel()`; hooked into `resetSong()`
  (release before `memset`), `keyOff()`, `playSample()` (implicit cut + note-on before
  early sample-missing exits; `note>96` clamp moved earlier so VST still fires),
  `E8h` note-cut and `ECx` note-cut paths.
- `SAMPLE_LENGTH_BYTES` redefined via `SAMPLE_NUM_CHANNELS` / `SAMPLE_LENGTH_SAMPLES`
  (length/loopStart/loopLength are now *frame* counts; one frame = L+R).
- `delta2Samp()` / `samp2Delta()` rewritten for interleaved stereo (per-channel delta
  chains, `i+=2` stepping); old “downmix to mono” branches removed (see also `ft2_load_xm`).
- New `stereoPlanarToInterleaved()` helper (used by S3M loader).

---

## 4. Configuration persistence

`src/ft2_config.{c,h}` (+843 lines in `.c`): the `CONFIG_SCREEN_VST3` screen and the
plug-in cache appended to `FT2.CFG`.

- Loader tolerant of `1732 .. CONFIG_FILE_SIZE + 64 MiB`; reads first
  `min(fileSize, CONFIG_FILE_SIZE)` bytes as config, then `vst3ReadPluginCache(f, rest)`.
  Saver writes config then `vst3WritePluginCache(f)`; cache-write failure is a real save error.
- New UI state: `vst3ScanState/Succeeded/CompletionDrawn`, progress throttle,
  plugin/skipped selection, `vst3FxPanelShown/Selected/AvailableScroll`, geometry
  `VST3_LIST_*`, `CONFIG_VST3_CACHE_MAX_SIZE=64MiB`.
- New functions (see `ft2_config.h`): `rbConfigVST3`, `configScanVST3Plugins`,
  `drawVST3PluginList`, `configToggleVST3SkippedReport`, `updateVST3ScanWindow`,
  `scrollVST3PluginListUp/Down`, `sbVST3PluginSetPos`, `testVST3PluginListMouseDown`,
  `configShowVST3FxPanel`, `configVST3FxEditor/Bypass/Remove/Close`,
  `testVST3FxPanelMouseDown`, `vst3FxPanelMouseWheel`.

---

## 5. User-interface changes

- **`ft2_inst_ed.{c,h}` (+~324 lines):** extended-instrument editor gains VST assignment
  row + 7-row popup list (`VST3_INST_LIST_*`, entry 0 = “(Off)”), scroll clamp, highlight of
  `vst3GetAssignedInstrument(curInstr)`, `drawInstEditorExt()` plugin-name field; popup
  closed on instrument change; hit-tests `testVST3Instrument{Selector,Editor,List}MouseDown()`
  + `vst3InstrumentListMouseWheel()`.
- **`ft2_config.c` rendering (see §4):** VST3 list with scrollbar, `Re-scan` / `Master FX...` /
  `Skipped...` buttons, progress window, error box, skipped-report + details, 4-slot FX panel
  (`Editor/Bypass/Remove/Close` per slot).
- **`ft2_pushbuttons.{c,h}`:** new `PB_SAMP_EXT_LOOP_STRETCH` (“Loop tempo”) and 7
  `PB_CONFIG_VST3_*` buttons (`Re-scan`, `Master FX...`, `Skipped...`, FX `Editor/Bypass/Remove/Close`).
- **`ft2_radiobuttons.{c,h}`:** `RB_CONFIG_VST3` (config column shifted: layout 34→33,
  misc 50→48, VST3 at 63, MIDI 66→78); `RB_DISKOP_MOD_SAVEAS_XMV` (MOD/XM/XMV/WAV list).
- **`ft2_scrollbars.{c,h}`:** `SB_VST3_PLUGIN_SCROLL` (614,18,18×116, dynamic thumb, page 10).
- **`ft2_mouse.c`:** wheel routes to VST3 list / FX panel on config screen and to
  `vst3InstrumentListMouseWheel()` in instrument editor; left-click order inserts
  `testVST3FxPanelMouseDown()` then instrument VST tests before text/push/checkbox
  handling, plus `testVST3PluginListMouseDown()` after scopes.
- **`ft2_keyboard.c`:** `Ctrl+F11` now opens `CONFIG_SCREEN_VST3`/`RB_CONFIG_VST3`
  (was MIDI); `Ctrl+F12` opens MIDI input (was unhandled there).
- **`ft2_video.c`:** per-frame `updateVST3ScanWindow()` while config screen shown.
- **`scopes/ft2_scopes.{c,h}` (+120 lines):** `scope_t` gains VST fields
  (`vst3/vst3Instrument/vst3SilentFrames`), `vst3ScopeOwner[]`; silent (>16 frames under
  `GetScopePeak()<0.0002`) blanks scope; new `drawVST3Scope()` min/max envelope from
  1024-sample live capture.
- **`ft2_diskop.{c,h}`:** `MOD_SAVE_MODE_XMV=2` (WAV shifts 2→3), `.xmv` extension handling,
  `rbDiskOpModSaveXmv()`, save-list rows MOD/XM/XMV/WAV.
- **`ft2_sysreqs.c`:** stereo-loader dialog gains 4th button “Keep stereo” (`SDLK_k`).

---

## 6. Module and sample file handling

### 6.1 `.xmv` song format
- `ft2_module_saver.c`: `saveXM()` → `saveXMInternal(filename, saveVstChunk)`;
  stereo sample headers doubled (`length/loopStart/loopLength <<=1` covers both channels);
  delta uses `SAMPLE_LENGTH_SAMPLES`; `saveXMV()` appends `vst3WriteSongVstChunk()`;
  `saveMusic()` dispatches XMV vs XM/MOD/WAV.
- `modloaders/ft2_load_xm.c`: trailing bytes after XM data → `vst3ReadSongVstChunk()`;
  stereo samples kept (`length/loopStart/loopLength >>=1` to frames, `reallocateSmpData`
  dealloc removed, `SAMPLE_STEREO` flag no longer stripped).
- `ft2_module_loader.c`: `.xmv` added to `supportedModExtensions`; `vst3DiscardSongVstChunk()`
  on failure paths; `vst3ApplySongVstChunk()` on main thread after successful load;
  `argv[0]=='-'` guard so `--ft2-vst3-scan-worker` is never treated as a module.

### 6.2 Stereo-aware loaders
- `ft2_sample_loader.h`: `STEREO_SAMPLE_KEEP_STEREO=4`.
- `smploaders/ft2_load_{wav,aiff}.c` (all bit depths), `flac/mp3/ogg`: new keep-stereo
  branch sets `SAMPLE_STEREO` and preserves interleaved frames; otherwise old
  left/right/mix behavior unchanged; length caps adjusted (`MAX_SAMPLE_LEN*2` samples,
  even frame counts).
- `modloaders/ft2_load_s3m.c`: stereo penalty fixed — `length` treated as frame count,
  `allocate/fread/conv` use `length*channels`, then `stereoPlanarToInterleaved()` + set
  `SAMPLE_STEREO` instead of downmix+shrink.

---

## 7. Sample editor and DSP tools

`src/ft2_sample_ed.{c,h}` (+320 lines), `src/ft2_sample_ed_features.c` (+339 lines):

- Waveform: `STEREO_SAMPLE_HEIGHT/L/R_CENTER`; `getSampleDataPeak()` per-channel;
  `drawWaveformChannel()`; stereo draws L/R halves + own center lines.
- `fixStereoSample()` (interleaved forward/pingpong edge taps); `fixSample()` dispatches on flag.
- Copy/duplicate paths use `SAMPLE_LENGTH_{SAMPLES,BYTES}`; overwrite/cut/resize blocked for
  stereo with “Sorry, ...” boxes; new `stereoEditNotSupported()` guard wraps cut/copy/paste/
  trim/fade/volume/etc. (8 call sites).
- New **Loop tempo** tool (`PB_SAMP_EXT_LOOP_STRETCH` → `pbLoopStretchTempo()`):
  resamples whole sample so one loop cycle = N song ticks (1..16, scrollbar+arrows),
  using C4 rate (`getSampleC4Hz`) and `BPM*2/5` tick rate; `loopStretch_Beats`,
  `drawLoopStretchBox()` (current/target ticks), `loopStretchThread()` resampler,
  `pbDoLoopStretch()`, `setupLoopStretchBoxWidgets()` (Apply/Exit + beats controls).

Supporting: `ft2_trim.c` (`reallocateSmpData` with `SAMPLE_LENGTH_SAMPLES`),
`ft2_sample_ed.h` (`stereoEditNotSupported`, `pbLoopStretchTempo`).

---

## 8. WAV renderer, scopes, misc.

- `ft2_wav_renderer.c`: `vst3SetRenderSoloInstrument(VST3_RENDER_ALL)` at start,
  per-channel `VST3_RENDER_{channelInstr|NONE}` during soloed-track renders,
  reset to ALL at end (VST tails included per rendered channel only).
- `ft2_trim.c`: see §7 (2-line stereo-size fix).
- `ft2_mouse.c` / `ft2_keyboard.c` / `ft2_video.c`: see §5.
- Unchanged behavior verified by diff: `ft2_edit`, `ft2_events`, `ft2_gui`, `ft2_tables`,
  `ft2_textboxes`, `rtmidi`, `gfxdata`, `helpdata`, most `modloaders/smploaders` beyond
  files listed.

---

## 9. Platform notes and limitations

- VST hosting is **Windows-only** (`_WIN32`/`WIN32`, HWND editors, registry scan,
  `NOMINMAX`). Linux/macOS/CMake and cross builds get the stub: UI entry points still
  exist but scan/assign/process are no-ops and `.xmv` chunks are empty-but-valid.
- New runtime requirements on Windows: 64-bit VST3 bundles / VST2 DLLs, SSE (pre-existing
  check now runs after worker-arg check), SDL2, `third_party/vst3sdk` at build time.
- `make-windows.bat` paths are machine-specific; `release/win32/SDL2.dll` was also
  refreshed in this tree.
- Stereo editing is partial by design: cut/resize/overwrite/FX tools refuse stereo
  samples; playback/mixing/display/loading/saving/rendering fully support them.
- Config files remain backward compatible (old sizes still load, cache optional) but new
  files are larger (config + up-to-64 MiB cache); other trackers ignore the `.xmv` tail.
