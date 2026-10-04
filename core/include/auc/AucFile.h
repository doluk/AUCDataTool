// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/Dataset.h"

#include <QString>

namespace auc {

/// Result of an I/O operation. `ok()` is true on success; `message` explains failures.
struct IoResult {
    enum Code { Ok, CannotOpen, NotAucFile, BadVersion, BadType, NoData, BadCrc, WriteFailed };
    Code code = Ok;
    QString message;
    bool ok() const { return code == Ok; }
};

/// Reader/writer for the openAUC raw-data format (".auc", magic "UCDA").
///
/// Layout (little-endian): magic "UCDA", version "04"/"05", type (2 chars), cell (u8),
/// channel (char), GUID (16 bytes), description (240 bytes), 7 floats
/// (r_min, r_max, Δr, data1 min/max, data2 min/max), scan count (i16); then per scan
/// "DATA", T (f32), rpm (f32), seconds (i32), ω²t (f32), wavelength (u16), Δr (f32),
/// count (i32), readings as u16 quantised between data1 min/max (plus a u16 stddev per
/// reading when data2 range is non-zero), interpolation bitmap; finally a CRC-32 over
/// all preceding bytes.
///
/// Version 4 encodes the wavelength as (λ − 180 nm)·100, version 5 as λ·10.
class AucFile {
public:
    static constexpr int kWriteVersion = 5;

    static IoResult read(const QString& path, Dataset& out);
    static IoResult readFromBytes(const QByteArray& bytes, Dataset& out);

    /// Writes `data` in format version 5. Values are quantised to 16 bits over their range.
    static IoResult write(const QString& path, const Dataset& data);
    static QByteArray toBytes(const Dataset& data);

    /// Reads only the file header and the first scan header (no readings) – fast, for file lists.
    struct Header {
        DataType type = DataType::RadialAbsorbance;
        int cell = 0;
        char channel = 'A';
        QString description;
        int scanCount = 0;
        int version = 0;
        std::array<std::uint8_t, 16> guid{};
        // From the first scan header:
        double wavelength = 0.0;  ///< nm
        double rMin = 0.0;        ///< cm
        double deltaR = 0.0;      ///< cm
        int points = 0;
    };
    static IoResult readHeader(const QString& path, Header& out);
};

}  // namespace auc
