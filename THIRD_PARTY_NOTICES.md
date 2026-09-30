# Third-party provenance

No project-wide license is assigned. Source visibility does not change the
rights or license terms of upstream code, artwork, sounds or bundled libraries.

## Original sounds, explicitly requested by the user

`assets/HitSounds/don.wav` and `kat.wav` are byte-for-byte downloads from
[4dblackhole/Taiko-Key-ASIO](https://github.com/4dblackhole/Taiko-Key-ASIO/tree/c3ed9a669e3369dd149171fba8fc06e84589d316/HitSounds),
commit `c3ed9a669e3369dd149171fba8fc06e84589d316`.
The files have not been normalized, trimmed, regenerated or edited.
Their SHA-256 hashes are recorded in `assets/ORIGIN.json`.

No LICENSE file was found in that commit's repository tree. The original
sounds are included in this local user-requested implementation; no public
redistribution permission is asserted by this project.

## Original UI reference and user-provided artwork (0.2.0)

The native UI blocks in `src/app/OriginalMainUi.cpp` are directly extracted from the same repository's
`TaikoKeyASIO_main.cpp` and `TaikoKeyASIO_main.h` at the pinned commit above, at the
user's explicit request. The original uses native Win32 ComboBox/Button/Trackbar
controls and a 400×400 BITMAP resource drawn with GDI BitBlt. Extracted blocks include
control creation, font setup, static colors, background painting and client sizing.
Source: [TaikoKeyASIO_main.cpp](https://github.com/4dblackhole/Taiko-Key-ASIO/blob/c3ed9a669e3369dd149171fba8fc06e84589d316/TaikoKeyASIO_main.cpp).
The app connects them to the requested Settings window and WASAPI controller.
No FMOD audio or original keyboard-processing implementation is incorporated.
The original repository has no stated license at this commit. This project
does not grant rights to upstream code.

`src/image/Asset 1-8.png` was provided by the user. `Background.bmp` is its
pixel-preserving BMP conversion for the Win32 resource compiler, embedded in the EXE.
No license to the artwork or third-party trademarks is granted by this project.

## dr_wav

Source: https://github.com/mackron/dr_libs
Pinned commit: `dfe8377631000664666519fdb83da193fd8037f4`.
Unmodified `third_party/dr_wav.h`, including its full license text.
License choice: MIT No Attribution, copyright 2023 David Reid.
Used only for loading PCM/float WAV data off the real-time path.

## miniaudio

Source: https://github.com/mackron/miniaudio
Version 0.11.23, commit `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`.
Unmodified `third_party/miniaudio.h`, including its full license text.
License choice: MIT No Attribution, copyright 2025 David Reid.
Only offline resampling is used. Device I/O, engine, node graph, resource
manager, decoding, encoding and generation are disabled by compile definitions
in `src/audio/Converters.h`. Windows WASAPI is called directly by project code.

## Portable build runtime

Local build uses LLVM-MinGW `20260922`, Clang 23.1.2, x64 UCRT,
from https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922.
C++ and compiler support runtimes are statically linked. Windows 10/11 UCRT
and Windows system DLLs remain operating-system dependencies.
The bundle's license notices are included in `third_party/LLVM-MINGW-LICENSE.txt`.
MSVC builds instead select the static MSVC runtime (`/MT` in Release).

No FMOD, ASIO SDK, FlexASIO, PortAudio, CUDA, Direct3D or Vulkan dependency is used.
