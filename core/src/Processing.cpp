// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Processing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <utility>

namespace auc::proc {

double thresholdIndex(std::span<const double> x, double value)
{
    assert(!x.empty());
    if (value <= x.front()) return 0.0;
    for (std::size_t i = 0; i + 1 < x.size(); ++i) {
        if (value > x[i] && value <= x[i + 1]) {
            const double dx = x[i + 1] - x[i];
            return dx > 0 ? double(i) + (value - x[i]) / dx : double(i + 1);
        }
    }
    return double(x.size() - 1);
}

std::size_t nearestIndex(std::span<const double> x, double value)
{
    // std::nearbyint uses the current rounding mode, which defaults to round-half-even.
    const double idx = std::nearbyint(thresholdIndex(x, value));
    return std::min<std::size_t>(std::size_t(std::max(idx, 0.0)), x.size() - 1);
}

void absorbance(std::span<const float> sample, std::span<const float> reference, std::span<float> out,
                const AbsorbanceParams& p)
{
    const std::size_t n = std::min({sample.size(), reference.size(), out.size()});
    for (std::size_t j = 0; j < n; ++j) {
        const float i = sample[j], i0 = reference[j];
        out[j] = (i > p.minCounts && i0 > p.minCounts) ? -std::log10(i / i0) : p.invalidValue;
    }
    for (std::size_t j = n; j < out.size(); ++j) out[j] = p.invalidValue;
}

std::vector<float> meanScan(const Dataset& d, std::span<const std::size_t> scanIndices)
{
    std::vector<double> acc(d.pointCount(), 0.0);
    std::size_t used = 0;
    auto add = [&](const Scan& s) {
        const std::size_t n = std::min(acc.size(), s.values.size());
        for (std::size_t j = 0; j < n; ++j) acc[j] += s.values[j];
        ++used;
    };
    if (scanIndices.empty()) {
        for (const auto& s : d.scans) add(s);
    } else {
        for (std::size_t i : scanIndices)
            if (i < d.scans.size()) add(d.scans[i]);
    }
    std::vector<float> out(acc.size(), 0.f);
    if (used)
        for (std::size_t j = 0; j < acc.size(); ++j) out[j] = float(acc[j] / double(used));
    return out;
}

Dataset toAbsorbance(const Dataset& intensity, std::span<const float> reference, const AbsorbanceParams& p)
{
    Dataset out = intensity;
    out.type = DataType::RadialAbsorbance;
    for (auto& s : out.scans) {
        std::vector<float> a(s.values.size());
        absorbance(s.values, reference, a, p);
        s.values = std::move(a);
        s.stddev.clear();  // intensity deviations do not carry over to absorbance
    }
    return out;
}

std::string absorbanceScanByScan(Dataset& sample, const Dataset& reference, const AbsorbanceParams& p)
{
    if (reference.pointCount() != sample.pointCount())
        return "reference has " + std::to_string(reference.pointCount()) + " radius points, sample "
             + std::to_string(sample.pointCount());
    const std::size_t n = std::min(sample.scans.size(), reference.scans.size());
    sample.scans.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto& s = sample.scans[i];
        std::vector<float> a(s.values.size());
        absorbance(s.values, reference.scans[i].values, a, p);
        s.values = std::move(a);
        s.stddev.clear();
    }
    sample.type = DataType::RadialAbsorbance;
    return {};
}

std::string absorbanceMeanReference(Dataset& sample, const Dataset& reference, std::size_t first, std::size_t last,
                                    const AbsorbanceParams& p)
{
    if (reference.pointCount() != sample.pointCount())
        return "reference has " + std::to_string(reference.pointCount()) + " radius points, sample "
             + std::to_string(sample.pointCount());
    const auto sel = selectScans(reference.scanCount(), first, last, 1);
    if (sel.empty()) return "the reference scan range is empty";
    const std::vector<float> i0 = meanScan(reference, sel);
    for (auto& s : sample.scans) {
        std::vector<float> a(s.values.size());
        absorbance(s.values, i0, a, p);
        s.values = std::move(a);
        s.stddev.clear();
    }
    sample.type = DataType::RadialAbsorbance;
    return {};
}

double windowMean(std::span<const float> values, std::span<const double> radius, double r1, double r2)
{
    if (r2 < r1) std::swap(r1, r2);
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t j = 0; j < radius.size() && j < values.size(); ++j) {
        if (radius[j] >= r1 && radius[j] <= r2) {
            sum += values[j];
            ++n;
        }
    }
    return n ? sum / double(n) : std::numeric_limits<double>::quiet_NaN();
}

std::string absorbanceRadialReference(Dataset& sample, const Dataset& reference, double r1, double r2,
                                      const AbsorbanceParams& p)
{
    const std::size_t n = std::min(sample.scans.size(), reference.scans.size());
    std::vector<float> i0(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = windowMean(reference.scans[i].values, reference.radius, r1, r2);
        if (std::isnan(m)) return "no radius points in the reference region";
        i0[i] = float(m);
    }
    sample.scans.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto& s = sample.scans[i];
        const std::vector<float> ref(s.values.size(), i0[i]);
        std::vector<float> a(s.values.size());
        absorbance(s.values, ref, a, p);
        s.values = std::move(a);
        s.stddev.clear();
    }
    sample.type = DataType::RadialAbsorbance;
    return {};
}

void applyDarkCurrent(std::span<float> values, std::span<const float> dark, bool subtract)
{
    const std::size_t n = std::min(values.size(), dark.size());
    const float sign = subtract ? -1.f : 1.f;
    for (std::size_t j = 0; j < n; ++j) values[j] += sign * dark[j];
}

void subtractOffsetAt(Dataset& d, double r)
{
    if (d.radius.empty()) return;
    const std::size_t k = nearestIndex(d.radius, r);
    for (auto& s : d.scans) {
        if (k >= s.values.size()) continue;
        const float off = s.values[k];
        for (float& v : s.values) v -= off;
    }
}

void subtractBaselineRegion(Dataset& d, double r1, double r2)
{
    if (r2 < r1) std::swap(r1, r2);
    for (auto& s : d.scans) {
        double sum = 0.0;
        std::size_t n = 0;
        for (std::size_t j = 0; j < d.radius.size() && j < s.values.size(); ++j) {
            if (d.radius[j] >= r1 && d.radius[j] <= r2) {
                sum += s.values[j];
                ++n;
            }
        }
        if (!n) continue;
        const float off = float(sum / double(n));
        for (float& v : s.values) v -= off;
    }
}

std::vector<float> medianFilter(std::span<const float> x, int leftRank, int rightRank)
{
    leftRank = std::max(leftRank, 0);
    rightRank = std::max(rightRank, 0);
    const std::ptrdiff_t n = std::ptrdiff_t(x.size());
    std::vector<float> out(x.size());
    if (leftRank + rightRank == 2) {
        // Three-point window (AUC-Viewer: left 2, right 0): median by min/max, ~10× faster
        // than the general path for runs with many scans.
        auto at = [&](std::ptrdiff_t k) { return (k >= 0 && k < n) ? x[std::size_t(k)] : 0.f; };
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            const float a = at(i - leftRank), b = at(i - leftRank + 1), c = at(i - leftRank + 2);
            out[std::size_t(i)] = std::max(std::min(a, b), std::min(std::max(a, b), c));
        }
        return out;
    }
    std::vector<float> window(std::size_t(leftRank + rightRank + 1));
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        std::size_t w = 0;
        for (std::ptrdiff_t k = i - leftRank; k <= i + rightRank; ++k)
            window[w++] = (k >= 0 && k < n) ? x[std::size_t(k)] : 0.f;
        // Window length is odd only when left+right is even; for even lengths NI returns
        // the mean of the two middle elements.
        const std::size_t m = window.size() / 2;
        std::nth_element(window.begin(), window.begin() + std::ptrdiff_t(m), window.end());
        float med = window[m];
        if (window.size() % 2 == 0) {
            const float lower = *std::max_element(window.begin(), window.begin() + std::ptrdiff_t(m));
            med = 0.5f * (med + lower);
        }
        out[std::size_t(i)] = med;
    }
    return out;
}

void removeSpikes(Dataset& d)
{
    for (auto& s : d.scans) s.values = medianFilter(s.values, 2, 0);
}

void reverseRadius(Dataset& d)
{
    std::reverse(d.radius.begin(), d.radius.end());
    for (auto& s : d.scans) {
        std::reverse(s.values.begin(), s.values.end());
        std::reverse(s.stddev.begin(), s.stddev.end());
        s.interpolated.clear();  // bitmap no longer matches; recompute if needed
    }
}

namespace {

/// Point indices [lo, hi] of the radial window [r1, r2] (whole range if r2 ≤ r1).
std::pair<std::size_t, std::size_t> windowIndices(std::span<const double> radius, double r1, double r2)
{
    if (radius.empty()) return {0, 0};
    if (!(r2 > r1)) return {0, radius.size() - 1};
    std::size_t lo = 0, hi = radius.size() - 1;
    while (lo < hi && radius[lo] < r1) ++lo;
    while (hi > lo && radius[hi] > r2) --hi;
    return {lo, hi};
}

}  // namespace

std::vector<int> fringeCorrection(const Dataset& d, std::size_t k, double threshold)
{
    std::vector<int> c(d.scans.size(), 0);
    const std::size_t n = d.pointCount();
    if (n == 0 || c.empty()) return c;
    k = std::min(k, n - 1);
    const std::size_t a = k >= 2 ? k - 2 : 0, b = std::min(k + 2, n - 1);
    auto level = [&](const Scan& s) {
        double sum = 0.0;
        std::size_t m = 0;
        for (std::size_t j = a; j <= b && j < s.values.size(); ++j)
            if (std::isfinite(s.values[j])) {
                sum += s.values[j];
                ++m;
            }
        return m ? sum / double(m) : std::numeric_limits<double>::quiet_NaN();
    };
    threshold = std::max(threshold, 0.0);
    double prev = level(d.scans[0]);
    for (std::size_t i = 1; i < d.scans.size(); ++i) {
        const double cur = level(d.scans[i]);
        const double dv = cur - prev;
        int step = 0;
        if (std::isfinite(dv) && std::abs(dv) > threshold) {
            step = int(std::lround(dv));
            if (step == 0) step = dv > 0 ? 1 : -1;
        }
        c[i] = c[i - 1] + step;
        if (std::isfinite(cur)) prev = cur;
    }
    return c;
}

std::size_t removeFringeJumps(Dataset& d, double rAlign, const FringeJumpParams& p)
{
    if (d.radius.empty()) return 0;
    const std::vector<int> c = fringeCorrection(d, nearestIndex(d.radius, rAlign), p.threshold);
    std::size_t jumps = 0;
    for (std::size_t i = 0; i < d.scans.size(); ++i) {
        if (i && c[i] != c[i - 1]) ++jumps;
        for (float& v : d.scans[i].values) v -= float(c[i]);
    }
    return jumps;
}

double crossingRadius(std::span<const double> radius, std::span<const float> a, std::span<const float> b, double near,
                      double r1, double r2)
{
    const std::size_t n = std::min({radius.size(), a.size(), b.size()});
    double best = std::numeric_limits<double>::quiet_NaN();
    if (n < 2) return best;
    const auto [lo, hi] = windowIndices(radius.first(n), r1, r2);
    auto consider = [&](double r) {
        if (std::isnan(best) || std::abs(r - near) < std::abs(best - near)) best = r;
    };
    for (std::size_t j = lo; j < hi; ++j) {
        const double d0 = double(a[j]) - double(b[j]), d1 = double(a[j + 1]) - double(b[j + 1]);
        if (!std::isfinite(d0) || !std::isfinite(d1)) continue;
        if (d0 == 0.0) consider(radius[j]);
        else if ((d0 < 0.0) != (d1 < 0.0) && d1 != 0.0) consider(radius[j] + d0 / (d0 - d1) * (radius[j + 1] - radius[j]));
    }
    if (const double dl = double(a[hi]) - double(b[hi]); dl == 0.0) consider(radius[hi]);
    return best;
}

FringeAlignment analyzeFringeAlignment(const Dataset& d, const FringeJumpParams& p)
{
    FringeAlignment out;
    const std::size_t n = d.pointCount();
    if (d.scans.size() < 2 || n < 3) return out;
    for (const auto& s : d.scans)
        if (s.values.size() < n) return out;
    const auto [lo, hi] = windowIndices(d.radius, p.r1, p.r2);
    if (hi < lo + 2) return out;

    // T(r): crossing of the first and last scan nearest r after the correction aligned at r
    // (the first scan is never shifted).
    const auto& first = d.scans.front().values;
    const auto& last = d.scans.back().values;
    // Only a clear crossing counts: away from it the scans separate by more than the threshold,
    // with opposite signs on the two sides. Where they lie on top of each other (the air–air
    // region, which the jitter correction sets to 0 in every scan) noise crosses them anywhere.
    std::vector<float> lk(n);
    auto crossingFor = [&](std::size_t k, double near) {
        const float shift = float(fringeCorrection(d, k, p.threshold).back());
        for (std::size_t j = 0; j < n; ++j) lk[j] = last[j] - shift;
        const double t = crossingRadius(d.radius, first, lk, near, p.r1, p.r2);
        if (!std::isfinite(t)) return t;
        const std::size_t c = std::clamp(nearestIndex(d.radius, t), lo, hi);
        auto side = [&](std::ptrdiff_t step) {
            for (std::ptrdiff_t j = std::ptrdiff_t(c); j >= std::ptrdiff_t(lo) && j <= std::ptrdiff_t(hi); j += step) {
                const double diff = double(lk[std::size_t(j)]) - double(first[std::size_t(j)]);
                if (std::abs(diff) > p.threshold) return diff > 0 ? 1 : -1;
            }
            return 0;
        };
        const int left = side(-1), right = side(1);
        return left != 0 && right != 0 && left != right ? t : std::numeric_limits<double>::quiet_NaN();
    };

    const std::size_t stride = std::max<std::size_t>(1, (hi - lo + 1) / 400);
    std::vector<std::size_t> trialIdx;
    std::vector<double> trialT;
    for (std::size_t k = lo; k <= hi; k += stride) {
        trialIdx.push_back(k);
        trialT.push_back(crossingFor(k, d.radius[k]));
    }
    std::vector<double> finite;
    for (double t : trialT)
        if (std::isfinite(t)) finite.push_back(t);
    if (finite.empty()) return out;
    std::nth_element(finite.begin(), finite.begin() + std::ptrdiff_t(finite.size() / 2), finite.end());
    out.centre = finite[finite.size() / 2];

    // Route A: expand from the centre while the integer correction of all scans stays the same.
    {
        const std::size_t kc = std::clamp(nearestIndex(d.radius, out.centre), lo, hi);
        const std::vector<int> cc = fringeCorrection(d, kc, p.threshold);
        std::size_t a = kc, b = kc;
        while (a > lo && fringeCorrection(d, a - 1, p.threshold) == cc) --a;
        while (b < hi && fringeCorrection(d, b + 1, p.threshold) == cc) ++b;
        out.stableLo = d.radius[a];
        out.stableHi = d.radius[b];
        const double t = crossingFor(kc, out.centre);
        if (b >= a + 2 && std::isfinite(t) && t >= out.stableLo && t <= out.stableHi) {
            out.stableValid = true;
            out.stableAlign = t;
        }
    }

    auto jumpsAt = [&](std::size_t k) {
        const std::vector<int> c = fringeCorrection(d, k, p.threshold);
        std::size_t count = 0;
        for (std::size_t i = 1; i < c.size(); ++i) count += c[i] != c[i - 1];
        return count;
    };

    // Route B: fixed point T(r*) ≈ r*, stable against the neighbouring trials. Far from the
    // isoconcentration point the real change between scans can be rounded away as "jumps" and
    // give a fixed point as well; at the true one the profile hardly changes, so of several
    // fixed points the one needing the fewest jumps wins.
    {
        const double dr = (d.radius[hi] - d.radius[lo]) / double(hi - lo);
        const double tol = std::abs(dr) * double(std::max<std::size_t>(3, 2 * stride));
        std::size_t bestJumps = SIZE_MAX;
        double bestSpread = std::numeric_limits<double>::infinity(), bestErr = bestSpread;
        for (std::size_t m = 0; m < trialIdx.size(); ++m) {
            const double t = trialT[m];
            if (!std::isfinite(t)) continue;
            const double err = std::abs(t - d.radius[trialIdx[m]]);
            if (err > tol) continue;
            double spread = 0.0;
            for (std::size_t q = m >= 2 ? m - 2 : 0; q <= m + 2 && q < trialIdx.size(); ++q)
                spread = std::max(spread, std::isfinite(trialT[q]) ? std::abs(trialT[q] - t) : std::numeric_limits<double>::infinity());
            if (spread > tol) continue;
            const std::size_t jumps = jumpsAt(trialIdx[m]);
            if (jumps < bestJumps || (jumps == bestJumps && (spread < bestSpread || (spread == bestSpread && err < bestErr)))) {
                bestJumps = jumps;
                bestSpread = spread;
                bestErr = err;
                out.selfConsistent = t;
                out.selfConsistentValid = true;
            }
        }
    }

    out.suggested = out.stableValid ? out.stableAlign : out.selfConsistentValid ? out.selfConsistent : out.centre;
    out.jumps = jumpsAt(nearestIndex(d.radius, out.suggested));
    return out;
}

std::vector<std::size_t> selectScans(std::size_t scanCount, std::size_t first, std::size_t last,
                                     std::size_t everyNth)
{
    std::vector<std::size_t> idx;
    if (scanCount == 0) return idx;
    last = std::min(last, scanCount - 1);
    everyNth = std::max<std::size_t>(everyNth, 1);
    for (std::size_t i = first; i <= last; i += everyNth) idx.push_back(i);
    return idx;
}

AccelerationEstimate estimateAcceleration(std::span<const double> measuredSpeeds)
{
    AccelerationEstimate e;
    if (measuredSpeeds.empty()) return e;
    const double v = std::accumulate(measuredSpeeds.begin(), measuredSpeeds.end(), 0.0) / double(measuredSpeeds.size());
    e.meanSpeed = v;
    e.omega2t = 32681000.0 * std::expm1(7.55e-05 * v);
    e.time = 49.49 + 0.00121 * v + 2.64357e-08 * v * v;
    return e;
}

double integrateOmega2t(std::span<const double> seconds, std::span<const double> rpm)
{
    const std::size_t n = std::min(seconds.size(), rpm.size());
    constexpr double k = 2.0 * std::numbers::pi / 60.0;
    double sum = 0.0;
    for (std::size_t i = 1; i < n; ++i) {
        const double w0 = k * rpm[i - 1], w1 = k * rpm[i];
        sum += 0.5 * (w0 * w0 + w1 * w1) * (seconds[i] - seconds[i - 1]);
    }
    return sum;
}

Omega2tFit fitOmega2t(std::span<const double> seconds, std::span<const double> omega2t, std::span<const double> rpm)
{
    Omega2tFit f;
    const std::size_t n = std::min({seconds.size(), omega2t.size(), rpm.size()});
    if (n < 2) return f;
    std::vector<double> sorted(rpm.begin(), rpm.begin() + std::ptrdiff_t(n));
    std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(n / 2), sorted.end());
    const double median = sorted[n / 2];
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < n; ++i)
        if (median <= 0.0 || std::abs(rpm[i] - median) <= 0.005 * median) idx.push_back(i);
    if (idx.size() < 2) return f;
    double st = 0, sw = 0, stt = 0, stw = 0;
    for (std::size_t i : idx) {
        st += seconds[i];
        sw += omega2t[i];
        stt += seconds[i] * seconds[i];
        stw += seconds[i] * omega2t[i];
    }
    const double m = double(idx.size());
    const double den = m * stt - st * st;
    if (!(std::abs(den) > 0.0)) return f;
    f.slope = (m * stw - st * sw) / den;
    f.intercept = (sw - f.slope * st) / m;
    if (!(f.slope > 0.0)) return f;
    f.rpm = std::sqrt(f.slope) * 60.0 / (2.0 * std::numbers::pi);
    f.t0 = -f.intercept / f.slope;
    double ss = 0.0, scale = 0.0;
    for (std::size_t i : idx) {
        const double r = omega2t[i] - (f.slope * seconds[i] + f.intercept);
        ss += r * r;
        scale = std::max(scale, std::abs(omega2t[i]));
    }
    f.rms = scale > 0.0 ? std::sqrt(ss / m) / scale : 0.0;
    f.used = idx.size();
    f.valid = true;
    return f;
}

WavelengthBins binWavelengths(std::span<const float> wavelengths)
{
    WavelengthBins b;
    long current = 0;
    double sum = 0.0;
    for (std::size_t i = 0; i < wavelengths.size(); ++i) {
        const long nm = long(std::nearbyint(wavelengths[i]));
        if (i == 0 || nm != current) {
            if (i) b.wavelength.push_back(float(sum / double(b.count.back())));
            b.start.push_back(i);
            b.count.push_back(0);
            current = nm;
            sum = 0.0;
        }
        ++b.count.back();
        sum += wavelengths[i];
    }
    if (!b.count.empty()) b.wavelength.push_back(float(sum / double(b.count.back())));
    return b;
}

std::vector<float> averageOverBins(std::span<const float> rows, std::size_t pointCount, const WavelengthBins& bins)
{
    std::vector<float> out(bins.start.size() * pointCount, 0.f);
    for (std::size_t b = 0; b < bins.start.size(); ++b) {
        for (std::size_t j = 0; j < pointCount; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < bins.count[b]; ++k) {
                const std::size_t idx = (bins.start[b] + k) * pointCount + j;
                if (idx < rows.size()) s += rows[idx];
            }
            out[b * pointCount + j] = float(s / double(bins.count[b]));
        }
    }
    return out;
}

double integrateRadial(std::span<const double> r, std::span<const float> y, double r1, double r2, bool radialWeight)
{
    const std::size_t n = std::min(r.size(), y.size());
    if (n < 2) return 0.0;
    double sign = 1.0;
    if (r2 < r1) {
        std::swap(r1, r2);
        sign = -1.0;
    }
    r1 = std::max(r1, r.front());
    r2 = std::min(r2, r[n - 1]);
    if (r2 <= r1) return 0.0;

    auto f = [&](double rr, double yy) { return radialWeight ? yy * rr : yy; };
    auto interp = [&](double x) {
        const double t = thresholdIndex(r.first(n), x);
        const std::size_t i = std::min<std::size_t>(std::size_t(t), n - 2);
        const double frac = t - double(i);
        return double(y[i]) + frac * (double(y[i + 1]) - double(y[i]));
    };

    double sum = 0.0;
    double xPrev = r1, fPrev = f(r1, interp(r1));
    for (std::size_t j = 0; j < n; ++j) {
        if (r[j] <= r1) continue;
        if (r[j] >= r2) break;
        const double fj = f(r[j], y[j]);
        sum += 0.5 * (fPrev + fj) * (r[j] - xPrev);
        xPrev = r[j];
        fPrev = fj;
    }
    sum += 0.5 * (fPrev + f(r2, interp(r2))) * (r2 - xPrev);
    return sign * sum;
}

std::vector<double> integrateScans(const Dataset& d, double r1, double r2, bool radialWeight)
{
    std::vector<double> out;
    out.reserve(d.scans.size());
    for (const auto& s : d.scans) out.push_back(integrateRadial(d.radius, s.values, r1, r2, radialWeight));
    return out;
}

}  // namespace auc::proc
