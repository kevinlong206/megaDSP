# Contributing to megaDSP

This guide covers prerequisites, build commands, architecture conventions, and the PR process for human contributors. Automated tooling should read [AGENTS.md](AGENTS.md) instead — that file contains the full set of non-negotiable rules, DSP constraints, and compatibility invariants that apply to every change.

---

## Prerequisites

### macOS

- macOS 12 or later
- Xcode command-line tools: `xcode-select --install`
- CMake 3.22 or newer: `brew install cmake ninja`
- Git (included with Xcode CLT)

### Windows

- Windows 10 or later
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.22 or newer (bundled with Visual Studio or via <https://cmake.org>)
- Git for Windows

### Linux

- CMake 3.22 or newer and Ninja
- A C++20 compiler (GCC 12+ or Clang 16+)
- ALSA and X11 development libraries:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
  libasound2-dev libfontconfig1-dev libfreetype-dev \
  libgl1-mesa-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev ninja-build xvfb
```

JUCE and the CLAP JUCE extensions are fetched automatically during CMake configuration; no separate download is required.

---

## Configure, build, and test

### macOS (universal binary)

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
  -DMEGADSP_ADHOC_SIGN=OFF
cmake --build build --config Release \
  --target megaDSP_VST3 megaDSP_AU megaDSP_CLAP megaDSPTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Windows

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMEGADSP_ADHOC_SIGN=OFF
cmake --build build --config Release \
  --target megaDSP_VST3 megaDSP_CLAP megaDSPTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Linux

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMEGADSP_ADHOC_SIGN=OFF
cmake --build build --config Release \
  --target megaDSP_VST3 megaDSP_CLAP megaDSPTests --parallel
xvfb-run --auto-servernum ctest --test-dir build -C Release --output-on-failure
```

### Platform targets and artifact locations

| Platform | Targets | Output under `build/megaDSP_artefacts/Release/` |
|---|---|---|
| macOS | `megaDSP_VST3`, `megaDSP_AU`, `megaDSP_CLAP` | `VST3/`, `AU/`, `CLAP/` |
| Windows | `megaDSP_VST3`, `megaDSP_CLAP` | `VST3/`, `CLAP/` |
| Linux | `megaDSP_VST3`, `megaDSP_CLAP` | `VST3/`, `CLAP/` |

Audio Units (`megaDSP_AU`) are an Apple-only format and are built only on macOS.

---

## JUCE licensing

megaDSP is built on [JUCE 8](https://juce.com). You must comply with the JUCE licence when building or distributing:

- **Commercial licence** — required if you distribute a closed-source build.
- **AGPLv3** — open-source option; your modifications must also be released under AGPLv3.

See <https://juce.com/juce-open-source-licence> for full terms.

---

## Architecture overview

The codebase is a standard JUCE plugin project. Key layers:

| Source | Role |
|---|---|
| `src/Parameters.*` | Fixed host parameter layout, descriptors from the registry, and state migration (schemas 2–7) |
| `src/modules/ModuleRegistry.*` | Stable module enum, presentation metadata, control labels, defaults, formatting/parsing, contextual predicates, factories, and capabilities |
| `src/modules/*` | Per-module DSP declarations, implementations, and shared DSP helpers |
| `src/DspModules.h` | Compatibility umbrella for all DSP module declarations |
| `src/EffectRack.*` | Slot ownership, serial routing, bypass interpolation, latency alignment, visualization capture, and tail aggregation |
| `src/PluginProcessor.*` | Host buses, state, presets, latency reporting, and rack topology operations |
| `src/PluginEditor.*` | Tabbed rack shell and semantic module controls |
| `src/ui/*` | Reusable effect graph shell, module view interface, and view factory |
| `src/ui/modules/*` | Module-specific graph rendering and interactions |
| `tests/DspModuleTests.cpp` | Focused DSP module tests |
| `tests/TestMain.cpp` | Rack, host-contract, state, and integration tests |

Every new processor must be wired through Parameters, `src/modules/`, EffectRack, the registry (presentation metadata, defaults, formatting/parsing), the UI module-view factory, and tests.

---

## Module registry workflow

Adding a new module requires changes in a specific order:

1. **Append** a new `ModuleType` integer value at the end of the enum in `ModuleRegistry`. Never reuse or reorder existing values.
2. Add DSP declarations in `src/modules/` and the umbrella `DspModules.h`.
3. Register presentation metadata, control labels, defaults, formatting/parsing, predicates, and the factory in `ModuleRegistry`.
4. Wire the new module through `EffectRack` (slot routing, tail, latency).
5. Add a module view in `src/ui/modules/` and register it in the view factory.
6. Add DSP tests in `DspModuleTests.cpp` and host-contract assertions in `TestMain.cpp`.

The current registered module values are listed in [AGENTS.md](AGENTS.md).

---

## State and parameter compatibility

These constraints are non-negotiable:

- The host parameter topology is **fixed**: eight slots, twelve controls per slot. Parameter IDs must not change.
- `ModuleType` integer values are **stable**. Existing values must not be reassigned; new values are always appended.
- State migration must remain compatible with schemas 2 through the current schema (7). All past migration paths must continue to work.
- Module-type parameters are not automatable; saved integer values must restore exactly.
- Host automation is tied to **rack slot numbers**, not module identity. Reordering modules moves settings between slots; existing automation stays with its original slot number.

---

## Realtime audio thread constraints

Processing inside `processBlock` (or equivalent) must be:

- **Allocation-free** — no `new`, `malloc`, containers with dynamic growth, or exceptions.
- **Lock-free** — no mutexes, `std::atomic` spin-waits, or blocking calls.
- **Finite** — no unbounded loops or recursion.
- **Safe for both mono and stereo buffers** — do not assume channel count.

Smooth every audible continuous parameter. Structural changes (mode switches, IR loads, model crossfades) must be click-safe. IR file work for Convolution Reverb must remain entirely off the audio thread.

---

## CI

GitHub Actions runs the full build-and-test matrix on every push and pull request:

| Job | Runner | Targets |
|---|---|---|
| macOS (VST3, AU, CLAP) | `macos-15` | VST3, AU, CLAP, tests |
| Windows (VST3, CLAP) | `windows-2022` | VST3, CLAP, tests |
| Linux (VST3, CLAP) | `ubuntu-24.04` | VST3, CLAP, tests (under Xvfb) |

Pull requests must pass all three jobs before merging. Do not introduce a new build framework or test runner for a local change; use the existing CMake/CTest tooling.

---

## Screenshot update guidance

Screenshots live under `docs/images/`. The intended filenames are:

| File | Contents |
|---|---|
| `docs/images/rack-overview.png` | Full plugin window with multiple tabs populated |
| `docs/images/module-browser.png` | Module browser open, showing categorized results |
| `docs/images/visual-processing.png` | Several interactive graphs — e.g. EQ editor, compressor history, delay timeline |
| `docs/images/reverb-modulation.png` | Algorithmic and/or convolution reverb graph with controls visible |

To regenerate the screenshots from the real Standalone editor:

```sh
cmake -S . -B build-screenshots -DCMAKE_BUILD_TYPE=Release \
  -DMEGADSP_CAPTURE_SCREENSHOTS=ON -DMEGADSP_ADHOC_SIGN=OFF
cmake --build build-screenshots --target megaDSP_Standalone --parallel
MEGADSP_SCREENSHOT_DIR="$PWD/docs/images" \
  ./build-screenshots/megaDSP_artefacts/Release/Standalone/megaDSP.app/Contents/MacOS/megaDSP
```

The opt-in capture mode seeds synthetic rack names and exits after writing all
four PNG files; it is disabled in normal builds. Do not commit screenshots of
private data, unlicensed third-party content, or work-in-progress UI states.

---

## PR guidance

- **One concern per PR.** Separate DSP changes from UI changes from documentation changes.
- **Pass CI** before requesting review. All three matrix jobs must be green.
- **Add or update tests** for any changed DSP behaviour or host-contract assumption.
- **Follow the surrounding style** — JUCE/C++20, surgical changes, no unrelated reformatting.
- **Update `README.md`** when user-visible processors, controls, compatibility, or installation instructions change.
- **Do not commit** generated build artifacts, installed plugin bundles, or secrets.
- For coding rules, DSP constraints, and compatibility invariants, see [AGENTS.md](AGENTS.md).
