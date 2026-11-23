@echo off
rem Build script for FT2 clone v2.28 for Windows with LLVM MinGW and VST3 support

set "MINGW_PATH=C:/Users/tpitk/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/llvm-mingw-20260602-ucrt-x86_64/bin"

set "CC=%MINGW_PATH%/x86_64-w64-mingw32-gcc.exe"
set "CXX=%MINGW_PATH%/x86_64-w64-mingw32-g++.exe"
set "RC=%MINGW_PATH%/x86_64-w64-mingw32-windres.exe"

if not exist obj mkdir obj
if not exist release\win32 mkdir release\win32

echo Building FT2 clone v2.28 for Windows with LLVM MinGW (VST3 support)...

rem Compile C source files from src/
for %%f in (
  src\ft2_about.c
  src\ft2_audio.c
  src\ft2_audioselector.c
  src\ft2_bmp.c
  src\ft2_checkboxes.c
  src\ft2_config.c
  src\ft2_diskop.c
  src\ft2_edit.c
  src\ft2_events.c
  src\ft2_gui.c
  src\ft2_help.c
  src\ft2_hpc.c
  src\ft2_inst_ed.c
  src\ft2_keyboard.c
  src\ft2_main.c
  src\ft2_midi.c
  src\ft2_module_loader.c
  src\ft2_module_saver.c
  src\ft2_mouse.c
  src\ft2_nibbles.c
  src\ft2_palette.c
  src\ft2_pattern_draw.c
  src\ft2_pattern_ed.c
  src\ft2_pushbuttons.c
  src\ft2_radiobuttons.c
  src\ft2_random.c
  src\ft2_replayer.c
  src\ft2_sample_ed.c
  src\ft2_sample_ed_features.c
  src\ft2_sample_loader.c
  src\ft2_sample_saver.c
  src\ft2_sampling.c
  src\ft2_scrollbars.c
  src\ft2_smpfx.c
  src\ft2_structs.c
  src\ft2_sysreqs.c
  src\ft2_tables.c
  src\ft2_textboxes.c
  src\ft2_trim.c
  src\ft2_unicode.c
  src\ft2_video.c
  src\ft2_wav_renderer.c
  src\gfxdata\ft2_bmp_fonts.c
  src\gfxdata\ft2_bmp_gui.c
  src\gfxdata\ft2_bmp_instr.c
  src\gfxdata\ft2_bmp_logo.c
  src\gfxdata\ft2_bmp_looppins.c
  src\gfxdata\ft2_bmp_midi.c
  src\gfxdata\ft2_bmp_mouse.c
  src\gfxdata\ft2_bmp_nibbles.c
  src\gfxdata\ft2_bmp_scopes.c
  src\mixer\ft2_mix.c
  src\mixer\ft2_windowed_sinc.c
  src\scopes\ft2_scopedraw.c
  src\scopes\ft2_scopes.c
  src\modloaders\ft2_load_bem.c
  src\modloaders\ft2_load_digi.c
  src\modloaders\ft2_load_it.c
  src\modloaders\ft2_load_mod.c
  src\modloaders\ft2_load_s3m.c
  src\modloaders\ft2_load_stk.c
  src\modloaders\ft2_load_stm.c
  src\modloaders\ft2_load_xm.c
  src\smploaders\ft2_load_aiff.c
  src\smploaders\ft2_load_brr.c
  src\smploaders\ft2_load_flac.c
  src\smploaders\ft2_load_iff.c
  src\smploaders\ft2_load_mp3.c
  src\smploaders\ft2_load_ogg.c
  src\smploaders\ft2_load_raw.c
  src\smploaders\ft2_load_wav.c
) do (
  if "%%~xf"==".c" (
    echo Compiling %%~f...
    %CC% -c "%%f" -o "obj\%%~nf.obj" -I. -Ithird_party/vst3sdk -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include -DNDEBUG -DHAVE_M_PI -DHAS_MIDI -D__WINDOWS_MM__ -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DSDL_MAIN_HANDLED -D_UCRT -O2 -fcommon -Wno-implicit-function-declaration
  )
)

rem Compile VST3 host (Windows only)
echo Compiling ft2_vst3.cpp...
%CXX% -c src\ft2_vst3.cpp -o obj\ft2_vst3.obj -I. -isystem third_party/vst3sdk -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include -DNDEBUG -DHAVE_M_PI -DHAS_MIDI -D__WINDOWS_MM__ -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DSDL_MAIN_HANDLED -D_UCRT -O2 -fcommon -fexceptions -Wno-implicit-function-declaration

rem Compile VST2 host (Windows only)
echo Compiling ft2_vst2.cpp...
%CXX% -c src\ft2_vst2.cpp -o obj\ft2_vst2.obj -I. -Ithird_party/vst3sdk -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include -DNDEBUG -DHAVE_M_PI -DHAS_MIDI -D__WINDOWS_MM__ -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DSDL_MAIN_HANDLED -D_UCRT -O2 -fcommon -fexceptions -Wno-implicit-function-declaration

rem Compile RtMidi
echo Compiling RtMidi...
%CXX% -c src\rtmidi\RtMidi.cpp -o obj\RtMidi.obj -I. -Ithird_party/vst3sdk -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include -DNDEBUG -DHAVE_M_PI -DHAS_MIDI -D__WINDOWS_MM__ -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DSDL_MAIN_HANDLED -D_UCRT -O2 -fcommon

rem Compile rtmidi_c
echo Compiling rtmidi_c.cpp...
%CXX% -c src\rtmidi\rtmidi_c.cpp -o obj\rtmidi_c.obj -I. -Ithird_party/vst3sdk -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include -DNDEBUG -DHAVE_M_PI -DHAS_MIDI -D__WINDOWS_MM__ -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DSDL_MAIN_HANDLED -D_UCRT -O2 -fcommon

rem Compile resource
echo Compiling resource...
%RC% -I. -I..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\include src\ft2-clone.rc -O coff -o obj\ft2-clone.res 2>nul

rem Link
echo Linking...
%CXX% -o release\win32\ft2-clone-win32.exe ^
  obj\ft2_about.obj obj\ft2_vst3.obj obj\ft2_vst2.obj obj\ft2_audio.obj obj\ft2_audioselector.obj ^
  obj\ft2_bmp.obj obj\ft2_checkboxes.obj obj\ft2_config.obj obj\ft2_diskop.obj ^
  obj\ft2_edit.obj obj\ft2_events.obj obj\ft2_gui.obj obj\ft2_help.obj ^
  obj\ft2_hpc.obj obj\ft2_inst_ed.obj obj\ft2_keyboard.obj obj\ft2_main.obj ^
  obj\ft2_midi.obj obj\ft2_module_loader.obj obj\ft2_module_saver.obj obj\ft2_mouse.obj obj\ft2_nibbles.obj obj\ft2_palette.obj ^
  obj\ft2_pattern_draw.obj obj\ft2_pattern_ed.obj obj\ft2_pushbuttons.obj ^
  obj\ft2_radiobuttons.obj obj\ft2_random.obj obj\ft2_replayer.obj ^
  obj\ft2_sample_ed.obj obj\ft2_sample_ed_features.obj obj\ft2_sample_loader.obj ^
  obj\ft2_sample_saver.obj obj\ft2_sampling.obj obj\ft2_scrollbars.obj ^
  obj\ft2_smpfx.obj obj\ft2_structs.obj obj\ft2_sysreqs.obj obj\ft2_tables.obj ^
  obj\ft2_textboxes.obj obj\ft2_trim.obj obj\ft2_unicode.obj obj\ft2_video.obj ^
  obj\ft2_wav_renderer.obj ^
  obj\ft2_bmp_fonts.obj obj\ft2_bmp_gui.obj obj\ft2_bmp_instr.obj obj\ft2_bmp_logo.obj ^
  obj\ft2_bmp_looppins.obj obj\ft2_bmp_midi.obj obj\ft2_bmp_mouse.obj obj\ft2_bmp_nibbles.obj ^
  obj\ft2_bmp_scopes.obj ^
  obj\ft2_mix.obj obj\ft2_windowed_sinc.obj ^
  obj\ft2_scopedraw.obj obj\ft2_scopes.obj ^
  obj\ft2_load_bem.obj obj\ft2_load_digi.obj obj\ft2_load_it.obj obj\ft2_load_mod.obj ^
  obj\ft2_load_s3m.obj obj\ft2_load_stk.obj obj\ft2_load_stm.obj obj\ft2_load_xm.obj ^
  obj\ft2_load_aiff.obj obj\ft2_load_brr.obj obj\ft2_load_flac.obj obj\ft2_load_iff.obj ^
  obj\ft2_load_mp3.obj obj\ft2_load_ogg.obj obj\ft2_load_raw.obj obj\ft2_load_wav.obj ^
  obj\RtMidi.obj obj\rtmidi_c.obj obj\ft2-clone.res ^
  -L..\..\ft2-clone-1.09_fix\vs2019_project\ft2-clone\sdl\lib64 ^
  -static -lmingw32 -lSDL2 -mwindows -lucrt -lwinmm -limm32 -lversion -lsetupapi -lole32 -loleaut32 -luser32 -lgdi32 -lshlwapi -lm

echo Build complete! Executable: release\win32\ft2-clone-win32.exe
pause
