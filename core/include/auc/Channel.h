// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/AucFile.h"
#include "auc/Dataset.h"

#include <QString>
#include <QStringList>

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace auc {

enum class SourceFormat {
    Auc,   ///< openAUC .auc, one file per wavelength
    Mwrs,  ///< Cölfen MWL detector .mwrs (v1.0–1.4), one file per scan, all wavelengths
    Mw,    ///< older MWL .mw (v1.0/1.1 and v1.2 with dark current), one file per scan
};

/// Run conditions of one scan (shared by all wavelengths of that scan).
struct ScanInfo {
    int number = 0;          ///< scan number from the file
    double temperature = 0;  ///< °C
    double rpm = 0;          ///< measured rotor speed
    double setRpm = 0;       ///< set speed (0 if not stored)
    double seconds = 0;      ///< elapsed time
    double omega2t = 0;      ///< rad²/s
};

/// One cell/channel of a run: scans × wavelengths × radius points.
///
/// Opening a source reads only headers. Readings are fetched on demand per wavelength
/// (one radius row from each scan file), so an 8-cell × 2-channel × 600-wavelength run
/// never has to fit into memory. Recently used slices are cached.
class ChannelSource {
public:
    virtual ~ChannelSource() = default;

    int cell = 0;
    char channel = 'A';
    QString runId;
    QString description;   ///< sample description
    QString folder;        ///< directory of the files
    SourceFormat format = SourceFormat::Auc;
    QString formatName;    ///< e.g. "mwrs 1.4"
    bool absorbanceData = false;  ///< readings are absorbance already (else intensity)

    std::vector<double> wavelengths;  ///< nm, ascending
    std::vector<double> radius;       ///< cm
    std::vector<ScanInfo> scans;

    /// Dark-current value per wavelength (only .mw v1.2); empty otherwise.
    std::vector<float> darkCurrent;
    bool darkSubtractedInFile = false;

    QStringList files;

    /// "1A", "2B" …
    QString key() const;
    /// Index of the wavelength closest to `nm`.
    std::size_t nearestWavelength(double nm) const;

    /// All scans at one wavelength, as a Dataset (type RI for intensity, RA for absorbance).
    /// Thread-safe; cached.
    IoResult wavelengthSlice(std::size_t index, std::shared_ptr<const Dataset>& out) const;

    /// Mean over the wavelengths [first, first+count) – multi-wavelength averaging (MWA).
    IoResult wavelengthMean(std::size_t first, std::size_t count, std::shared_ptr<const Dataset>& out) const;

    /// Re-reads headers after files were added or rewritten (live mode). Returns true if
    /// anything changed. Clears the slice cache.
    virtual bool refresh() = 0;

    void clearCache() const;

protected:
    /// Reads rows [first, first+count) for every scan; values[scan][k*npoint + j].
    virtual IoResult readRows(std::size_t first, std::size_t count, std::vector<std::vector<float>>& values) const = 0;
    Dataset emptyDataset(double wavelength) const;

private:
    struct CacheEntry {
        std::size_t first, count;
        std::shared_ptr<const Dataset> data;
    };
    mutable std::mutex m_cacheMutex;
    mutable std::list<CacheEntry> m_cache;  ///< most recent first
    static constexpr std::size_t kCacheSize = 6;
};

using ChannelPtr = std::shared_ptr<ChannelSource>;

struct OpenResult {
    std::vector<ChannelPtr> channels;
    QStringList warnings;
};

/// Opens files and/or folders (folders recursively, 3 levels) and groups them into
/// channels: .mwrs/.mw by header cell/channel per folder, .auc by cell/channel/type.
OpenResult openData(const QStringList& paths);

}  // namespace auc
