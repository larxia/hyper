# Installing HYPER on an M-VAVE FM-1 — and getting back

> **HYPER v0.1 is an untested beta.** It is built from FM Stud1o and Felucca, two firmwares that run on real
> FM-1s, and its new parts pass their tests on a computer. But **no one has run this exact build on an FM-1
> yet.** Installing third-party firmware isn't supported by M-VAVE. Everything below can be undone, but use it
> at your own risk, and back up first.

## What you need

- An M-VAVE FM-1 and a **USB data cable** (not a charge-only cable).
- **Chrome or Edge** on a Mac, Windows or Linux computer (the install page uses WebMIDI), *or* Python 3.
- The firmware file `hyper-0.1.fwsc`. It comes out of the GitHub build (see "Getting the firmware file").

## 0. Back up first

Installing never touches the area where your projects, presets, samples and settings live. Still, back up:

- On the install page, the backup button saves your current music, projects, sound banks, samples and settings to
  a file on your computer.
- If you're coming from the official firmware, use M-VAVE's workbench (<https://open.m-vave.com/fm1-workbench>)
  to export your projects, DX7 voices and samples. **Projects from the official firmware don't open in HYPER, and
  HYPER's don't open in the official firmware.**

## 1. Getting the firmware file

The FM-1's compiler (JieLi's toolchain) only downloads onto Linux machines, so GitHub builds HYPER for you:

1. Make a new repository on GitHub (for example `HYPER`) and upload **everything** in the HYPER folder, including
   the hidden `.github` folder. (On github.com: **Add file → Upload files**, then drag in the folder's contents.)
2. **Settings → Pages → Source: GitHub Actions** (once).
3. **Actions → Build HYPER → Run workflow**, version `0.1`. It takes about 10 minutes.
4. When it's done, the run's page has:
   - **Artifacts → HYPER-0.1-firmware**: `hyper-0.1.fwsc`, plus `SHA256SUMS` and the licences.
   - **Artifacts → HYPER-0.1-tests**: the test log and the factory song as a WAV.
   - The Pages link, `https://YOUR-NAME.github.io/HYPER/`, which is the install page.

The build log's `ok    HYPER fits with ...` line says what was shrunk to fit the FM-1's app memory. For v0.1: the boot
art at 80 × 80 drawn 3×, no USB serial console (a developer's tool), no knob-card icons.

If the **pages** job fails with "Not Found", Pages isn't on yet: set **Settings → Pages → Source: GitHub Actions**,
then re-run the workflow. The firmware file is on the run's page either way.

## 2. Install

**From the browser (easiest).** Open your Pages link in Chrome or Edge, plug in the FM-1, press **Install**, and
allow MIDI access. The page starts the FM-1's update loader, writes the package, waits for the restart and checks
what's installed. Keep the cable in and the FM-1 powered until it says it's done.

**From a terminal.**

```sh
pip3 install mido python-rtmidi
python3 tools/fm1_install.py hyper-0.1.fwsc          # asks before writing (--yes skips the question)
python3 tools/fm1_install.py --info                  # what the connected FM-1 is running
```

The FM-1 restarts into HYPER: the boot art, then the factory song's 8 tracks at 150 BPM. Press PLAY.

### The factory song as a project (optional)

In the browser emulator the factory song plays at power-on, but the device build has no room for it in program
memory. To get it on your FM-1 as **project 1** (with FM Stud1o's trip-hop demo as project 2):

```sh
python3 tools/fm1_demo_projects.py --build
python3 tools/fm1_demo_projects.py --send            # REPLACES EVERY PROJECT ON THE DEVICE: back up first
```

Then open it from **SAVE → FILE / PROJECTS**.

## 3. Going back to the official firmware

Any of these works:

- **M-VAVE's workbench** (<https://open.m-vave.com/fm1-workbench>): it can return to the official V15 and migrate
  from compatible third-party firmware.
- **The install page's return-to-stock section** (it asks you to save a complete backup first).
- **The terminal**, with M-VAVE's official package (none is included here; M-VAVE publishes it, and their
  `fm1_open` SDK ships `FM-1_015.fwsc`):

  ```sh
  python3 tools/fm1_install.py FM-1_015.fwsc         # the unmodified official V15 is recognised
  python3 tools/fm1_install.py OTHER.fwsc --force    # any package the installer doesn't recognise
  ```

## 4. If something goes wrong

- **An install was cut off** (cable pulled, computer asleep) and the FM-1 is still in update mode: run the install
  again; it finishes the write.
- **Getting into update mode by hand**: with HYPER running, hold **OCT-** and **OCT+** together for 5 seconds
  (a countdown shows; let go to cancel). The FM-1 waits for an install.
- **The FM-1 crashes on start**: after two failed starts in a row, it drops into update mode by itself (the boot
  guard). Install again, HYPER or the official package.
- **Nothing responds at all**: the FM-1's boot ROM still accepts USB. Recover with
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) or M-VAVE's workbench, then reinstall.
- **Saving doesn't work** ("saves stay in RAM"): your FM-1 has a flash chip HYPER doesn't recognise. HYPER knows the
  original Puya P25Q80 and the UC25HQ80 used in units sold from late 2026. Please report the chip shown on
  **MENU → SYSTEM → INFO**.

## What's protected

The loader writes only the **application area**. It never writes the region holding your settings, projects, user
presets, user samples, or the panel and knob calibration (**MENU → CALIBRATION**). The boot ROM and M-VAVE's
bootloader are never written.
