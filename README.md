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
- ✅ **Recompiler pipeline** — lifts **886 functions** (near *and* the
  Microsoft-C large-model far-code segments above DGROUP) with zero lifter
  errors, sharded across 18 files.
  The toolchain is synced from
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), so the 16-bit lifter's
  correctness fixes — ADC/SBB carry as a real input, shift flags at a zero or
  oversized count, 32-bit MUL/DIV, three-operand IMUL, string-op segment
  overrides, word port I/O, a non-fatal divide-by-zero — land here too.
- ✅ **Relocation-correct lifting** — segment immediates are rebased by the load
  segment exactly as DOS would, so `DS` points at the real DGROUP. This is what
  lets the game find its own data.
- ✅ **Builds & links** a native `coaster.exe` (MSVC + SDL2 via vcpkg).
- ✅ **Boots into the game** — feed it the original `COASTER.EXE` and it unpacks,
  relocates, runs the Microsoft-C startup into the game's `main()`, opens the
  real `COASTER1.RSC` / `HSCORE.DAT`, and **reads its resource files**.
- ✅ **Boots through device setup** - it probes the AdLib at 0x388 and a Sound
  Blaster at 0x226, switches to text mode 3, then to **VGA mode 13h**, and
  unchains it into **Mode X** (SEQ index 4 <- 0x06, GC index 5 <- 0x40, CRTC
  index 0x14 <- 0). That is the real 1993 video driver running.

What's **not** there yet:

- ⏳ **Nothing is drawn.** The HAL renders mode 13h as a linear 320x200 buffer
  at A000, but the game unchains the VGA - the framebuffer is four planes
  selected by the Sequencer Map Mask, so a linear read is not the picture.
  Mode X support in the video HAL (via the `RECOMP_MEM_HOOK` hook the runtime
  already carries) is the next real piece of work.
- ⏳ **One unresolved indirect jump derails startup.** `[DISPATCH] jmp miss
  0187:0000` - a driver slot that is still null when it is called. Control
  returns to the wrong place, the next thing executed is a garbage `INT 21h
  AH=3Bh`, and the program then sits in `sub_0017C8` forever. Everything after
  this point in the log is downstream of that one miss.
- ⏳ Function-boundary detection is heuristic; a few far-code spans over data
  tables are imperfect, and some C-runtime/native shims are still stubs.
- ⏳ Rendering the actual coaster view is future work.

In other words: the engine turns over, it's idling, it's even reading the map —
but it hasn't pulled out of the station yet. 🎢

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
│   ├── coaster_recomp_*.c  the 886 lifted functions
│   ├── coaster_dispatch.c  indirect/far-call resolver table
│   ├── coaster_stubs.c     stubs for not-yet-identified targets
│   └── coaster_impl.c      hand-written overrides (wins at link time)
└── CMakeLists.txt
```

### Roadmap

1. ~~Get the Microsoft-C startup to reach the game's `main()`.~~ ✅ done
2. ~~Load the game's resource files (`COASTER1.RSC`, the `.TRA` tracks).~~ ✅ the
   game opens and reads them.
3. ~~Crack the resource-loading hang.~~ ✅ Three things: indirect jumps lowered
   to comments (control fell through into garbage), no interrupt poll (the game
   busy-waits on a counter its own INT 1Ch handler increments), and near calls
   that wrap past their segment base being dropped on the floor.
4. **Get a picture on screen** ← *we are here.* Mode X in the video HAL, and the
   null driver slot behind `jmp miss 0187:0000`.
5. Title screen and menus through the VGA HAL.
6. Track editor → ride view → the six judges. Make Mouszila ride again.

### How we got here (milestones)

The hard part of a recomp is rarely the first build — it's making the lifted
code *behave*. The fixes that moved the needle, in order:

| Fix | Why it mattered |
|-----|-----------------|
| **LZEXE v0.91 unpacker** (Python + C port) | The on-disk EXE is a compressed blob; nothing is possible until it's expanded and relocated. |
| **Relocation-aware lifting** | The lifter baked the *un-relocated* `MOV DS, DGROUP`; `DS` pointed into the void and every filename read came back garbage (`"./H"`). Rebasing segment immediates by the load segment is what made the game find its own data — it instantly started opening `COASTER1.RSC`. |
| **DOS `INT 21h/48` off-by-one** | Startup asked for exactly the free memory and the allocator refused it, deadlocking the boot. |
| **Signed branch displacements** | Backward `call`/`jmp`/`jcc` wrapped ~64 KB forward into phantom symbols. Reading them as signed (in both the lifter and the function-discovery pass) resolved every call — zero dispatch misses. |
| **Secondary-entry & far-code lifting** | Loops that branch *before* their own entry, plus the MSC large-model far-code segments above DGROUP, are now lifted correctly. |

---

*Part of an ongoing series of PC static recompilations. Built with stubbornness
and too much coffee.*

**Further reading on the game itself:**
[MobyGames](https://www.mobygames.com/game/2160/coaster/) ·
[Wikipedia](https://en.wikipedia.org/wiki/Coaster_(video_game))
