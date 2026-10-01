# Third-party provenance

No project-wide license is assigned. Source visibility does not change the
rights or license terms of upstream code, artwork, sounds or bundled libraries.

## Original sounds and UI (used with permission)

The hit sounds and UI code below come from
[4dblackhole/Taiko-Key-ASIO](https://github.com/4dblackhole/Taiko-Key-ASIO)
at commit `c3ed9a669e3369dd149171fba8fc06e84589d316`. That repository has no
LICENSE file; they are used and redistributed in this project with the
original author's permission.

- `assets/HitSounds/don.wav` and `kat.wav` are byte-for-byte copies of the
  [original HitSounds](https://github.com/4dblackhole/Taiko-Key-ASIO/tree/c3ed9a669e3369dd149171fba8fc06e84589d316/HitSounds),
  unmodified. Their SHA-256 hashes are recorded in `assets/ORIGIN.json`.
- `src/app/OriginalMainUi.cpp` contains UI blocks extracted from
  [TaikoKeyASIO_main.cpp](https://github.com/4dblackhole/Taiko-Key-ASIO/blob/c3ed9a669e3369dd149171fba8fc06e84589d316/TaikoKeyASIO_main.cpp)
  and `TaikoKeyASIO_main.h`: native ComboBox/Button/Trackbar creation, font
  setup, static colors, background painting and client sizing. Control IDs and
  labels were adapted to connect them to this project's Settings window and
  WASAPI controller. No FMOD audio or original keyboard-processing code is used.

## Main-window artwork

`src/image/Asset 1-8.png` is the main-window artwork supplied by this
project's author. `Background.bmp` is its pixel-preserving BMP conversion for
the Win32 resource compiler and is embedded in the EXE. No license to the
artwork or third-party trademarks is granted.

## dr_wav

Source: https://github.com/mackron/dr_libs
Pinned commit: `dfe8377631000664666519fdb83da193fd8037f4`.
Unmodified `third_party/dr_wav.h` in the source tree, including its full license text.
License choice: MIT No Attribution, copyright 2023 David Reid. MIT-0 requires
no notice, so the release ZIP does not ship the header.
Used only for loading PCM/float WAV data off the real-time path.

## miniaudio

Source: https://github.com/mackron/miniaudio
Version 0.11.23, commit `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`.
Unmodified `third_party/miniaudio.h` in the source tree, including its full license text.
License choice: MIT No Attribution, copyright 2025 David Reid. Not shipped in
the release ZIP, as above.
Only offline resampling is used. Device I/O, engine, node graph, resource
manager, decoding, encoding and generation are disabled by compile definitions
in `src/audio/Converters.h`. Windows WASAPI is called directly by project code.

## Build runtimes

GitHub release builds use MSVC with the static MSVC runtime (`/MT` in Release).

The optional portable build (`tools/build-portable.ps1`) uses LLVM-MinGW
`20260922`, Clang 23.1.2, x64 UCRT, from
https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922. Its C++ and
compiler support runtimes are statically linked, so packages built that way
include the toolchain's notices as `third_party/LLVM-MINGW-LICENSE.txt`.
Windows 10/11 UCRT and Windows system DLLs remain operating-system dependencies.

No FMOD, ASIO SDK, FlexASIO, PortAudio, CUDA, Direct3D or Vulkan dependency is used.
