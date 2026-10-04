#pragma once

#include "auc/Dataset.h"

#include <span>
#include <vector>

/// Numerical processing of AUC scans.
///
/// Functions marked [AUC-Viewer] reproduce the behaviour of the LabVIEW AUC-Viewer 2.2.2
/// (sub-VI named in brackets), recovered from its block diagrams. Edge-case semantics
/// (rounding, out-of-range handling) follow the LabVIEW primitives used there.
namespace auc::proc {

// ---------------------------------------------------------------------------------------
// Index helpers
// ---------------------------------------------------------------------------------------

/// [AUC-Viewer, LabVIEW "Threshold 1D Array"] Fractional index at which ascending `x`
/// reaches `value`: the first pair x[i] < value ≤ x[i+1] gives i + (value−x[i])/(x[i+1]−x[i]).
/// Returns 0 if value ≤ x[0] and n−1 if no pair is found. Requires a non-empty array.
double thresholdIndex(std::span<const double> x, double value);

/// thresholdIndex rounded like LabVIEW "To Long Integer" (round half to even).
std::size_t nearestIndex(std::span<const double> x, double value);

// ---------------------------------------------------------------------------------------
// Intensity → absorbance
// ---------------------------------------------------------------------------------------

struct AbsorbanceParams {
    float minCounts = 100.f;   ///< below this in sample or reference, the point is invalid
    float invalidValue = 3.f;  ///< absorbance assigned to invalid points
};

/// [AUC-Viewer: sub_process_spectra] A = −log10(I / I0) where I > minCounts and
/// I0 > minCounts, otherwise `invalidValue`. `out` must have the size of `sample`.
void absorbance(std::span<const float> sample, std::span<const float> reference, std::span<float> out,
                const AbsorbanceParams& p = {});

/// [AUC-Viewer: cal reference array] Point-wise mean of the selected scans' values.
/// `scanIndices` empty → all scans.
std::vector<float> meanScan(const Dataset& d, std::span<const std::size_t> scanIndices = {});

/// Converts an intensity dataset into absorbance against a reference intensity profile
/// (e.g. meanScan of the reference channel or of reference scans).
Dataset toAbsorbance(const Dataset& intensity, std::span<const float> reference, const AbsorbanceParams& p = {});

// ---------------------------------------------------------------------------------------
// Corrections
// ---------------------------------------------------------------------------------------

/// [AUC-Viewer: dark current subtract] Subtracts (subtract=true) or adds back
/// (subtract=false) a dark-current value per point.
void applyDarkCurrent(std::span<float> values, std::span<const float> dark, bool subtract);

/// [AUC-Viewer: cal offset] Shifts every scan so its value at radius `r` becomes zero.
/// The reference point is the radius index nearest to `r` (LabVIEW rounding).
void subtractOffsetAt(Dataset& d, double r);

/// Shifts every scan by the mean of its values in [r1, r2] (baseline region). Not in the
/// LabVIEW program; more robust against noise than a single-point offset.
void subtractBaselineRegion(Dataset& d, double r1, double r2);

/// [AUC-Viewer: Dont Show Spikes → NI "Median Filter"] y_i = median(x_{i−left} … x_{i+right});
/// elements outside the array count as 0 (NI semantics). AUC-Viewer uses left=2, right=0.
std::vector<float> medianFilter(std::span<const float> x, int leftRank, int rightRank);

/// Spike removal as configured in AUC-Viewer (median, left rank 2, right rank 0), in place.
void removeSpikes(Dataset& d);

/// Reverses the radius axis and values of every scan (AUC-Viewer "Reverse Scans").
void reverseRadius(Dataset& d);

// ---------------------------------------------------------------------------------------
// Scan selection
// ---------------------------------------------------------------------------------------

/// [AUC-Viewer: select scans / cut w2t Calculation] Indices first..last (0-based,
/// inclusive, clamped) taking every `everyNth` scan.
std::vector<std::size_t> selectScans(std::size_t scanCount, std::size_t first, std::size_t last,
                                     std::size_t everyNth = 1);

// ---------------------------------------------------------------------------------------
// Run quantities
// ---------------------------------------------------------------------------------------

struct AccelerationEstimate {
    double meanSpeed = 0.0;  ///< rpm
    double omega2t = 0.0;    ///< rad²/s accumulated during acceleration
    double time = 0.0;       ///< s
};

/// [AUC-Viewer: sub_cal_start_w2t_cal] Empirical ω²t and time accumulated while the
/// rotor accelerates to the mean of the measured speeds v̄:
///   ω²t = 3.2681·10⁷ · (exp(7.55·10⁻⁵ · v̄) − 1)
///   t   = 49.49 + 1.21·10⁻³ · v̄ + 2.64357·10⁻⁸ · v̄²
/// The constants are instrument calibration values from the LabVIEW program.
AccelerationEstimate estimateAcceleration(std::span<const double> measuredSpeeds);

/// ω²t from a speed profile by trapezoidal integration of ω(t)² (ω = 2π·rpm/60).
double integrateOmega2t(std::span<const double> seconds, std::span<const double> rpm);

// ---------------------------------------------------------------------------------------
// Multi-wavelength
// ---------------------------------------------------------------------------------------

struct WavelengthBins {
    std::vector<std::size_t> start;   ///< first wavelength index of each bin
    std::vector<std::size_t> count;   ///< number of wavelengths in each bin
    std::vector<float> wavelength;    ///< mean wavelength of each bin
};

/// [AUC-Viewer: sub_average_wl, "wl schrittweite 1nm"] Groups consecutive wavelengths
/// that round to the same integer nanometre (LabVIEW unsigned conversion, half to even).
WavelengthBins binWavelengths(std::span<const float> wavelengths);

/// Averages wavelength-resolved data (rows = wavelengths, each row `pointCount` values)
/// over the bins. Returns bins.start.size() rows.
std::vector<float> averageOverBins(std::span<const float> rows, std::size_t pointCount,
                                   const WavelengthBins& bins);

// ---------------------------------------------------------------------------------------
// Radial integration
// ---------------------------------------------------------------------------------------

/// ∫ y(r) dr over [r1, r2] by the trapezoidal rule, with linear interpolation at the
/// limits. With `radialWeight`, integrates y(r)·r dr (sector-shaped cell geometry).
double integrateRadial(std::span<const double> r, std::span<const float> y, double r1, double r2,
                       bool radialWeight = false);

/// integrateRadial for every scan of a dataset.
std::vector<double> integrateScans(const Dataset& d, double r1, double r2, bool radialWeight = false);

}  // namespace auc::proc
