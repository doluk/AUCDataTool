// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/AucFile.h"

#include <QByteArray>
#include <QMap>
#include <QString>

#include <cstdint>
#include <vector>

/// Binary formats of the multi-wavelength (MWL) detector of AG Cölfen.
///
/// All multi-byte values are big-endian. Layouts were recovered from the LabVIEW
/// AUC-Viewer 2.2.2 (sub_fileIO_header_reader, data reader bi2I32, file_io data reader,
/// sub_fileIO_read_xml_file) and cross-checked with UltraScan III (US_MwlData).
///
/// .mwrs, one file per cell/channel/scan, version from the run's *.mwrs.xml:
///   v1.1–1.4 (26 bytes): cell u8 | channel char | scan u16 | rotor speed u16 |
///     set speed u16 | T u16 (°C·10) | ω²t f32 | time u32 (s) | points u16 |
///     r_start u16 (cm·1000) | r_step u16 (cm·10000) | nλ u16 | λ u16[nλ] (nm) |
///     readings i32[nλ][points]
///   v1.0: no set speed. LabVIEW reads nλ as i32 and λ as u32 (nm·10); UltraScan reads
///     nλ u16 and λ u16 (nm). The reader accepts whichever layout matches the file size.
///   Scaling: v1.0–1.2 ÷1000, v1.3 ×1, v1.4 ÷10000 for absorbance runs
///   (take_intensity="N"), ×1 for intensity runs.
///
/// .mw, one file per scan:
///   v1.0/1.1 (100 bytes): magic 4 | version 2 | date/time 6 | cell u8 | channel char |
///     scan i16 | description char[64] | speed i16 | T i16 (·10) | ω²t u32 (×10000) |
///     time i32 | points u16 | r_start u16 (·1000) | r_end u16 (·1000) | nλ u16 |
///     λ i16[nλ] (nm·10) | readings i32[nλ][points]
///   v1.2 (115 bytes): magic 4 | version 2 | value bytes u8 | value type u8 (1 unsigned,
///     2 signed, 3 float) | dark subtracted u8 | date/time 6 | start date/time 6 |
///     duration u16 | replicates u16 | cell | channel | scan | description[64] | speed |
///     set speed | T | ω²t | time | points | r_start | r_end | nλ | λ i16[nλ] |
///     dark current value[nλ] | readings value[nλ][points]
namespace auc::mwl {

/// Settings from a run's "*.mwrs.xml".
struct MwrsRunInfo {
    double version = 1.4;
    bool takeIntensity = true;      ///< false → readings are absorbance
    QString runId;
    QMap<QString, QString> samples;  ///< key "1A" → sample description
    bool fromFile = false;
};

IoResult readMwrsXml(const QString& path, MwrsRunInfo& out);
IoResult writeMwrsXml(const QString& path, const MwrsRunInfo& info);

/// Header of one scan file (.mwrs or .mw).
struct ScanHeader {
    int cell = 1;
    char channel = 'A';
    int scan = 1;
    double rpm = 0, setRpm = 0;
    double temperature = 0;
    double omega2t = 0;
    double seconds = 0;
    int points = 0;
    double rStart = 0, rStep = 0;
    std::vector<double> wavelengths;  ///< nm
    QString description;              ///< .mw only
    QString dateTime;                 ///< .mw only, "dd.mm.yyyy hh:mm:ss"

    // Data layout
    qint64 dataOffset = 0;
    int valueBytes = 4;   ///< 2 or 4
    int valueType = 2;    ///< 1 unsigned, 2 signed, 3 float
    double scale = 1.0;   ///< reading = raw · scale
    std::vector<float> darkCurrent;  ///< .mw v1.2, per wavelength
    bool darkSubtracted = false;
    QString variant;      ///< "mwrs 1.4", "mw 1.2", …
};

/// Bytes needed from the start of the file to parse a header with `nLambdaMax` wavelengths.
constexpr qint64 kHeaderProbeBytes = 64 * 1024;

/// Parses an .mwrs header. `head` must hold at least the header and wavelength table;
/// `fileSize` disambiguates the v1.0 layouts and verifies completeness.
IoResult parseMwrsHeader(const QByteArray& head, qint64 fileSize, const MwrsRunInfo& run, ScanHeader& out);

/// Parses an .mw header (v1.0/1.1 or v1.2, detected from the file size).
IoResult parseMwHeader(const QByteArray& head, qint64 fileSize, ScanHeader& out);

/// Decodes `count` big-endian values into floats, multiplied by `scale`.
void decodeValues(const char* src, std::size_t count, int valueBytes, int valueType, double scale, float* dst);

/// Encoders (used by the synthetic data generator and tests). `rows[λ][point]` are raw
/// integer readings as stored in the file.
QByteArray encodeMwrs(const ScanHeader& h, const std::vector<std::vector<std::int32_t>>& rows, double version);
QByteArray encodeMw12(const ScanHeader& h, const std::vector<std::vector<std::int32_t>>& rows,
                      const std::vector<std::int32_t>& dark);

}  // namespace auc::mwl
