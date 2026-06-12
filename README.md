# 🎢 Coaster — Static Recompilation

```
   ____   ___    _    ____ _____ _____ ____
  / ___| / _ \  / \  / ___|_   _| ____|  _ \
 | |    | | | |/ _ \ \___ \ | | |  _| | |_) |
 | |___ | |_| / ___ \ ___) || | | |___|  _ <
  \____| \___/_/   \_\____/ |_| |_____|_| \_\

      Roller Coaster Construction Set (1993)
       ...rebuilt to run on a modern machine
```

> A from-the-binary static recompilation of **Coaster**, the 1993 DOS
> roller-coaster builder by **Code To Go**, published by **Walt Disney Computer
> Software**. We take the original 16-bit DOS executable, lift it to C, and link
> it against a tiny DOS-on-SDL2 runtime so it builds and runs natively on
> Windows 11 — no DOSBox, no emulator, no original code shipped.

---

## Part I — A Love Letter to a Forgotten Gem 💛

Before *RollerCoaster Tycoon*. Before *Planet Coaster*. Before a generation of
kids learned that the real fun was drowning guests in a lake — there was
**Coaster**.

It's 1993. Disney slaps its name on a box, and inside is a game that asks one
gloriously specific question: *can you build a roller coaster that doesn't make
people throw up — but almost?* You lay track segment by segment, bank the
curves, stack the hills, thread the loops, and then you **ride it** in a
first-person seat-of-the-cart view that, for 1993, on a 386, in 256 colors, felt
like genuine witchcraft.

And then you get **judged**. Six evaluators climb into your creation, each with
their own personality and their own criteria — thrill, smoothness, fear,
whatever dark math lives in their little pixelated hearts — and they score you.
Build it too tame and they yawn. Build it too insane and you've made a
guillotine on rails.

Speaking of which — the coasters shipped with names that are pure 1993 energy.
The original `.TRA` track files in the game are called:

> **CLASSIC** · **CRUSHER** · **DEMON** · **GUILLOTINE** · **THE HAWK** ·
> **MAGIC FLIGHT** · **MOUSZILA** · **SCAMPHERREL** · **SF RUNNER** ·
> **SUPERNOVA** · **SWEET-D** · the **MATTER**-horns and a **LEGEND** or two

*Mouszila.* Somebody at Code To Go named a roller coaster **Mouszila** and went
home proud, and they were right to.

Coaster never got the fame it deserved. This project is us giving it the
afterlife it earned: not trapped in an emulator, but **recompiled** — its actual
1993 logic, turned into C, running as a real native program. Everything old is
new again.

---

## Part II — The Technical Guts 🔧

### What "static recompilation" means here

We are **not** writing an emulator (no instruction-by-instruction interpreter at
runtime) and we are **not** rewriting the game from scratch. We mechanically
translate the original machine code into C, ahead of time:

```
COASTER.EXE (LZEXE-packed 16-bit DOS)
   └─► unlzexe          decompress to a clean, relocated MZ image
        └─► decode16    disassemble 8086/286 instructions
             └─► analyze find function boundaries (MSC-5 prologues + call targets)
                  └─► lift translate each function to C over a CPU struct
                       └─► link against a DOS-on-SDL2 runtime  → native coaster.exe
```

Each original routine becomes a C function `sub_XXXXXX(CPU *cpu)` that reads and
writes a struct modelling the 8086 register file and a flat 1 MB address space.
DOS `INT 21h`, video `INT 10h`, keyboard `INT 16h` and the mouse `INT 33h` are
re-implemented natively; VGA mode 13h and the EGA modes are blitted to an SDL2
window.

### The party trick: the game unpacks itself, live

`COASTER.EXE` ships compressed with **LZEXE v0.91** (Fabrice Bellard's famous
self-extracting EXE packer). You cannot disassemble a packed binary — the bytes
on disk are a tiny decompressor stub plus a squashed blob.

So we ported the LZEXE decompressor to C and baked it into the runtime
(`src/recomp/startup.c`). At launch the program reads **your** copy of
`COASTER.EXE`, undoes the LZEXE packing *in memory*, rebuilds the relocation
table, drops the image into the virtual address space at segment `0x0110`,
applies fixups, and jumps into the recompiled Microsoft-C `__astart`. This is
why the repository can ship **zero bytes of Disney's code** and still run the
real thing — you bring the cartridge, we bring the console.

### Current status — honest edition

This is an early, load-bearing foundation, not a finished port. What works
**today**:

- ✅ **LZEXE v0.91 unpacker** — standalone (`tools/unlzexe.py`) *and* a C port in
  the runtime; reproduces the 136,682-byte image with all 152 relocations.
- ✅ **Recompiler pipeline** — lifts **234 functions / ~31,700 lines of C** with
  zero lifter errors, sharded for fast parallel compilation.
- ✅ **Builds & links** a native `coaster.exe` (MSVC + SDL2 via vcpkg).
- ✅ **Boots for real** — feed it the original `COASTER.EXE` and it unpacks,
  relocates, and *executes* the recompiled startup, running through hundreds of
  the game's own functions and the Microsoft-C runtime init.

What's **not** there yet:

- ⏳ Most game logic is still auto-generated over stubbed leaf routines, so it
  runs the boot path and exits rather than drawing the title screen.
- ⏳ A handful of computed far-call targets and C-runtime helpers need real
  implementations (tracked as `[STUB]` / `[DISPATCH]` log lines at runtime).
- ⏳ Function-boundary detection in the C-runtime region is heuristic and will
  be refined; rendering of the actual coaster view is future work.

In other words: the engine turns over and the pistons fire. We haven't dropped
it in gear yet. 🚗💨

### Build it

**Requirements:** CMake 3.20+, a C17 compiler (MSVC 2022 tested), and SDL2
(easiest via [vcpkg](https://vcpkg.io)). Python 3.10+ only if you want to
re-run the recompiler.

```bash
# 1. Configure (point at your vcpkg toolchain for SDL2)
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows

# 2. Build
cmake --build build --config Release

# 3. Run — point it at YOUR legally-owned packed COASTER.EXE
./build/Release/coaster.exe path/to/COASTER.EXE --gamedir path/to/gamefiles
```

Want to regenerate the lifted C from your own copy?

```bash
python tools/unlzexe.py COASTER.EXE COASTER_unpacked.exe   # decompress
python tools/recomp.py  COASTER_unpacked.exe RecompiledFuncs  # lift to C
```

### Bring your own game 🎟️

This repository contains **none** of Coaster's copyrighted material — no
executable, no `COASTER1.RSC` resource blob, no `.TRA` tracks, no art or audio.
Coaster is © 1993 its respective rights holders. To build and play, supply your
own legally obtained copy of the game; the loader expects the original
LZEXE-packed `COASTER.EXE`. See [`LICENSE`](LICENSE) — the MIT license covers
*our* code only.

### Repository layout

```
coaster/
├── tools/                 the recompiler toolchain
│   ├── unlzexe.py         LZEXE v0.91 decompressor (standalone)
│   ├── decode16.py        8086/286 disassembler
│   ├── analyze.py         function-boundary + call-graph analysis
│   ├── lift.py            x86-16 → C lifter
│   └── recomp.py          driver: decode → detect → lift → emit
├── src/
│   ├── main.c             entry point / boot + window loop
│   ├── recomp/            CPU model, DOS/BIOS shims, LZEXE loader (startup.c)
│   ├── hal/               video / input / timer hardware abstraction
│   └── platform/          SDL2 window, renderer, input
├── include/               public headers (recomp/, hal/, platform/)
├── RecompiledFuncs/        AUTO-GENERATED lifted C (regenerate with recomp.py)
│   ├── coaster_recomp_*.c  the 234 lifted functions
│   ├── coaster_dispatch.c  indirect/far-call resolver table
│   ├── coaster_stubs.c     stubs for not-yet-identified targets
│   └── coaster_impl.c      hand-written overrides (wins at link time)
└── CMakeLists.txt
```

### Roadmap

1. Replace C-runtime stubs (`_output`, memory, file I/O) with real shims so
   `__astart` reaches the game's `main()`.
2. Get the title screen and menu rendering through the VGA/EGA HAL.
3. Track editor → ride view → the six judges. Make Mouszila ride again.

---

*Part of an ongoing series of PC static recompilations. Built with stubbornness
and too much coffee.*

**Further reading on the game itself:**
[MobyGames](https://www.mobygames.com/game/2160/coaster/) ·
[Wikipedia](https://en.wikipedia.org/wiki/Coaster_(video_game))
