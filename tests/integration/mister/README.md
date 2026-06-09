# MiSTer streamed-frame fidelity tests

Integration tests that stream a rendered frame through the **real** GroovyMiSTer output path
(`gfx/gfx_mister.c` + the C++ `groovymister` UDP sender) to a software **simulator** of the
MiSTer FPGA over loopback UDP, then compare the rendered vs. received frame with **SSIM** (gate)
and **VMAF** (advisory). Not part of CI.

## Run
```bash
make mister-integration-test          # from repo root (no ./configure needed)
# or:
cd tests/integration/mister && make run
```
Requires `libcriterion-dev`. Current state: **50 tests, all green**. (The suite found and the fork
fixed two off-by-ones in `gfx_mister.c`; the edge tests are now regression guards — see "Edge &
branch regression guards".)

## Layout
```
spec_*.c            specification layer — declarative behavior tests
  spec_frame_fidelity.c   the 5 acceptance scenarios + interior-lossless companion
  spec_cfg_permutations.c lz4 / rgb565 / scanlines / interlaced permutations (interior)
  spec_edge_fidelity.c    per-formula edge guards (565/scanlines/interlaced/vertical/
                          flipped_rotated/hw) — solid fill, populated-border checks
  spec_rotation_fidelity.c  rotated-blit interior faithfulness (vertical / flipped_rotated / flipped)
  spec_hwrender_fidelity.c  hw-readback interior faithfulness (shift-tolerant, models the vflip)
  spec_link.c             real gfx_mister links + connect handshake
  spec_transport.c        UDP transport boundary
driver/             hides mister_set_mode/mister_draw + simulator lifecycle behind a small API
sim/                the FPGA simulator: factories (L1) → state_machine/composer (L2) → udp_transport (L3)
metrics/            ssim (vendored ~60-line luma kernel), compare (RGB↔YUV/resample), vmaf (advisory)
seams/              fake_* — link seams for RetroArch globals (config/audio/menu/video/verbosity)
unit/               fast pure-logic tests (ssim, conversion, protocol decoders) — no sockets
```

## How it links
`gfx_mister.c` compiles with `-DHAVE_MISTER -DHAVE_MENU` + RetroArch include paths — **no
`config.h` needed**. The Makefile compiles the real `gfx_mister.c`, `groovymister*.cpp`, the
libretro scaler, and vendored `deps/lz4`, and substitutes only ~9 RetroArch global getters via
`seams/fake_*.c` (which include the real headers so struct layouts match).

## Reliability constraints (don't regress these)
- **Run serially (`-j1`)** — every fidelity test binds the GroovyMiSTer hardcoded UDP port
  **32100**; Criterion's default parallelism makes concurrent tests cross-talk on the socket
  (crashes/misrouted frames). The `run` target sets `-j1`.
- **Frames must fit the socket buffer** — a raw 256×192 frame is ~98 datagrams whose
  socket-buffer *truesize* exceeds the default `rmem_max` (~208 KB), dropping packets. Fidelity
  specs stream with **LZ4** (lossless — proven by `lz4_compression_is_nondegrading`) so a frame
  is ~10 datagrams. Specs also `cr_skip` (not fail) if a frame doesn't fully arrive.
- **No ad-hoc file I/O inside a test** — Criterion sandboxes tests in BoxFort; a stray
  `fopen(...,"w")` for diagnostics crashes the worker. Use `cr_log_info` instead.
- **Modes must be >= 200 wide and >= 160 tall** — `mister_set_mode` silently drops anything smaller
  (`gfx_mister.c:674`), so no SWITCHRES is sent and the frame never arrives (looks like an rmem
  skip, but isn't). Rotated tests use a square 200×200 to satisfy the floor and keep the rotation
  reference simple.

## Adding a permutation / scenario
Use the driver setters before connecting (`mdrv_start`):
`mdrv_set_lz4`, `mdrv_set_rgb565`, `mdrv_set_scanlines`, `mdrv_set_interlaced`,
`mdrv_set_rotation` (ORIENTATION_* 0..3), and `mdrv_arrange_mode[_scaled|_interlaced]`. For the
hardware-readback path use `mdrv_draw_hw` (supplies the frame via a `read_viewport` test double).
Compare the interior (inset to skip the edge guards' territory) for fidelity; rotation references
must inset all four sides (a rotation moves the dropped edge to a different border), and the hw
path reads bottom-up (vertical flip). Pixel-affecting knobs need a reference modeling the intended
transform; transport knobs (lz4) must stay lossless. Branch inputs (rotation, hw-render) are
injected through link seams in `seams/fake_video.c`, not mocks.

## VMAF (advisory, optional)
Auto-skips if no `vmaf` CLI on PATH. To enable, build Netflix libvmaf (no sudo):
```bash
python3 -m pip install --user meson
git clone --depth 1 https://github.com/Netflix/vmaf.git ~/src/vmaf
cd ~/src/vmaf/libvmaf && meson setup build --buildtype release -Denable_asm=false && ninja -C build
ln -sf ~/src/vmaf/libvmaf/build/tools/vmaf ~/.local/bin/vmaf
```
The helper honors `$VMAF_BIN` or `vmaf` on PATH; it never gates the build (VMAF is unreliable on
synthetic emulator content).

## Edge & branch regression guards
The suite originally carried a red-by-design test documenting a blit off-by-one. That defect (and a
second one the characterization surfaced) are now **fixed** in `gfx_mister.c`, and the tests are
green regression guards:
- `frame_fidelity::rgb888_matched_resolution_is_lossless` — full-frame SSIM (shared-bound fix,
  `:328`/`:342`).
- `edge_fidelity::*` — per-index-formula populated-edge guards (normal-565, scanlines, interlaced,
  vertical `:337`, flipped_rotated `:340`, hw-readback `:331`).
- `rotation_fidelity::*`, `hwrender_fidelity::*` — interior faithfulness per branch.

Reverting any single bound/term in `gfx_mister.c` turns the matching guard red.

## Regression-check against upstream
The suite is branch-agnostic (uses `ROOT_DIR=../../..`). To run it against another checkout
(e.g. antonioginer's upstream):
```bash
git clone --depth 1 -b mister https://github.com/antonioginer/RetroArch.git /tmp/upstream
cp -r tests/integration/mister /tmp/upstream/tests/integration/
cd /tmp/upstream/tests/integration/mister && make run
```
Before the fixes, upstream produced identical results (full-frame 0.928 / VMAF 96.87), confirming
both off-by-ones are upstream-original. Run against upstream now and the edge/full-frame guards go
**red** there (they pass in this fork) — that red delta *is* the proof our two `gfx_mister.c` fixes
are the only difference.
