// pk3write.cpp - ein .pk3 schreiben (siehe pk3.h, writePk3)
#include "bhed/pk3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace bhed {
namespace {

std::uint32_t crc32(const std::string& d) {
    static const std::array<std::uint32_t, 256> tabelle = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFU;
    for (const char ch : d) {
        c = tabelle[(c ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

void u16(std::string& o, std::uint32_t v) {
    o += static_cast<char>(v & 0xFFU);
    o += static_cast<char>((v >> 8U) & 0xFFU);
}
void u32(std::string& o, std::uint32_t v) {
    u16(o, v & 0xFFFFU);
    u16(o, v >> 16U);
}

}  // namespace

bool writePk3(const std::string& path, const std::vector<Pk3Datei>& files, std::string* error) {
    const auto fehler = [error](const std::string& t) {
        if (error != nullptr) { *error = t; }
        return false;
    };
    // Zip ohne Zip64: hoechstens 65535 Eintraege und 4 GB.
    if (files.size() > 0xFFFFU) {
        return fehler("too many files");
    }
    // Fester Zeitstempel 1.1.2026 - gleicher Inhalt gibt dasselbe Archiv.
    constexpr std::uint32_t kZeit = 0;
    constexpr std::uint32_t kDatum = ((2026U - 1980U) << 9U) | (1U << 5U) | 1U;
    std::string lokal;
    std::string verzeichnis;
    for (const Pk3Datei& f : files) {
        if (f.name.empty() || f.name.size() > 0xFFFFU) {
            return fehler("bad file name");
        }
        if (lokal.size() + f.inhalt.size() + f.name.size() + 64 > 0xFFFFFFF0ULL) {
            return fehler("archive too large");
        }
        const std::uint32_t crc = crc32(f.inhalt);
        const auto groesse = static_cast<std::uint32_t>(f.inhalt.size());
        const auto versatz = static_cast<std::uint32_t>(lokal.size());
        u32(lokal, 0x04034B50U);
        u16(lokal, 10);          // noetige Fassung 1.0
        u16(lokal, 0x0800U);     // Namen in UTF-8
        u16(lokal, 0);           // ohne Packung
        u16(lokal, kZeit);
        u16(lokal, kDatum);
        u32(lokal, crc);
        u32(lokal, groesse);
        u32(lokal, groesse);
        u16(lokal, static_cast<std::uint32_t>(f.name.size()));
        u16(lokal, 0);
        lokal += f.name;
        lokal += f.inhalt;

        u32(verzeichnis, 0x02014B50U);
        u16(verzeichnis, 20);
        u16(verzeichnis, 10);
        u16(verzeichnis, 0x0800U);
        u16(verzeichnis, 0);
        u16(verzeichnis, kZeit);
        u16(verzeichnis, kDatum);
        u32(verzeichnis, crc);
        u32(verzeichnis, groesse);
        u32(verzeichnis, groesse);
        u16(verzeichnis, static_cast<std::uint32_t>(f.name.size()));
        u16(verzeichnis, 0);
        u16(verzeichnis, 0);
        u16(verzeichnis, 0);
        u16(verzeichnis, 0);
        u32(verzeichnis, 0);
        u32(verzeichnis, versatz);
        verzeichnis += f.name;
    }
    std::string ende;
    u32(ende, 0x06054B50U);
    u16(ende, 0);
    u16(ende, 0);
    u16(ende, static_cast<std::uint32_t>(files.size()));
    u16(ende, static_cast<std::uint32_t>(files.size()));
    u32(ende, static_cast<std::uint32_t>(verzeichnis.size()));
    u32(ende, static_cast<std::uint32_t>(lokal.size()));
    u16(ende, 0);

    const std::filesystem::path ziel = std::filesystem::u8path(path);
    std::filesystem::path tmp = ziel;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            return fehler("cannot write " + path + ".tmp");
        }
        o.write(lokal.data(), static_cast<std::streamsize>(lokal.size()));
        o.write(verzeichnis.data(), static_cast<std::streamsize>(verzeichnis.size()));
        o.write(ende.data(), static_cast<std::streamsize>(ende.size()));
        if (!o.flush()) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return fehler("cannot write " + path + ".tmp");
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, ziel, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        return fehler("cannot replace " + path + " (" + ec.message() + ") - is the game running?");
    }
    return true;
}

}  // namespace bhed
