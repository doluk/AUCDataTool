#pragma once

#include "auc/Dataset.h"

#include <vector>

/// Systematic noise in sedimentation-velocity data.
///
/// A residual matrix e_ij (scan i, radius point j), e.g. data minus a fitted model, is
/// decomposed by least squares into
///   e_ij ≈ b_j + β_i
/// with b_j the time-invariant (TI) noise – a fixed radial pattern from optics and cell
/// windows – and β_i the radially invariant (RI) noise – a per-scan baseline offset.
/// For a complete matrix the least-squares solution is closed-form:
///   β_i = mean_j(e_ij) − mean_ij(e_ij),   b_j = mean_i(e_ij)
/// (the split of the grand mean is arbitrary; here it goes to b, so Σβ_i = 0).
/// Subtracting both from the data removes the noise; this is the same model UltraScan fits
/// jointly with its 2DSA/GA analyses.
namespace auc::noise {

enum Components { TimeInvariant = 1, RadiallyInvariant = 2, Both = 3 };

struct Result {
    std::vector<double> ti;  ///< b_j, one per radius point (empty if not requested)
    std::vector<double> ri;  ///< β_i, one per scan (empty if not requested)
    double rmsBefore = 0.0;  ///< RMS of the residuals
    double rmsAfter = 0.0;   ///< RMS after removing the fitted noise
};

/// Fits the requested noise components to `residuals` (scan values are the residuals;
/// all scans must have the same point count).
Result fit(const Dataset& residuals, Components which = Both);

/// Fits noise to (data − model), where `model` has the same shape as `data`.
Result fitToModel(const Dataset& data, const Dataset& model, Components which = Both);

/// Subtracts fitted noise from `d` in place.
void subtract(Dataset& d, const Result& noise);

}  // namespace auc::noise
