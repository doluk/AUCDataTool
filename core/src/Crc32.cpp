#include "Crc32.h"

#include <array>

namespace auc::detail {

namespace {
constexpr std::array<std::uint32_t, 256> makeTable()
{
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
    return t;
}
constexpr auto kTable = makeTable();
}  // namespace

std::uint32_t crc32(std::uint32_t crc, const void* data, std::size_t len)
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    crc ^= 0xFFFFFFFFu;
    while (len--)
        crc = kTable[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace auc::detail
