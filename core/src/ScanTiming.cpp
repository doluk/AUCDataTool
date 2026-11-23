// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/ScanTiming.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace auc::timing {

double median(std::vector<double> v)
{
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    const std::size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(m), v.end());
    const double hi = v[m];
    if (v.size() % 2) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + std::ptrdiff_t(m));
    return 0.5 * (lo + hi);
}

Analysis analyze(std::vector<Slot> input)
{
    Analysis a;
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    // Sort and de-duplicate each slot's scans; merge slots repeating another one's scans
    // (same cell, wavelength and times: two channels read from the same scans).
    std::vector<Slot> merged;
    for (Slot& s : input) {
        std::sort(s.scans.begin(), s.scans.end(), [](const ScanTime& x, const ScanTime& y) {
            return x.seconds != y.seconds ? x.seconds < y.seconds : x.number < y.number;
        });
        s.scans.erase(std::unique(s.scans.begin(), s.scans.end(),
                                  [](const ScanTime& x, const ScanTime& y) { return x.seconds == y.seconds && x.number == y.number; }),
                      s.scans.end());
        if (s.scans.empty()) continue;
        const auto same = std::find_if(merged.begin(), merged.end(), [&](const Slot& m) {
            return m.cell == s.cell && std::abs(m.wavelength - s.wavelength) < 0.05 && m.scans.size() == s.scans.size()
                && std::equal(m.scans.begin(), m.scans.end(), s.scans.begin(),
                              [](const ScanTime& x, const ScanTime& y) { return x.seconds == y.seconds; });
        });
        if (same == merged.end()) {
            s.channels.assign(1, s.channel);
            merged.push_back(std::move(s));
        } else if (same->channels.find(s.channel) == std::string::npos) {
            same->channels += s.channel;
            std::sort(same->channels.begin(), same->channels.end());
            same->channel = same->channels.front();
        }
    }
    if (merged.empty()) return a;

    // Events in time order (ties: by slot, i.e. by cell, then wavelength).
    std::sort(merged.begin(), merged.end(), [](const Slot& x, const Slot& y) {
        if (x.cell != y.cell) return x.cell < y.cell;
        return x.wavelength != y.wavelength ? x.wavelength < y.wavelength : x.channel < y.channel;
    });
    std::vector<Event> events;
    for (std::size_t k = 0; k < merged.size(); ++k)
        for (const ScanTime& t : merged[k].scans) events.push_back({t.seconds, int(k), t.number});
    std::stable_sort(events.begin(), events.end(), [](const Event& x, const Event& y) { return x.seconds < y.seconds; });

    // Usual successor of each slot: the slot most often scanned next.
    const int ns = int(merged.size());
    std::vector<std::vector<int>> count(std::size_t(ns), std::vector<int>(std::size_t(ns), 0));
    for (std::size_t i = 0; i + 1 < events.size(); ++i) ++count[std::size_t(events[i].slot)][std::size_t(events[i + 1].slot)];
    std::vector<int> succ(std::size_t(ns), -1);
    for (int s = 0; s < ns; ++s) {
        int best = 0;
        for (int t = 0; t < ns; ++t)
            if (count[std::size_t(s)][std::size_t(t)] > best) best = count[std::size_t(s)][std::size_t(t)], succ[std::size_t(s)] = t;
    }

    // Acquisition order: follow the successors from the first slot scanned; the rest by first scan.
    std::vector<int> order;
    std::vector<bool> placed(std::size_t(ns), false);
    for (int s = events.front().slot; s >= 0 && !placed[std::size_t(s)]; s = succ[std::size_t(s)]) {
        order.push_back(s);
        placed[std::size_t(s)] = true;
    }
    for (const Event& e : events)
        if (!placed[std::size_t(e.slot)]) {
            order.push_back(e.slot);
            placed[std::size_t(e.slot)] = true;
        }
    std::vector<int> rank(static_cast<std::size_t>(ns), 0);
    for (int k = 0; k < ns; ++k) rank[std::size_t(order[std::size_t(k)])] = k;

    for (int s : order) a.cellSlots.push_back(std::move(merged[std::size_t(s)]));
    for (Event& e : events) e.slot = rank[std::size_t(e.slot)];
    a.successor.assign(std::size_t(ns), -1);
    for (int s = 0; s < ns; ++s)
        if (succ[std::size_t(s)] >= 0) a.successor[std::size_t(rank[std::size_t(s)])] = rank[std::size_t(succ[std::size_t(s)])];
    a.events = std::move(events);

    // Interval to the next scan, transitions and skips.
    const auto& ev = a.events;
    a.next.assign(ev.size(), nan);
    std::map<std::pair<int, int>, std::size_t> index;
    for (std::size_t i = 0; i + 1 < ev.size(); ++i) {
        const int from = ev[i].slot, to = ev[i + 1].slot;
        const double dt = ev[i + 1].seconds - ev[i].seconds;
        a.next[i] = dt;
        auto [it, inserted] = index.try_emplace({from, to}, a.transitions.size());
        if (inserted) {
            Transition t;
            t.from = from;
            t.to = to;
            const int usual = a.successor[std::size_t(from)];
            t.regular = usual == to;
            if (!t.regular && usual >= 0) {
                // Walk the cycle from the usual successor until `to`: those were passed over.
                std::vector<int> passed;
                int s = usual;
                for (int steps = 0; steps < ns && s >= 0 && s != to; ++steps) {
                    passed.push_back(s);
                    s = a.successor[std::size_t(s)];
                }
                if (s == to) t.skipped = std::move(passed);
            }
            a.transitions.push_back(std::move(t));
        }
        Transition& t = a.transitions[it->second];
        t.intervals.push_back(dt);
        t.at.push_back(ev[i].seconds);
        if (!t.skipped.empty()) a.skips.push_back({ev[i].seconds, from, to, t.skipped});
    }
    for (Transition& t : a.transitions) t.median = median(t.intervals);
    std::stable_sort(a.transitions.begin(), a.transitions.end(), [](const Transition& x, const Transition& y) {
        if (x.regular != y.regular) return x.regular;
        if (x.regular) return x.from < y.from;
        return x.intervals.size() > y.intervals.size();
    });

    // Per slot: interval between consecutive scans.
    for (const Slot& s : a.cellSlots) {
        std::vector<double> d;
        for (std::size_t k = 1; k < s.scans.size(); ++k) d.push_back(s.scans[k].seconds - s.scans[k - 1].seconds);
        a.slotMedian.push_back(median(std::move(d)));
    }
    a.integerSeconds = std::all_of(ev.begin(), ev.end(), [](const Event& e) { return e.seconds == std::floor(e.seconds); });
    return a;
}

}  // namespace auc::timing
