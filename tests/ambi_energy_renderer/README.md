# Ambi Energy renderer parity prototype

This is the first-stage Windows feasibility/visual-parity experiment. It is
**not** a new CLAP, a VSTGUI adaptation, or a release-platform-policy change.
Ambi Energy remains Mac-only until the native Windows run and subsequent
host/GUI acceptance are complete. Tracker remains deferred independently.

## Architecture and source of truth

- `plugins/clap_ambisonic_energy_visualizer/renderer/metal_shader.h` contains the
  exact 7,847-byte shader extracted from the shipping Cocoa editor. Both that
  editor and the offscreen Metal harness now include it. Shader extraction and
  sharing the existing 80-byte parameter layout are the only production changes.
- `d3d_shader.h` translates the same compute and fragment equations to HLSL
  Shader Model 5. It preserves the two RGBA16F history textures, MaxRe weights,
  64-channel signed snapshot projection, body/detail separation, attack/release,
  wake transport/decay, all eight palettes, linear-clamp sampling, vertical flip,
  thresholds, and RGBA8 UNORM output (no additional sRGB conversion).
- The D3D11 backend reads the previous texture through an SRV and writes the
  next through a UAV. This avoids depending on extended typed-UAV-load support.
  Resources are unbound between compute/draw and history swaps. HLSL is embedded
  and compiled with D3DCompile on Windows, so the EXE can be built on the Mac
  without a Windows SDK shader compiler or extra shader files in the package.
- `energy_reference.cpp` is an independent CPU transcription of the **Metal
  compute** equations, including round-to-nearest-even half storage. It is not
  the legacy Cocoa bitmap fallback and is not proposed as a live CPU renderer.
- The capture interface intentionally waits for GPU completion and reads pixels
  back. This is a test interface, never an audio-thread or final GUI render loop.

Windows command-line paths use UTF-16 `wmain`, explicit UTF-8 conversion, then
`filesystem::u8path`. Tests do not depend on the Windows ANSI code page.

## Build and record on Mac

From the repository root (no downloaded dependencies needed):

```sh
cmake -S tests/ambi_energy_renderer -B build-ambi-energy-probe-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-ambi-energy-probe-mac -j 4
ctest --test-dir build-ambi-energy-probe-mac --output-on-failure
build-ambi-energy-probe-mac/s3g_ambi_energy_renderer_probe --generate build-ambi-energy-probe-mac/input.bin
build-ambi-energy-probe-mac/s3g_ambi_energy_renderer_probe --record build-ambi-energy-probe-mac/input.bin build-ambi-energy-probe-mac/metal-reference.bin build-ambi-energy-probe-mac/record
build-ambi-energy-probe-mac/s3g_ambi_energy_renderer_probe --compare build-ambi-energy-probe-mac/input.bin build-ambi-energy-probe-mac/metal-reference.bin build-ambi-energy-probe-mac/compare
```

GPU calls need access to the real Metal device; a filesystem/process sandbox
can hide it. Use the normal local session or approved GPU access, not a mock.
The probe refuses existing output folders/recordings. Use new names for reruns.
References may only be recorded by the Mac backend. Their header fingerprints
the complete input fixture (basis, weights, snapshots, parameters, dimensions).

The 49-frame fixture uses synthetic signed HOA samples generated with the
repository's actual basis and weighting functions. The Mac writes those inputs
to disk so Windows consumes identical bytes, avoiding platform sin/cos/random
differences. This stage bypasses CLAP's audio callback and snapshot ring.

## Cross-build and package

```sh
cmake -S tests/ambi_energy_renderer -B build-ambi-energy-probe-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-w64-x86_64.cmake"
cmake --build build-ambi-energy-probe-windows -j 4
cmake -P scripts/package-windows-ambi-energy-probe.cmake
```

The package helper requires successful Mac recording and same-Mac replay
reports. Override `input_file`, `reference_file`, `record_report`,
`compare_report`, and `build_dir` with `-D...` before `-P` if using other paths.
Windows testing is described in `WINDOWS_README.txt`. Test hardware first,
then WARP for comparison. Keep both reports. A successful Mac cross-build does
not validate HLSL compilation or Windows driver execution.

## Acceptance and limits

- CPU unit checks: all non-NaN half-float bit patterns roundtrip, rounding ties,
  reset, uniform W-only energy, global polarity invariance, higher-order/body
  separation, wake wrap/clamp, invalid inputs.
- GPU sequence: all eight maps, 0/1/16 snapshots, 1/4/9/16/25/36/49/64 active
  channels, moving sources across the horizontal seam, opposed-phase sources,
  higher-order-only input, silence tails, half-pixel motion and reset.
- Output sizes include native map resolution, 513x257, 1920x1080, 319x193 and
  257x129, exercising sampling and nontrivial row pitch.
- Provisional per-frame field limits: maximum 0.003, mean 0.00015. RGB limits:
  maximum 8/255, mean 0.35/255. Alpha must remain opaque; non-finite output fails.
  These are numeric-parity gates, not a promise of pixel-identical output.
- Image pairs require visual review. Render timing includes synchronous GPU
  readback and allocation, not CPU checking/image writing; it is not live GUI
  performance. Shader compile time is outside those per-frame timings.

## Work deliberately left for the next stage

1. Run this package on the separate Windows GPU. Fix shader/driver/numerical
   differences before treating the Windows renderer as validated.
2. Build the VSTGUI shell with the original MAP/SIZE and title-preset behavior,
   shared Fira Code/colors/alignment/separated menus, and Energy's special
   dynamic Normal/Large sizing. Do not invent controls from dormant code.
3. Embed the two native GPU surfaces below custom menus, without switching to
   the analytically different old CPU fallback when a menu opens. Preserve
   display scaling, orientation, color transfer, and independent aspect sizing.
4. Integrate the live snapshot ring and visibility/host-activity gating, eliminate
   synchronous readback from live rendering, handle device loss/recreation, and
   check repeated open/close and multiple instances on both platforms.
5. Verify unchanged float32/float64, 64-channel ACN/SN3D passthrough in REAPER,
   both in-place and out-of-place; preserve plugin ID, parameter ID and state.
   Code inspection found an existing risk: `process()` clears all outputs before
   copying inputs while the ports advertise in-place pairing. This prototype
   leaves audio code untouched; cover and resolve that risk during integration.
6. Benchmark actual sustained 30Hz operation, including lower-end Windows
   hardware, before choosing a supported fallback/device policy. WARP support
   in the test program is diagnostic, not an approved real-time fallback.

Relevant platform references: [D3D11 feature levels](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro),
[D3DCompile](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile),
[CPU-access restrictions](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_cpu_access_flag).
