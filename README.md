# AUCDataTool

Cross-platform viewer for analytical ultracentrifugation (AUC) raw data — a C++/Qt 6
reimplementation of the LabVIEW **AUC-Viewer 2.2.2** (AG Cölfen, Universität Konstanz).

Targets Windows, macOS and Linux; the UI is Qt Quick so an Android build is possible.

![AUCDataTool with a multi-wavelength run: 16 channels, 600 wavelengths, absorbance against the reference channel](docs/screenshot.png)

## Status

| Area | State |
|---|---|
| openAUC `.auc` reader/writer (v4, v5, stddev, interpolation bitmap, CRC); multi-wavelength `.auc` sets grouped per cell/channel | done |
| Multi-wavelength `.mwrs` (v1.0–1.4, with run XML) and `.mw` (v1.0/1.1, v1.2 with dark current) | done — layouts recovered from the LabVIEW program, **not yet validated on real files** |
| Lazy wavelength slices (runs of 8 cells × 2 channels × 600+ λ never loaded completely) | done |
| Wavelength selection, multi-wavelength averaging (MWA) over a λ range | done |
| Intensity ↔ absorbance with a reference channel (scan by scan or mean of reference scans) | done |
| Dark current (`.mw` v1.2): toggle subtracted/not subtracted | done |
| GPU scan plot (all scans in one pass, zoom/pan without re-upload) | done |
| Scan range / every n-th, reverse, spike filter, offset (point, baseline region) | done |
| Radial integration (∫A dr, ∫A·r dr) + integral-vs-time plot | done |
| TI/RI noise: load UltraScan noise XML or plain text | done |
| Live mode: follow a folder while the run is acquiring (new scans, new cells) | done |
| CSV export | done |
| XL `.RA/.RI/.WA/.WI/.IP` text files, 3D view (radius × λ), spectrum plot | pending |
| Beckman / Origin / US3 export, printing | pending |

## Build

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt ≥ 6.4 (Qt 6.8 LTS recommended) with
Quick, QuickControls2 and Concurrent.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/app/AUCDataTool path/to/run/         # files or folders
```

Options: `-DAUC_BUILD_APP=OFF` builds only the core library, tools and tests (needs only Qt Core).

### Tools

- `aucinfo PATH… [--bench]` — summary of every channel in files/folders (`.auc`, `.mwrs`,
  `.mw`); `--bench` times opening and wavelength-slice reads.
- `aucgen OUTDIR [--scans N --points N --cells N …]` — synthetic single-wavelength
  sedimentation-velocity data (`.auc`) with TI/RI noise, plus the noise as UltraScan XML.
- `mwlgen OUTDIR [--cells N --scans N --wavelengths N --points N --version 1.2|1.3|1.4]` —
  synthetic multi-wavelength `.mwrs` run with run XML: channel B = reference intensity
  (lamp spectrum × optics), channel A = sample with two sedimenting species of different
  spectra (bands at 280 and 260 nm), counting noise and a meniscus.

### App command line

```
AUCDataTool [files/folders] [--watch FOLDER] [--set key=value …]
            [--ti-noise FILE] [--ri-noise FILE]
            [--screenshot out.png [--screenshot-delay ms]] [--benchmark N]
```
`--watch` opens a folder in live mode. `--set` applies view/processing options, e.g.
`--set wavelengthIndex=60 --set displayMode=0 --set integrate=true`.

## Working with multi-wavelength data

- **Channels.** Every cell/channel of a run is one entry in the list, with all its
  wavelengths and scans. Opening reads only the file headers.
- **Wavelength.** Slider, ◀ ▶ buttons or Ctrl+←/→ step through the wavelengths. *Average
  wavelength range (MWA)* shows the mean over a range instead.
- **Absorbance.** For intensity data the *Data* panel switches between *Intensity* and
  *Absorbance*. Absorbance needs a reference channel (I₀); by default channel B of the
  same cell is used (the AUC-Viewer convention: A/S = sample, B = reference).
  - *Scan by scan*: sample scan i against reference scan i.
  - *Mean of reference scans*: every scan against the mean of reference scans *k…m*.
  - A = −log₁₀(I/I₀) where I and I₀ exceed 100 counts; other points are set to 3.0
    (as in the LabVIEW program).
- Settings (wavelength, reference, display) are remembered per channel.

**Performance** (synthetic run, 8 cells × 2 channels × 600 λ × 50 scans × 800 points =
1.5 GB, 800 files; Linux, warm file cache): opening 160 ms; one wavelength slice
(50 scans × 800 points) 0.3–3 ms; MWA mean over 21 λ 4 ms; memory per displayed slice
0.16 MB, app peak 220 MB.

## Architecture

```
core/   auccore – Qt Core only, no GUI. File formats, lazy channel sources, processing,
        noise, folder watcher.
app/    Qt Quick application. ScanPlot (scene-graph item), AppController, QML UI.
tools/  aucinfo, aucgen, mwlgen
tests/  Qt Test unit tests for the core
```

**Data access.** `auc::openData()` groups files into `ChannelSource`s (one per cell/channel):
`.mwrs`/`.mw` one file per scan, `.auc` one file per wavelength. A wavelength slice reads one
radius row from each scan file (seek + read); recent slices are cached.

**Plot.** All scans are uploaded once as coloured line segments in data coordinates
(chunked at 65 534 vertices per scene-graph node) inside their own render layer. Zoom and
pan change only a transform matrix — verified with `QSG_RENDERER_DEBUG=upload`. The
LabVIEW viewer recoloured and redrew every plot through separate property-node calls.

**Processing** runs on a worker thread (`QtConcurrent`) — including the file reads of a
slice; option changes during a run are coalesced. Order: wavelength slice → dark current
→ absorbance → noise → scan selection → reverse → spike filter → offset → integration.

## Algorithms recovered from the LabVIEW program

| LabVIEW VI | Function | Definition |
|---|---|---|
| `sub_process_spectra` | `proc::absorbance` | A = −log₁₀(I/I₀) if I > 100 and I₀ > 100 counts, else 3.0 |
| `cal abs` | `proc::absorbanceScanByScan`, `proc::absorbanceMeanReference` | reference channel paired by scan, or mean of reference scans |
| `cal reference array` | `proc::meanScan` | point-wise mean of reference scans |
| `dark current subtract` | `proc::applyDarkCurrent` | ± dark-current value per wavelength |
| `cal offset` | `proc::subtractOffsetAt` | subtract value at radius index from *Threshold 1D Array*, rounded half-to-even |
| `Dont Show Spikes` | `proc::removeSpikes` | NI Median Filter, left rank 2, right rank 0, zero-padded |
| `sub_cal_start_w2t_cal` | `proc::estimateAcceleration` | ω²t = 3.2681·10⁷(e^{7.55·10⁻⁵ v̄} − 1); t = 49.49 + 1.21·10⁻³ v̄ + 2.64357·10⁻⁸ v̄² |
| `sub_average_wl` | `proc::binWavelengths` | 1 nm bins by rounded wavelength |
| `select scans` / `cut w2t Calculation` | `proc::selectScans` | range + every n-th |

## Noise files

TI (time-invariant, one value per radius point) and RI (radially invariant, one value
per scan) noise is loaded per channel in the *Noise* panel and subtracted after the
absorbance conversion.

- **UltraScan III noise XML** (`<NoiseData><noise type="ti|ri" minradius=… maxradius=…><d v=…/>…`).
  A TI vector from an edited range is placed by `minradius`; the range must match the
  radius grid within half a step.
- **Plain text / CSV**: one value per line, or two columns (radius, value for TI;
  scan/time, value for RI). `#` starts a comment.

## File formats

All MWL values are big-endian. Layouts were recovered from the LabVIEW program
(`sub_fileIO_header_reader`, `data reader bi2I32`, `file_io data reader`,
`sub_fileIO_read_xml_file`) and cross-checked with UltraScan III (`US_MwlData`).

**`.mwrs`** — one file per cell/channel/scan, run settings in `*.mwrs.xml`
(`settings_mwrs_experiment version`, `take_intensity`, sample descriptions).

| Offset | v1.1–1.4 | Size | Meaning |
|---|---|---|---|
| 0 | cell | u8 | |
| 1 | channel | char | A/S sample, B reference |
| 2 | scan | u16 | |
| 4 | rotor speed | u16 | rpm |
| 6 | set speed | u16 | rpm (not in v1.0) |
| 8 | temperature | u16 | °C × 10 |
| 10 | ω²t | f32 | rad²/s |
| 14 | time | u32 | s |
| 18 | points | u16 | |
| 20 | r start | u16 | cm × 1000 |
| 22 | r step | u16 | cm × 10000 |
| 24 | nλ | u16 | |
| 26 | λ | u16 × nλ | nm |
| … | readings | i32 × nλ × points | wavelength-major |

Scaling of readings: v1.0–1.2 ÷ 1000; v1.3 × 1; v1.4 ÷ 10000 for absorbance runs
(`take_intensity="N"`), × 1 for intensity runs.

**`.mw`** — one file per scan. v1.0/1.1: 100-byte header (magic, version, date, cell,
channel, scan, 64-char description, speed, T × 10, ω²t/10000 as u32, time, points,
r start/end × 1000, nλ), λ as i16 (nm × 10), readings i32. v1.2: 115-byte header adding
value size/type (u16/u32, i16/i32, f32), a dark-subtracted flag, start time, duration,
replicates and set speed, followed by λ, one dark-current value per wavelength, readings.

**`.auc`** (openAUC, magic `UCDA`): little-endian header, readings quantised to 16 bit
between stored min/max, optional stddev, interpolation bitmap (MSB first), trailing
CRC-32 (zlib polynomial, seeded with 0xFFFFFFFF). v4 stores λ as (λ − 180)·100, v5 as λ·10.

### To be confirmed with real files

- **`.mwrs` v1.0**: LabVIEW reads nλ as i32 and λ as u32 (nm × 10), UltraScan reads u16
  and u16 (nm). Both layouts are accepted; the one matching the file size is used.
- **`.mwrs` v1.1+ speeds**: LabVIEW reads rotor speed at offset 4 and set speed at 6,
  UltraScan the reverse. LabVIEW's order is used.
- **`.mw`** v1.0 vs v1.2 is detected from the file size.

## License

LGPL-3.0-or-later (see `LICENSE`; the LGPL incorporates the GPL-3.0 in `COPYING`).
File format handling follows the formats as used by UltraScan III (LGPL-3.0).
