"""Download pinned portable CPU build tools into this project; no system installation."""
from pathlib import Path
import hashlib
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent / 'toolchain'
DOWNLOADS = [
    ('llvm.zip', 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip',
     'e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666'),
    ('cmake.zip', 'https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-windows-x86_64.zip',
     '4d52ebab7193a698651639ed80d8d04fd903358843572cf44c7fd234cb7c26ab'),
    ('ninja.zip', 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip',
     '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'),
]


def main():
    ROOT.mkdir(exist_ok=True)
    for name, url, expected in DOWNLOADS:
        archive = ROOT / name
        if not archive.exists():
            print(f'Downloading {name}...', flush=True)
            urllib.request.urlretrieve(url, archive)
        if hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() != expected:
            raise RuntimeError(f'Checksum mismatch: {archive}; no files extracted')
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                target = (ROOT / member.filename).resolve()
                if not target.is_relative_to(ROOT.resolve()):
                    raise RuntimeError(f'Archive path escapes tool directory: {member.filename}')
            bundle.extractall(ROOT)
        print(f'Verified and extracted {name}', flush=True)
    shutil.copyfile(ROOT / 'llvm-mingw-20260922-ucrt-x86_64/LICENSE.TXT',
                    ROOT.parent.parent / 'third_party/LLVM-MINGW-LICENSE.txt')


if __name__ == '__main__':
    main()
