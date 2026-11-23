// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

/// Scan timing of a run whose cells are scanned one after another (Beckman XL-A/XL-I
/// absorbance and intensity optics): the instrument visits the cells in a fixed cycle,
/// one scan each. From the scan headers this finds the cycle, the interval from every scan
/// to the next one (any cell), the interval between consecutive scans of the same cell and
/// the scans the instrument skipped (a cell missing from a cycle).
namespace auc::timing {

/// One scan: elapsed run time and the scan number from the file name.
struct ScanTime {
    double seconds = 0.0;
    int number = 0;
};

/// What the instrument scans in one go: a cell (channel) at one wavelength (XL, Optima/.auc
/// scan every wavelength separately) or at all wavelengths at once (multi-wavelength
/// detector, .mw/.mwrs: `wavelength`…`wavelengthTo`).
struct Slot {
    int cell = 0;
    double wavelength = 0.0;      ///< nm (first wavelength if all are recorded at once)
    std::vector<ScanTime> scans;  ///< any order; analyze() sorts them by time
    char channel = 'A';
    double wavelengthTo = 0.0;    ///< nm; > wavelength: the range recorded at once
    /// Set by analyze(): the channels recorded in these scans, ascending ("A", or "AB" when
    /// channel B has the same scan times and was merged into this slot).
    std::string channels;
};

/// One scan of the run, in time order.
struct Event {
    double seconds = 0.0;
    int slot = 0;    ///< index into Analysis::cellSlots
    int number = 0;  ///< scan number
};

/// All consecutive scan pairs going from slot `from` to slot `to`.
struct Transition {
    int from = 0, to = 0;
    /// Slots of the cycle passed over between the two (empty: the regular successor).
    std::vector<int> skipped;
    bool regular = true;            ///< `to` follows `from` in the cycle (nothing skipped)
    std::vector<double> intervals;  ///< s
    std::vector<double> at;         ///< time of the first scan of each pair, s
    double median = 0.0;
};

/// The instrument went from `from` to `to`, passing over `skipped` (slots of the cycle).
struct Skip {
    double seconds = 0.0;  ///< time of the scan of `from`
    int from = 0, to = 0;
    std::vector<int> skipped;
};

struct Analysis {
    /// In acquisition order (the dominant cycle, starting with the slot scanned first);
    /// slots outside the cycle follow in order of their first scan. Scans sorted by time,
    /// duplicates removed.
    std::vector<Slot> cellSlots;
    std::vector<Event> events;       ///< all scans by time
    std::vector<double> next;        ///< per event: interval to the next scan (any slot), NaN for the last
    std::vector<int> successor;      ///< per slot: the slot usually scanned next, −1 if none
    std::vector<Transition> transitions;  ///< regular ones in cycle order, then the others by count
    std::vector<Skip> skips;              ///< by time
    std::vector<double> slotMedian;  ///< per slot: median interval between its consecutive scans (NaN if < 2 scans)
    bool integerSeconds = true;      ///< all times are whole seconds (XL: ±0.5 s quantisation)
};

/// Analyses the scans of the slots. A slot with the same cell, wavelength and scan times as
/// an earlier one is merged into it (Slot::channels; e.g. the sample and reference channel
/// of the same intensity files, or both channels read in the same sweep of a
/// multi-wavelength detector); channels of a cell scanned at different times stay separate
/// slots.
Analysis analyze(std::vector<Slot> cellSlots);

/// Median of `v` (NaN if empty).
double median(std::vector<double> v);

}  // namespace auc::timing
