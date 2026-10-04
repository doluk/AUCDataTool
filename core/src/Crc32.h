// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace auc::detail {

/// Reflected CRC-32 (polynomial 0xEDB88320, as zlib), continued from a previous value.
/// The openAUC format seeds the running value with 0xFFFFFFFF rather than 0.
std::uint32_t crc32(std::uint32_t crc, const void* data, std::size_t len);

}  // namespace auc::detail
