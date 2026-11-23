# Fasttracker 2 Clone Extended
Fasttracker 2 Clone Extended for Windows/macOS/Linux

> **About this fork:** this is **Fasttracker 2 Clone Extended**, based on upstream
> [olavrv/ft2-clone](https://github.com/olavrv/ft2-clone) (merged up to v2.30).
> It adds VST instrument/effect hosting, true stereo samples, and a new `.xmv`
> song format on top of the accurate FT2 clone. See
> [DOCUMENTATION_OF_CHANGES.md](DOCUMENTATION_OF_CHANGES.md) for the full change list.

Aims to be a highly accurate clone of the classic Fasttracker II software for MS-DOS. \
The XM player itself has been directly ported from the original source code, for maximum accuracy. \
The code is partly my own, partly based on the original FT2 code.

*What is Fasttracker II? Read about it on [Wikipedia](https://en.wikipedia.org/wiki/FastTracker_2).*

# Releases
Windows/macOS binary releases can always be found at [16-bits.org](https://16-bits.org/ft2.php).

Linux binaries can be found [here](https://repology.org/project/fasttracker2/versions). \
If these don't work for you, you'll have to compile the code manually.

# New in this fork (Extended changes)
- **VST2/VST3 hosting (Windows):** per-instrument VST instruments plus a 4-slot master
  effect chain, with plugin editors, crash-isolated background scanner, plugin cache
  stored in `FT2.CFG`, and latency compensation. New `Config -> VST3` screen
  (`Ctrl+F11`), instrument-editor assignment popup, live scopes for VST output.
  Other platforms compile with a no-op stub so songs still load.
- **True stereo samples:** `SAMPLE_STEREO` is honored end-to-end (interleaved storage,
  stereo mixer path with all interpolations, stereo waveform, stereo-aware loaders with
  a "Keep stereo" option) instead of being downmixed to mono.
- **New `.xmv` song format:** a plain `.xm` with an appended `FT2VSTXM` chunk holding
  plugin references + state, so songs with VSTs save/load. Regular `.xm` files stay
  fully compatible.
- **Sample-editor "Loop tempo" tool:** time-stretch a loop so one cycle lasts N song ticks.
- **Build:** `CMakeLists.txt` (VST sources on Win32, stub elsewhere), `make-windows.bat`
  (LLVM-MinGW), VS2026 project updates, and the Steinberg VST3 SDK 3.8.1 as a
  `third_party/vst3sdk` submodule — clone with `git clone --recurse-submodules`.
- Full details: [DOCUMENTATION_OF_CHANGES.md](DOCUMENTATION_OF_CHANGES.md).

# Improvements over original DOS version
- New sample editor features, like waveform generators and resonant filters
- The channel resampler/mixer uses floating-point arithmetics for less errors, and has extra interpolation options (4-point cubic spline and 8-point/16-point windowed-sinc)
- The sample loader supports AIFF/FLAC/OGG/MP3/BRR (SNES) samples and more WAV types than original FT2. It will also attempt to tune the sample (finetune and rel. note) to its playback frequency on load.
- It contains a new "Trim" feature, which will remove unused stuff to potentially make the module smaller
- Drag n' drop of modules/samples
- The waveform display in the sample editor shows peak based data when zoomed out
- Textboxes have a text marking option, where you can cut/copy/paste
- MOD/STM/S3M import has been slightly improved (S3M import is still not ideal, as it's not compatible with XM)
- Supports loading DIGI Booster (non-Pro) modules
- Supports loading Impulse Tracker modules (Awful support! Don't use this for playback)
- It supports loading XMs with stereo samples, uneven amount of channels, more than 32 channels, more than 16 samples per instrument, more than 128 patterns etc. The unsupported data will be mixed to mono/truncated.
- It has some small additions to make life easier (C4/middle-C Hz display in Instr. Ed., envelope point coordinate display, etc).

# Screenshots

![Example #1](https://16-bits.org/ft2-clone-3.png)
![Example #2](https://16-bits.org/ft2-clone-4.png)
<img width="1903" height="1278" alt="Screenshot 2026-11-17 232216" src="https://github.com/user-attachments/assets/4f1415ab-335a-42d2-ba43-df8ec63820de" />



# Compiling the code
Build instructions can be found in the repository (HOW-TO-COMPILE.txt).

Keep in mind that the program may fail to compile on Linux, depending on your distribution and GCC version. \
Please don't nag me about it, and try to use the Linux packages linked to from [16-bits.org](https://16-bits.org/ft2.php) instead.

PS: The source code is quite hackish and hardcoded. \
My first priority is to make an accurate clone, and not to make flexible and easily modifiable code.

Big parts of the code (except GUI) are directly ported from the original FT2 source code, with permission to use a BSD 3-Clause license.
