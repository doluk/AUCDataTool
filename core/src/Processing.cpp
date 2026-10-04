// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Processing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <numeric>

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
