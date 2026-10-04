#include "auc/Noise.h"

#include <algorithm>
#include <cmath>

namespace auc::noise {

namespace {

double rms(const Dataset& d, const Result* r)
{
    double ss = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < d.scans.size(); ++i) {
        const auto& v = d.scans[i].values;
        for (std::size_t j = 0; j < v.size(); ++j) {
            double e = v[j];
            if (r && !r->ti.empty() && j < r->ti.size()) e -= r->ti[j];
            if (r && !r->ri.empty() && i < r->ri.size()) e -= r->ri[i];
            ss += e * e;
            ++n;
        }
    }
    return n ? std::sqrt(ss / double(n)) : 0.0;
}

}  // namespace

Result fit(const Dataset& e, Components which)
{
    Result res;
    const std::size_t ns = e.scans.size();
    std::size_t np = e.pointCount();
    for (const auto& s : e.scans) np = std::min(np, s.values.size());
    if (ns == 0 || np == 0) return res;

    std::vector<double> rowMean(ns, 0.0), colMean(np, 0.0);
    double grand = 0.0;
    for (std::size_t i = 0; i < ns; ++i) {
        for (std::size_t j = 0; j < np; ++j) {
            const double v = e.scans[i].values[j];
            rowMean[i] += v;
            colMean[j] += v;
        }
        grand += rowMean[i];
        rowMean[i] /= double(np);
    }
    for (double& c : colMean) c /= double(ns);
    grand /= double(ns * np);

    const bool ti = which & TimeInvariant;
    const bool ri = which & RadiallyInvariant;
    if (ti && ri) {
        res.ti = colMean;
        res.ri.resize(ns);
        for (std::size_t i = 0; i < ns; ++i) res.ri[i] = rowMean[i] - grand;
    } else if (ti) {
        res.ti = colMean;
    } else if (ri) {
        res.ri = rowMean;
    }
    res.rmsBefore = rms(e, nullptr);
    res.rmsAfter = rms(e, &res);
    return res;
}

Result fitToModel(const Dataset& data, const Dataset& model, Components which)
{
    Dataset resid = data;
    const std::size_t ns = std::min(data.scans.size(), model.scans.size());
    resid.scans.resize(ns);
    for (std::size_t i = 0; i < ns; ++i) {
        auto& v = resid.scans[i].values;
        const auto& m = model.scans[i].values;
        const std::size_t n = std::min(v.size(), m.size());
        v.resize(n);
        for (std::size_t j = 0; j < n; ++j) v[j] -= m[j];
    }
    return fit(resid, which);
}

void subtract(Dataset& d, const Result& noise)
{
    for (std::size_t i = 0; i < d.scans.size(); ++i) {
        auto& v = d.scans[i].values;
        const double beta = i < noise.ri.size() ? noise.ri[i] : 0.0;
        for (std::size_t j = 0; j < v.size(); ++j) {
            const double b = j < noise.ti.size() ? noise.ti[j] : 0.0;
            v[j] = float(v[j] - b - beta);
        }
    }
}

}  // namespace auc::noise
