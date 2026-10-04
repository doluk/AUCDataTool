# AUCDataTool

Cross-platform viewer for analytical ultracentrifugation (AUC) raw data — a C++/Qt 6
reimplementation of the LabVIEW **AUC-Viewer 2.2.2** (AG Cölfen, Universität Konstanz).

Targets Windows, macOS and Linux; the UI is Qt Quick so an Android build is possible.

![AUCDataTool with a multi-wavelength run: 16 channels, 600 wavelengths, absorbance against the reference channel](docs/screenshot.png)

## Status

| Area | State |
|---|---|
| openAUC `.auc` reader/writer (v4, v5, stddev, interpolation bitmap, CRC); multi-wavelength `.auc` sets grouped per cell/channel | done |
| Multi-wavelength `.mwrs` (v1.0–1.4, with run XML) and `.mw`/`.MW1`…`.MW8` (v1.0/1.1, v1.2 with dark current) | done — `.mwrs` 1.3 and `.mw` 1.0 validated on real runs (see below) |
| Beckman XL ASCII `.RA/.RI/.IP/.FI/.WA/.WI` (XL-A/XL-I, Optima and UltraScan exports); wavelength folders (`2A280`, `2A230`) combined; sample + reference intensity columns | done |
| Lazy wavelength slices (runs of 8 cells × 2 channels × 600+ λ never loaded completely) | done |
| Wavelength selection, multi-wavelength averaging (MWA) over a λ range | done |
| Intensity ↔ absorbance with a reference channel (scan by scan or mean of reference scans) | done |
| Dark current (`.mw` v1.2): toggle subtracted/not subtracted | done |
| GPU scan plot (all scans in one pass, zoom/pan without re-upload) | done |
| Curve styles: colour, width, dash pattern, markers, visibility per scan (click to select) and for all scans | done |
| Scan range / every n-th, reverse, spike filter, offset (point, baseline region) | done |
| Radial integration (∫A dr, ∫A·r dr) + integral-vs-time plot | done |
| TI/RI noise: load UltraScan noise XML or plain text | done |
| Live mode: follow a folder while the run is acquiring (new scans, new cells) | done |
| Spectra: all scans against λ at a radius (draggable), with absorbance/dark current | done |
| Run conditions: speed, temperature, ω²t per scan against time, ω²t line fit | done |
| 3D surface (Qt Graphs): radius × time (or ω²t) of the scans at one λ, or radius × λ of one scan | done |
| Export: CSV, Origin ASCII, Beckman XL, UltraScan III `.auc` — current view or a λ range | done |
| Print graph, save graph (PNG, PDF) | done |

## Build

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt ≥ 6.8 with Quick, QuickControls2 and
Concurrent. Optional: Qt Graphs (with Quick 3D) for the 3D surface view, Widgets + Print
Support for printing; without them the app builds without these features
(`-DAUC_WITH_GRAPHS=OFF`, `-DAUC_WITH_PRINT=OFF` to switch them off explicitly).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/app/AUCDataTool path/to/run/         # files or folders
```

With [vcpkg](https://vcpkg.io), Qt is installed from the `vcpkg.json` manifest when CMake
configures: set `VCPKG_ROOT` (the toolchain file is then picked up automatically) or pass
`-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. The first configure
builds Qt and takes a while; the packages land in `build/vcpkg_installed`. A build tree that
was configured in classic mode keeps `VCPKG_MANIFEST_MODE=OFF` – delete its `CMakeCache.txt`
(CLion: *Reset Cache and Reload Project*).

Options: `-DAUC_BUILD_APP=OFF` builds only the core library, tools and tests (needs only Qt Core).

### Installers

`cpack --config build/CPackConfig.cmake -C Release -B build/package` bundles Qt and produces
an NSIS installer (`.exe`, Windows), a disk image (`.dmg`, macOS) or `.deb`, `.rpm` (both install
to `/opt/AUCDataTool`) and a relocatable `.tar.gz` (Linux; Linux deployment needs Qt ≥ 6.5).
An Arch Linux package built against system Qt comes from `cd packaging/arch && makepkg -s`.
CI builds all of these for every push as
workflow artifacts and attaches them to a GitHub release when a `v*` tag is pushed.

### Tools

- `aucinfo PATH… [--bench]` — summary of every channel in files/folders (`.auc`, `.mwrs`,
  `.mw`, XL); `--bench` times opening and wavelength-slice reads.
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
            [--export csv|origin|beckman|us3=PATH [--export-wavelengths FROM:TO[:STEP]]]
            [--view scans|run|surface] [--screenshot out.png | --save-graph out.pdf] [--screenshot-delay ms] [--benchmark N]
```
`--watch` opens a folder in live mode. `--set` applies view/processing options, e.g.
`--set wavelengthIndex=60 --set displayMode=0 --set integrate=true --set showSpectrum=true`.
`--export` writes the processed data of the first channel (with all `--set` options) and
quits, e.g. absorbance at 260–280 nm, every 2nd wavelength, as UltraScan files:
`AUCDataTool run/ --export us3=out/ --export-wavelengths 260:280:2`.

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
- **Spectra.** *Show spectra at a radius* adds a plot of all selected scans against
  wavelength, averaged over r ± Δr. Drag the green marker in the scan plot to move the radius
  and the orange marker in the spectrum plot to change the wavelength. Absorbance, the
  reference mode, dark current and the scan selection apply as in the scan plot. Spectra
  read every scan file completely (a 350-scan × 202-λ run: about 0.2 s, then cached).
- **3D surface** (tab *3D surface*, Qt Graphs): *radius × time* — the processed scans at
  the current wavelength as one surface, intensity or absorbance, time in minutes or as
  ω²t (any data, also single-wavelength) — or *radius × wavelength* of one scan (slider,
  multi-wavelength data). Drag to
  rotate, wheel to zoom, click for the value. The grid is reduced to at most 400 × 300 points.
- Settings (wavelength, reference, display) are remembered per channel.

## Run conditions

The *Run conditions* tab plots, for the selected channel, the measured and set rotor speed,
the temperature and ω²t of every scan against time (from the scan headers). A straight line
ω²t = ω²·(t − t₀) is fitted to the scans within 0.5 % of the median speed; its slope gives
the effective speed and t₀ the time lost while accelerating. A deviation of the points from
the line shows speed changes, and a wrong ω²t scaling shows up as an effective speed far
from the measured one.

*Export…* writes the processed data as shown (reference, scan selection, noise, spike
filter, offset), for the current wavelength (or MWA range) or every n-th wavelength of a range:

| Format | Output |
|---|---|
| CSV | one file: radius column + one column per scan (Excel) |
| Origin ASCII | tab-separated, header rows *Long Name*, *Units*, *Comments* (scan, time) |
| Beckman XL | one file per scan, `2A280/A00012.RA2` — the XL/UltraScan export layout |
| UltraScan III | `run.RA.2.A.280.auc` (openAUC) per cell/channel/wavelength; NaN points flagged as interpolated |

Several wavelengths go to separate files (`name_280nm.csv`) or folders. *Print…* (Ctrl+P)
and *Save graph…* (PNG, JPEG, or PDF page with the run information) take the visible plots
or the 3D surface.

**Performance** (synthetic run, 8 cells × 2 channels × 600 λ × 50 scans × 800 points =
1.5 GB, 800 files; Linux, warm file cache): opening 160 ms; one wavelength slice
(50 scans × 800 points) 0.3–3 ms; MWA mean over 21 λ 4 ms; memory per displayed slice
0.16 MB, app peak 220 MB.

## Architecture

```
core/   auccore – Qt Core only, no GUI. File formats (openAUC, MWL, XL), lazy channel
        sources, processing, noise, exporters, folder watcher.
app/    Qt Quick application. ScanPlot (scene-graph item), AppController, QML UI,
        SurfaceFeeder (Qt Graphs surface).
tools/  aucinfo, aucgen, mwlgen
tests/  Qt Test unit tests for the core
```

**Data access.** `auc::openData()` groups files into `ChannelSource`s (one per cell/channel):
`.mwrs`/`.mw` and XL one file per scan, `.auc` one file per wavelength. A wavelength slice
reads one radius row from each scan file (seek + read; XL text files are parsed and
interpolated); recent slices are cached. Spectra and the radius × λ surface read whole scans.

**Plot.** All scans are uploaded once as coloured line segments in data coordinates
(chunked at 65 534 vertices per scene-graph node) inside their own render layer. Zoom and
pan change only a transform matrix — verified with `QSG_RENDERER_DEBUG=upload`. The
LabVIEW viewer recoloured and redrew every plot through separate property-node calls.

**Processing** runs on a worker thread (`QtConcurrent`) — including the file reads of a
slice; option changes during a run are coalesced. Order: wavelength slice → dark current
→ absorbance → noise → scan selection → reverse → spike filter → offset → integration;
spectra and the surface are computed in the same pass when shown.

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

**XL ASCII** (`.RA1`, `.RI2`, `.IP3`, `.FI1`, `.WA1`, `.WI1`; one file per scan): line 1
description (`cm/pixel: …, description` for interference), line 2
`type cell T rpm seconds ω²t λ replicates`, then `x value [third column]`. The header letter
(R, I, P, F, W) decides the type — some exports use `.RI` for absorbance. For intensity
files of double-sector cells the second column is the sample and the third the reference
intensity (A = log₁₀(I_ref/I_sample); checked against the matching `.ra2` files), so they
become two channels (A and B, or the file's letter and R) and absorbance works as for MWL
data. XL radii are not equidistant and differ between scans; each channel uses the
equidistant grid of its first scan and interpolates every scan onto it (NaN outside a
scan's range). The channel letter comes from the file name (`A00012.RA2`) or the folder
(`2A280`); wavelength folders of one run are grouped into one channel.

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
| 4 | set speed | u16 | rpm (v1.0: rotor speed) |
| 6 | rotor speed | u16 | rpm (not in v1.0) |
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

**`.mw`** — one file per scan, extension `.mw` or `.MW<cell>`. v1.0/1.1: 100-byte header
(magic, version, date, cell, channel, scan, 64-char description, speed, T × 10, ω²t as u32,
time, points, r start/end × 1000, nλ), λ as i16 (nm × 10), readings i32. ω²t is stored
÷10000 according to the LabVIEW reader but ÷1000 by later acquisition versions; the factor
that keeps ω²t ≤ ω²·t is used. v1.2: 115-byte header adding
value size/type (u16/u32, i16/i32, f32), a dark-subtracted flag, start time, duration,
replicates and set speed, followed by λ, one dark-current value per wavelength, readings.

**`.auc`** (openAUC, magic `UCDA`): little-endian header, readings quantised to 16 bit
between stored min/max, optional stddev, interpolation bitmap (MSB first), trailing
CRC-32 (zlib polynomial, seeded with 0xFFFFFFFF). v4 stores λ as (λ − 180)·100, v5 as λ·10.

### Validation with real files

- **`.mwrs` speeds**: the value at offset 4 is constant over all scans in real runs
  (60000, 53000, 50000 rpm) while the one at offset 6 varies by a few rpm, so offset 4 is
  the set speed and offset 6 the measured rotor speed — UltraScan's order. The LabVIEW
  viewer reads them the other way round (its XL export writes the set speed as rpm).
  Visible in the *Run conditions* tab.
- **`.mwrs` v1.3** (run 1844, 202 λ): readings, ω²t and time agree exactly with the XL `.RI2` files the LabVIEW viewer exported from
  the same run (`1844_Cell2_500nm_Intensity`).
- **`.mw` v1.0** (`A001.MW3`): header, λ table and time agree with the `.mwrs` files of the
  same run; ω²t is stored ÷1000 (see above).
- **Scaling of "1.3" intensity runs**: some runs whose XML says 1.3 store intensities
  ×10000 (like v1.4 absorbance). Detector counts stay far below 5·10⁶, so a mid-spectrum
  row maximum above that selects ÷10000; the format column then shows "(÷10000)".
- Still unconfirmed: `.mwrs` v1.0 (LabVIEW reads nλ as i32 and λ as u32 nm × 10,
  UltraScan u16/u16 nm — the layout matching the file size is used) and `.mw` v1.2.

## License

LGPL-3.0-or-later (see `LICENSE`; the LGPL incorporates the GPL-3.0 in `COPYING`).
File format handling follows the formats as used by UltraScan III (LGPL-3.0).
