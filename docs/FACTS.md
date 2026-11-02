# Archon Ultra (DOS, 1994) → Amiga 1200: Facts page

Tags: **[driver]** from a hardware driver (none exists here, see F1), **[ROM]** from analysis of the files,
**[trace]** measured in a reference-emulator run, **[assumed]** default or estimate. Nothing here was tested on real hardware.

## A. Identity

| # | Fact | Tag |
|---|---|---|
| F1 | The game is **Archon Ultra** (© 1994 Free Fall Associates, SSI CD-ROM edition), a **DOS/4GW 32-bit protected-mode PC game**, not an arcade/console board. No MAME driver applies, so all "source hardware" facts come from the executable and a DOSBox run. | ROM |
| F2 | `ARCHON.EXE` (499,805 B, md5 4445725b…) = 62,116 B MZ stub + DOS/4GW Professional extender (Rational Systems 1990–93) + the game as an **LE** module at file offset 0x270CC, built with Watcom C 386. `ARCHON.PKG` (2,902,220 B, md5 5b7f8013…) and `TITLE.PKG` (1,018,308 B, md5 c6a04cfd…) are data packages, format not yet analysed. | ROM |
| F3 | Kickstart file: 524,288 B, header `exec 40.10 (15.7.93)` = A1200 KS 3.1 (40.68). SHA-1 e21545723fe8374e91342617604f1b3d703094f1 (I recall this as the known value; not verified against a database). Testing only. | ROM |

## B. The program (LE module)

| # | Fact | Tag |
|---|---|---|
| B1 | 3 objects: **obj1 code+rodata** 0x3C353 B (246,611) at virtual 0x10000, 61 pages, 32-bit, R+X; **obj2** 0x442 B at 0x50000 (R+X); **obj3 data/bss** 0x33DD0 B (212 KB) at 0x60000, R+W, 5 pages. Page size 4096. Entry = obj1 + 0x26D2E (Watcom `_start`, `EB xx "WATCOM C 386…"`). Obj1 file offset 0x37214. | ROM |
| B2 | Under DOS/4GW in DOSBox obj1 is loaded at **linear 0x16D000** (relocated by +0x15D000; instruction starts match 1982/2000 sampled executed addresses). | trace |
| B3 | Linear sweep of obj1: ~79,300 instructions (includes embedded data), 19 undecodable bytes; only 7 FPU instructions, 339 `int`, 1,127 `in`/`out`, 259 indirect `call`/`jmp` sites (mostly vtable calls `call [edi+0x1c]`, `[ebx+0x148]`, …). | ROM |
| B4 | **No self-modifying code observed.** Obj1 (0x16D000–0x1A9353) hashed every frame for 12,000 frames of computer-vs-computer play (board, spells, arenas): no byte changed after frame 200 (load/relocation done by then). Not a proof for paths not exercised. | trace |
| B5 | Executed so far: **32,204 distinct instruction addresses** (≈40% of the swept count) in a 12,000-frame (171 s) run. The remainder (unexercised menus, modem/link play, saves, other arenas/pieces) is **not yet covered**. | trace |
| B6 | Computed jump targets are not yet resolved. Executed addresses are saved in `traces/cc4/executed_instruction_starts.txt.gz` as the seed for the control-flow discovery. | open |

## C. Hardware the program uses (all measured, [trace]; DOSBox 0.74-3 as reference)

| # | Fact |
|---|---|
| C1 | CPU: 386+ (manual: 386DX/33, 4 MB, VGA, mouse). Runs 32-bit flat; DOSBox `core=normal`, fixed 30,000 cycles/ms. |
| C2 | **Video**: VGA mode 13h set-up then **unchained ("mode X") 320×200, 256 colours**, 256 KB VGA memory, CRTC scan-line pitch 352 px (offset 44), **hardware display-start scrolling and page flipping** (arena scrolls through ≥216 distinct start addresses per 1000 frames). A 640×400 VGA mode (DOSBox mode id 9) is shown for ~30 frames while loading. 6-bit DAC palette (writes through 0x3C8/0x3C9, ~40k data writes in 12,000 frames). |
| C3 | VGA port write traffic (12,000 frames): 0x3C5 sequencer data **6.17 M** writes (map-mask changes per blit run, ~515/frame), 0x3C4 13k, 0x3CE/0x3CF 2.3k/9, 0x3D4/0x3D5 4.1k/0.3k (display start), 0x3C0 3.9k. So blitting is plane-mask driven. |
| C4 | **Timing**: PIT ch0 reprogrammed; **0x43 written 780k times (value 0x80 = latch counter 2)** → the game reads a PIT counter as a fine clock; IRQ0 EOI (port 0x20) 55.6k times ≈ 325 Hz; **CMOS RTC** ports 0x70/0x71 + slave PIC EOI (0xA0) 21.9k ≈ 128 Hz periodic IRQ8. The main wait is a 4-instruction spin at obj1+0x208A6 (`mov eax,[eax+0xe]; mov edx,[esi]; cmp edx,[eax+0x12]; jb`) comparing a tick counter with a deadline; this loop is ~95% of all executed instructions. Which IRQ feeds `[esi]` is not yet determined. |
| C5 | **Sound**: Sound Blaster DSP at base 0x220 (IRQ/DMA via 0x224/0x225 mixer, reset 0x226): commands 0x40, 0x14, 0xD0, 0xE1, 0xD1, **0x41 (set rate; 0x2B20 = 11,040 Hz seen)**, **0xB6 + mode 0x30 (16-bit stereo, auto-init DMA, block 0x16F+1 samples)**, 0xD5. Library string `BOBSOUND`. **No OPL (0x388) and no MPU-401 (0x330) writes** → sound is digital samples software-mixed by the game and streamed by DMA. DMA channel registers 0x0A/0x0B/0x0C/0x83 written. Exact sample set and mixer algorithm: not yet analysed. |
| C6 | **Input**: keyboard (int 9) with default maps *Light*: E/S/D/F + Space (button A) + B (button B); *Dark*: arrow keys + keypad 0 (A) + keypad `.` (B); also mouse (INT 33h) and joystick (port 0x201 read once; joystick item greyed in the menu without a joystick). Menu bar: Alt or button B; Esc leaves. |
| C7 | Storage: reads `archon.pkg`/`title.pkg` (3.9 MB together). A save-game file is written by the menu (not exercised). Modem/link play exists (not to be ported). |

## D. Per-frame work (measured; "frame" = one 70 Hz VGA vertical period; idle spin excluded)

DOSBox `core=normal`; one x86 instruction = 1 count; **a REP string instruction counts once regardless of iterations, so these are lower bounds for work**.

| scene (cc4 trace) | busy x86 insns/frame (median) | notes |
|---|---|---|
| title / menu / static board | ≈ 8–13 k | VRAM changes only ~10 frames/s |
| spells, arenas (frames 3000–9000) | 22–31 k median, ≈ 62–80 k mean per 1000-frame window, p90 166 k, p99 237 k | VRAM changes ≈ 40–48 times/s |
| whole 12,000-frame run | mean 49.9 k, median 12.7 k, p95 194 k, max 427 k (= the 30,000-cycle/ms DOSBox cap, i.e. load frames) | 21% of frames > 100 k |

Peak instantaneous demand is therefore far above the mean; the figures include the run being capped at 428 k/frame by the emulated 30 MIPS CPU, so true peaks on a slower CPU would only be visible in game-level slowdown.

## E. Target machine (as stated by the user, then corrected)

| # | Fact | Tag |
|---|---|---|
| E1 | **Amiga 1200 with 68EC030, AGA, 2 MB chip, 8 MB fast, Kickstart 3.1** (user's first message; the 68020 figure in my first reply was wrong and withdrawn). Joystick two-button in port 2, mouse in port 1, start = fire. | user |
| E2 | **030 clock not given → assumed 40 MHz.** The budget below depends on it. | assumed |
| E3 | AGA has 8 bitplanes, so the 256-colour 320×200 image maps to hardware playfield; but VGA mode X uses chunky-interleaved planes (pixel x → plane x&3) and plane-mask writes; an Amiga build must keep the 256 KB VGA model in fast RAM and convert chunky→planar for display (or write directly in planar order if the translated code allows). Not yet decided. | assumed |

## F. Feasibility figures (estimates, [assumed] until a translator is measured)

* A 68EC030 @ 40 MHz does roughly 6–10 M simple instructions/s with caches on (assumed).
* A faithful x86→68k translation of flag-heavy 32-bit code typically needs ~4–8 68k instructions per x86 instruction (assumed; includes flag, segment and port-I/O handling).
* Required in arena scenes: ≈ 75 k insns × 70 ≈ 5 M x86/s mean, i.e. ≈ 20–40 M 68k instr/s → **2–4× over budget** at 40 MHz. Title/board scenes (≈ 10 k/frame ≈ 0.7 M/s) fit. REP-string and plane-mask I/O work are not included and could add a large share.
* Mitigation candidates (none claimed yet): fusing plane-mask-write + store sequences, native blitter/fill replacements for recognised loops (keeping byte-exact VRAM dumps), running the game frame at < 70 Hz ticks, 68030 cache-friendly dispatch. Whether arena frame rate can be held is the **main risk**.

## G. Recommended approach (Facts ⇒ decision, per the brief)

* **(A) static translation** looks feasible as far as code size (246 KB) and absence of observed self-modification go; the open risks are the 259 indirect call sites, the DOS/4GW + BIOS/DOS service calls (339 `int`), and speed (F).
* **(B) interpreter** is ruled out by speed (an interpreter is 10–30× slower than translated code).
* **(C) hand re-implementation** only with the user's agreement; not proposed.
* So: **(A) static translation with a hardware-emulation layer (VGA mode-X model with display-start/palette/plane masks; PIT/RTC/PIC tick sources; SB16 DSP+DMA stream; keyboard/mouse INT 33h; DOS file I/O for the PKG files) around it.** Hardware-level behaviour that relies on cycle timing is limited to the tick/DSP-driven timers (C4), so tick counters can be driven from the Amiga CIA/vertical blank while preserving 70 Hz game ticks.

## H. Assumptions list

1. Target clock 40 MHz (E2). 2. PAL and NTSC both supported; game logic runs at the source tick rates (C4), not tied to Amiga frame rate. 3. Full fidelity: exact palette, all sounds at the original 11,040 Hz or higher (rate to be confirmed per sample), lossless packing. 4. FS-UAE testing with the user's Kickstart (not yet installed in this environment). 5. DOSBox 0.74-3 behaviour stands in for a real PC; **every fidelity claim is "checked against DOSBox only"**; no MAME/real-hardware oracle exists for this title. 6. The unexercised code paths (B5) are on the "unverified" list.

## I. Tooling built so far (this repository)

* `tools/dosbox-trace/trace.patch` — DOSBox 0.74-3 (Ubuntu source `0.74-3-5build2`, `--enable-debug=heavy`) instrumentation: per-frame csv (instruction count, idle count, video mode, display start, palette/VRAM/code hashes), gzip'd 256 KB VGA memory dump per changed frame, all port writes with instruction-count stamps, instruction-address coverage counts, scripted keyboard/mouse input, wave recording. `runvar.sh`, `shots.py` (render dumps to PNG), `db.tpl` (config).
* `traces/` — scripts for the two reference runs (`attract`: 9,000 frames title+menu; `cc4`: computer-vs-computer, 12,000 frames), per-frame csvs, executed-address list, port write counts. **Not committed** (size / third-party content): VGA dumps (267 MB), port logs (115 MB), wave recordings (30 MB); regenerate with `tools/dosbox-trace/runvar.sh`.
* Installed in the environment: DOSBox (apt, plus the instrumented build), qemu-m68k, m68k-linux-gnu-gcc, capstone. **Missing: FS-UAE, an AmigaOS-hunk toolchain (vasm/vlink/m68k-amigaos-gcc).**
