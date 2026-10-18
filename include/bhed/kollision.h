// kollision.h - Paket bewegung (rc601, Wege wie im Spiel): Spuren gegen die
// Brushes der Karte, wie die Engine sie zieht (CM_BoxTrace, cm_trace.cpp).
//
// Die Wege der NPCs haengen an Spuren: ob STEER gerade auf ein Ziel zulaeuft
// (MoveTrace mit dem Kasten der Figur), welcher Knoten des Wegnetzes der
// naechste ist (ViewNavTrace), welche Kanten das Netz behaelt (TestEdge,
// ein Kasten von 40 oder 120 Breite) und wie weit ein Wegpunkt reicht
// (waypoint_getRadius). Die Engine spurt gegen die BRUSHES der Karte - auch
// gegen unsichtbare wie monsterclip und botclip, mit denen Leveldesigner die
// NPCs lenken. Die Dreiecke der Zeichnung kennen die nicht. Darum hier der
// Teil der Kollision von Quake 3/JKA:
//
//   Lumps 1 (shaders: contentFlags), 2 (planes), 3 (nodes), 4 (leafs),
//   6 (leafbrushes), 7 (models), 8 (brushes), 9 (brushsides) - wie
//   CM_LoadMap (cm_load.cpp);
//   CM_BoxTrace mit CM_TraceThroughTree, CM_TraceToLeaf,
//   CM_TraceThroughBrush, CM_PositionTest/CM_TestInLeaf/CM_TestBoxInBrush
//   (cm_trace.cpp, Movie Duels code/qcommon) und CM_TransformedBoxTrace fuer
//   Brush-Modelle an ihrem Platz.
//
// rc601 (bewegung, teil 3): dazu die Patches (gekruemmte Flaechen,
// CM_GeneratePatchCollide/CM_TraceThroughPatchCollide/
// CM_PositionTestInPatchCollide aus cm_patch.cpp, Lumps 5, 10, 13). Terrain
// (CONTENTS_TERRAIN, RMG) fehlt - die Einzelspieler-Karten von JKA nutzen es
// nicht.
#ifndef BHED_KOLLISION_H
#define BHED_KOLLISION_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct KollisionsKarte {
    struct Ebene {
        float normale[3]{};
        float abstand = 0.0F;
        int typ = 3;          // PLANE_X/Y/Z oder 3 (nicht achsparallel)
        int vorzeichen = 0;   // signbits
    };
    struct Seite {
        int ebene = 0;
        int shader = 0;
    };
    struct Pinsel {
        int ersteSeite = 0;
        int seiten = 0;
        int shader = 0;
        std::uint32_t inhalt = 0;
        float mins[3]{};
        float maxs[3]{};
    };
    struct Knoten {
        int ebene = 0;
        int kinder[2]{};
    };
    struct Blatt {
        int cluster = -1;
        int erstePinsel = 0;
        int pinsel = 0;
        int ersteFlaeche = 0;   // rc601 (bewegung, teil 3)
        int flaechen = 0;
    };
    struct Modell {
        float mins[3]{};
        float maxs[3]{};
        int erster = 0;   // erster Brush
        int zahl = 0;
        int ersteFlaeche = 0;   // rc601 (bewegung, teil 3): firstSurface/numSurfaces (Patches)
        int flaechenZahl = 0;
    };
    // rc601 (bewegung, teil 3): ein Patch wie CM_GeneratePatchCollide ihn baut
    // (patchCollide_t, cm_patch.h): Ebenen, Facetten mit Raendern und Kanten.
    struct PatchEbene {
        float ebene[4]{};
        int vorzeichen = 0;
    };
    struct Facette {
        int flaeche = -1;   // surfacePlane
        int zahl = 0;       // numBorders
        int raender[4 + 6 + 16]{};
        bool innen[4 + 6 + 16]{};
        bool nichtAnpassen[4 + 6 + 16]{};
    };
    struct Patch {
        float mins[3]{};
        float maxs[3]{};
        std::vector<PatchEbene> ebenen;
        std::vector<Facette> facetten;
        std::uint32_t inhalt = 0;
        std::uint32_t flaeche = 0;
    };
    std::vector<Patch> patches;
    std::vector<int> flaechePatch;    // Flaeche (drawsurface) -> Patch, -1
    std::vector<int> blattFlaechen;   // leafsurfaces
    std::vector<std::uint32_t> shaderInhalt;
    std::vector<std::uint32_t> shaderFlaeche;
    std::vector<Ebene> ebenen;
    std::vector<Seite> seiten;
    std::vector<Pinsel> pinsel;
    std::vector<Knoten> knoten;
    std::vector<Blatt> blaetter;
    std::vector<int> blattPinsel;
    std::vector<Modell> modelle;
    [[nodiscard]] bool leer() const noexcept { return knoten.empty(); }
};

// Die Brushes aus den Bytes einer .bsp (RBSP/IBSP). false: nicht lesbar.
[[nodiscard]] bool leseKollision(const std::string& bsp, KollisionsKarte& aus);

struct KollisionsSpur {
    bool allsolid = false;
    bool startsolid = false;
    float fraction = 1.0F;
    std::array<float, 3> endpos{};
    std::array<float, 3> normale{};
    std::uint32_t inhalt = 0;
    // rc601 saber: trace->surfaceFlags der getroffenen Seite (SURF_NOIMPACT ...).
    std::uint32_t flaeche = 0;
};

// CM_BoxTrace gegen Modell 0 (die Welt) bzw. CM_TransformedBoxTrace gegen ein
// Brush-Modell (modell > 0) mit Ursprung und Winkeln.
[[nodiscard]] KollisionsSpur kollisionsSpur(const KollisionsKarte& k, const float start[3], const float mins[3],
                                            const float maxs[3], const float ende[3], std::uint32_t maske,
                                            int modell = 0, const float ursprung[3] = nullptr,
                                            const float winkel[3] = nullptr);

}  // namespace bhed
#endif
