# Build Instructions

Install CMake 3.25 or newer, Python 3+, Ninja, and a C/C++20 toolchain.
Clone the repository with its submodules:

```sh
git clone --recurse-submodules https://github.com/wize-logic/Nookery-Wii.git
cd Nookery-Wii
```

## Windows

Install Visual Studio with the "Desktop development with C++" workload selected.
Either use the UI, or:
From an **x64 Native Tools Command Prompt for VS** (or after running
`vcvars64.bat`), configure and build the MSVC preset:

```bat
cmake --preset windows-msvc-relwithdebinfo
cmake --build --preset windows-msvc-relwithdebinfo
```

Use the `windows-msvc-debug` preset for unoptimized builds with debug info.


## Linux and macOS

Configure and build one of the included presets:

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```

Use the `debug` preset for unoptimized builds with debug info.

## Android

See [BUILDING_ANDROID.md](BUILDING_ANDROID.md).

## Wii

Install devkitPro's `wii-dev` group and `ppc-zlib` (the preset expects devkitPro in `/opt/devkitpro`),
and Python 3.10 or newer. Of the submodules, only `extern/aurora` is needed.

```sh
git submodule update --init extern/aurora
cmake --preset wii
cmake --build --preset wii
```

This makes `build/wii/boot.dol`. See [wii/README.md](wii/README.md) for installing, releasing and
testing it.

## iOS

See [BUILDING_IOS.md](BUILDING_IOS.md).
