s3g Ambi Energy - Windows renderer comparison prototype
=====================================================

This is NOT a CLAP plugin or the finished VSTGUI migration. It does not use
REAPER, play audio, install files, or change your installed plugins.

1. Extract the entire ZIP into a writable folder on your Windows machine.
2. Double-click run-test.cmd. Leave input.bin and metal-reference.bin beside
   the executable. No developer tools or font installation are needed.
3. Wait for the final PASS / FAIL / ERROR. Results go into a new
   results-hardware-... folder. Send back report.txt from that folder.
4. The BMP images show this renderer's output alongside the corresponding
   *-metal-reference.bmp. Look for matching position, shape, color and trails.

Optional: run-software-test.cmd runs the same check through Microsoft's WARP
software renderer. Send that report too if hardware rendering fails, or to
help distinguish shader problems from driver/hardware differences. WARP timing
does not indicate hardware performance.

Use Windows 10/11 x64. Hardware mode requests Direct3D feature level 11_0.
The program uses Windows' D3D11 and D3DCompiler_47 system libraries. It has no
separate MinGW runtime DLLs. If a missing-DLL message appears before the program
starts, send that message; do not download DLLs from third-party sites.

The executable is an unsigned development build. If Windows or an organization
policy blocks it, report the warning; no security settings need to be disabled.
No administrator privileges are required.

What it checks
--------------
49 deterministic renderer-input frames, shared bit-for-bit with the Mac:
- Up to 64 signed ACN/SN3D channel coefficients and 16 simultaneous snapshots.
- Metal's body/detail energy, attack/release and inverse-color wake equations.
- All eight palettes, source motion/seam crossing, horizontal wake wrapping,
  vertical clamping, half-pixel motion, silence and history reset.
- One-channel/one-snapshot input, 1st through 7th order channel counts,
  two sources with phase differences, and higher-order-only energy.
- Odd-sized output buffers, enlargement to 1920x1080, and resizing down again.

The reference was rendered from the Mac plugin's actual Metal shader, not its
different legacy CPU bitmap fallback. The inputs are synthetic deterministic
HOA snapshot fixtures; this is not yet a live audio/host integration test.

Every frame checks both an independent CPU transcription of the Metal compute
equations and the saved Mac GPU result. Provisional cross-GPU limits:
field max error 0.003, mean error 0.00015; RGB max 8/255, mean 0.35/255.
These limits allow GPU arithmetic/half-float/filtering differences; PASS does
not claim byte-identical pixels or complete plugin parity. Do not loosen limits
to hide differences. Review failures and the paired images first.

Timings include synchronous GPU readback and output allocation, but exclude
CPU-reference calculations and writing images. They are diagnostic timings,
not a benchmark of the final live GUI. Initial shader compilation may pause.

The Windows executable has been cross-compiled on macOS. Windows shader
compilation and GPU execution must still be validated on your machine. Any
shader compilation error is written to report.txt when the program can start.

Keep the result folder even on failure. Re-running makes a new folder.
SHA256SUMS.txt covers the shipped files, including the reference and inputs.
