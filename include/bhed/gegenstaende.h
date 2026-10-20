// gegenstaende.h - rc602: die Gegenstaende der Karte (item_*, weapon_*,
// ammo_*, holocron_*) wie im Spiel.
//
// Gefunden mit der Kartenpruefung: auf 87 der 278 Karten lagen Medipacks,
// Waffen und Munition, die das Spiel zeigt - behaved zeichnete sie nicht.
//
// Die Engine baut die Liste bg_itemlist[ITM_NUM_ITEMS + 1] aus
// ext_data/items.dat (IT_LoadItemParms, g_itemLoad.cpp): jeder Block nennt
// mit "itemname" seinen Platz (das ITM_*-Aufzaehlungsglied, g_items.h), dazu
// classname, worldmodel, min/max, type, tag. G_CallSpawn (g_spawn.cpp:1021
// MD) sucht den Klassennamen mit strcmp, laeuft aber nur bis zum ERSTEN Platz
// ohne classname ("for (item = bg_itemlist + 1; item->classname; item++)").
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace bhed {

struct Gegenstand {
    std::string itemname;     // "ITM_MEDPAK_PICKUP"
    std::string classname;    // leer = Platz nicht belegt
    std::string worldmodel;   // .md3 oder .glm
    std::string typ;          // "IT_WEAPON" ... (wie in der Datei, gross)
    std::string tag;          // "WP_SABER" ...
    // IT_SetDefaults: -16 -16 -2 / 16 16 16, dann "min"/"max" (ganze Zahlen,
    // COM_ParseInt).
    std::array<float, 3> mins{-16.0F, -16.0F, -2.0F};
    std::array<float, 3> maxs{16.0F, 16.0F, 16.0F};
};

struct Gegenstandsliste {
    // Index = ITM_* (0 = ITM_NONE), Groesse ITM_NUM_ITEMS des Regelwerks.
    std::vector<Gegenstand> plaetze;
    // G_CallSpawn: der Gegenstand zu diesem Klassennamen, nullptr = das Spiel
    // spawnt ihn nicht als Gegenstand.
    [[nodiscard]] const Gegenstand* spawnbar(const std::string& klasse) const;
};

// items.dat lesen (Inhalt der Datei, die das Spiel nimmt: die des Mods vor
// base). movieDuels: die ITM_*-Aufzaehlung von Movie Duels (g_items.h MD,
// 80 Plaetze) statt der von OpenJK (61).
[[nodiscard]] Gegenstandsliste leseGegenstaende(const std::string& itemsDat, bool movieDuels,
                                                bool jediOutcast = false);

// --- Wo der Gegenstand liegt (FinishSpawningItem, g_items.cpp) -------------
//
// Ohne ITMSF_SUSPEND (1) faellt er: Spur mit seinen mins/maxs von origin+1
// senkrecht bis MIN_WORLD_COORD (MASK_SOLID | CONTENTS_PLAYERCLIP); beginnt
// sie im Festen, entfernt ihn das Spiel. ITMSF_INVISIBLE (32): EF_NODRAW.
inline constexpr int kItmsfSuspend = 1;
inline constexpr int kItmsfVertical = 16;
inline constexpr int kItmsfInvisible = 32;
inline constexpr int kItmsfNoGlow = 64;

}  // namespace bhed

// --- Aufstellen wie im Spiel -------------------------------------------------

namespace bhed {

struct MapEntity;

struct GegenstandAufstellung {
    bool sichtbar = false;
    std::string grund;          // warum nicht (fuer Protokoll und Pruefung)
    std::string modell;         // worldmodel
    bool glm = false;           // Ghoul2 (G2API_InitGhoul2Model gelingt nur mit .glm)
    std::array<float, 3> ort{};
    // s.angles: Ghoul2 zeichnet mit G2_GenerateWorldMatrix(ent.angles =
    // lerpAngles), CG_SetGhoul2Info (cg_ents.cpp:292 MD).
    std::array<float, 3> winkel{};
    // spinAngles mit dem ITMSF_VERTICAL-Kippen - gilt nur fuer die Achsen
    // (AnglesToAxis -> ent.axis), also fuer .md3 (CG_Item, cg_ents.cpp:1218 ff.).
    std::array<float, 3> md3Winkel{};
};

// Spur fuer das Fallen: Kasten mins/maxs von start nach ende gegen alles
// Feste (MASK_SOLID | CONTENTS_PLAYERCLIP); liefert endpos und startsolid.
using GegenstandSpur = std::function<void(const std::array<float, 3>& start, const std::array<float, 3>& mins,
                                          const std::array<float, 3>& maxs, const std::array<float, 3>& ende,
                                          std::array<float, 3>& endpos, bool& startsolid)>;

// G_SpawnGEntityFromSpawnVars (notsingle, Schwierigkeit), G_SpawnItem
// (targetname: erst per use) und FinishSpawningItem (JKA-Zweig) fuer EINEN
// Gegenstand. spielstufe = g_spskill (regelwerkSpielstufe()).
[[nodiscard]] GegenstandAufstellung gegenstandAufstellen(const MapEntity& e, const Gegenstand& g, int spielstufe,
                                                         bool movieDuels, const GegenstandSpur& spur);

// SpawnForCurrentDifficultySetting (g_spawn.cpp:1222 MD) und "notsingle"
// (:1259): gilt fuer JEDE Entity, bevor ihre Spawnfunktion laeuft.
[[nodiscard]] bool spawntBeiSchwierigkeit(const MapEntity& e, int spielstufe);

}  // namespace bhed
