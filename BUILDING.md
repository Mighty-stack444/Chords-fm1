# Building CHORDS

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `omni.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `chords.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1` makes a release build; the package is `build/chords-0.1.fwsc`.

On macOS with podman instead of Docker, put a `docker` script that runs `exec podman "$@"` first
on your PATH. `OM_JOBS` (default 4) limits parallel compiles: a podman machine drops
connections when many containers start at once.

Build option: `OM_CDC=1` adds Felucca's USB serial function (off by default: one plain
MIDI interface).

The build also generates `build/gen/` (the font). It fails if a soft-double routine is linked
(a `double` crept in) and checks the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the maths library against libm; flash storage; Felucca's update-path tests against `build/chords.fwsc`; and the whole app in the simulator
(`tests/scenarios/*.omni`), with its screenshots and audio in `build/scenarios/`.

`host/build_host.sh` builds the simulator alone; `build/host/omni_host SCRIPT OUTDIR` runs one
script (the command list is at the top of `host/omni_host.c`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/chords.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for Felucca's update
mode. If the FM-1 no longer starts but reaches the chip's update mode (4C4A:8057 on USB),
`tools/fm1_rescue.sh` puts stock firmware back from a Mac; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Building in the cloud

`.github/workflows/build.yml` does all of the above on GitHub's machines: push this tree to a GitHub
repository, set Settings > Pages > Source to "GitHub Actions", and every push to `main` builds the
package, the browser emulator and a site with the web installer, published on the repository's
Pages. The package is also attached to each run (Actions > the run > Artifacts).
