// glm.h - Ghoul2-Modelle lesen (.glm)
//
// Ein .glm enthaelt die GEOMETRIE einer Figur, aber kein Skelett und keine
// Animation: die stehen in einer .gla, deren Pfad im Kopf steht. Bei
// model.glm aus Movie Duels ist das "models/players/_humanoid/_humanoid".
//
// Aufbau, an einer echten Datei nachgemessen:
//
//   Kopf        "2LGM", Fassung 6, Name, Pfad der .gla,
//               numBones, numLODs, numSurfaces und zwei Versaetze
//   Hierarchie  je Flaeche: Name, Flaggen, Shadername, Elternteil, Kinder
//   LODs        je Stufe: Versatzliste, dann die Flaechen mit Vertices,
//               Dreiecken und Knochenverweisen
//
// Gelesen wird nur die feinste Stufe (LOD 0) - die groeberen sind fuer die
// Entfernungsdarstellung im Spiel und in einem Editor ohne Nutzen.
//
// Was hier NICHT drin ist: die Verformung durch das Skelett. Ohne .gla
// stehen die Vertices in ihrer Ruhelage, und genau die zeigen wir. Sobald
// das Skelett dazukommt, kann dieselbe Struktur animiert werden.
#ifndef BHED_GLM_H
#define BHED_GLM_H

#include <cstdint>
#include <string>
#include <vector>

#include "bhed/gla.h"

namespace bhed {

struct GlmVertex {
    float xyz[3]{};
    float normal[3]{};
    float st[2]{};
    // Bis zu vier Knochen mit Gewichten. Die Verweise sind hier bereits
    // AUFGELOEST, zeigen also ins Skelett der .gla.
    //
    // In der Datei stehen sie als LOKALE 5-Bit-Indizes in die
    // BoneRefs-Liste der jeweiligen Flaeche - fuenf Bit reichen nur fuer 32
    // Werte, ein Skelett hat aber 53 Knochen. Wer sie direkt benutzt,
    // bekommt eine zerrissene Figur.
    std::uint8_t bones[4]{};
    float weights[4]{};
};

struct GlmSurface {
    std::string name;
    std::string shader;      // aus der Hierarchie, oft leer
    std::string texture;     // aus der .skin-Datei nachgetragen
    int parent = -1;
    std::uint32_t flags = 0;
    std::vector<GlmVertex> verts;
    std::vector<std::uint32_t> indexes;

    // Ob die .skin diese Flaeche abgeschaltet hat.
    //
    // Eine Zeile "kopf_b,*off" heisst genau das. Ohne diese Auswertung
    // werden ALLE Varianten gleichzeitig gezeichnet - bei einem Modell mit
    // zwei Koepfen sieht man beide ineinander.
    bool skinnedOff = false;

    // Die Flaggen aus dem Modell selbst. Aus mdx_format.h der Engine:
    //     G2SURFACEFLAG_ISBOLT 0x1   Verbindungspunkt, keine Geometrie
    //     G2SURFACEFLAG_OFF    0x2   in der Vorgabe abgeschaltet
    static constexpr std::uint32_t kFlagBolt = 0x1;
    static constexpr std::uint32_t kFlagOff = 0x2;

    // Verbindungspunkte ("*hips_cap_l_leg", "*r_hand") bestehen aus einem
    // einzigen Dreieck und werden nicht gezeichnet.
    //
    // Geprueft wird die FLAGGE, nicht der Name: der *-Vorsatz ist nur eine
    // Namenskonvention, die Flagge ist die Angabe des Formats.
    [[nodiscard]] bool isTag() const noexcept {
        return (flags & kFlagBolt) != 0 || (!name.empty() && name[0] == '*');
    }

    // In der Vorgabe abgeschaltet - Kappen, die Loecher an abgetrennten
    // Gliedmassen schliessen.
    [[nodiscard]] bool isOffByName() const noexcept;

    // Wird diese Flaeche gezeichnet?
    [[nodiscard]] bool isVisible(bool showCaps) const noexcept {
        if (isTag() || verts.empty()) {
            return false;
        }
        if (skinnedOff) {
            return false;   // die .skin hat sie abgeschaltet
        }
        return showCaps || !isOffByName();
    }
};

struct GlmModel {
    std::string name;
    std::string animFile;    // Pfad der .gla, ohne Endung
    int numBones = 0;
    int numLods = 0;
    // Ein JK2-Modell (72 Knochen), dessen Verweise beim Lesen auf das
    // JKA-Skelett umgelegt wurden - siehe readGlm.
    bool jk2Umgewandelt = false;
    std::vector<GlmSurface> surfaces;

    float mins[3]{};
    float maxs[3]{};

    [[nodiscard]] bool empty() const noexcept { return surfaces.empty(); }
};

[[nodiscard]] bool readGlm(const std::string& bytes, GlmModel& out,
                           std::string* error = nullptr);

// Eine .skin-Datei zuordnen.
//
// Der Aufbau ist eine Zeile je Flaeche:
//     hips,models/players/luke/boots_hips_blue.tga
//     l_hand_cap_l_arm_off,models/players/stormtrooper/caps.tga
// Ein Eintrag "flaeche,*off" schaltet die Flaeche ab.
void applySkin(const std::string& skinText, GlmModel& model);

// --- Ein Bolzen: wohin man etwas anheftet -------------------------------
//
// Gemeldet: "ich sehe die Lichtschwerter auch nicht."
//
// Die Engine haengt den Griff an einen BOLZEN der Figur
// (g_client.cpp:1188):
//
//     ent->handRBolt = gi.G2API_AddBolt( &ent->ghoul2[...], "*r_hand" );
//
// "*r_hand" ist keine Knochen-, sondern eine TAGflaeche: ein einzelnes
// Dreieck im Modell, dessen Ecken wie jeder andere Vertex mitverformt
// werden. Aus den drei verformten Ecken entsteht eine Ausrichtung -
// G2_ProcessSurfaceBolt2, tr_ghoul2.cpp:2080 ff.
//
// Der Index der Flaeche, oder -1.
[[nodiscard]] int surfaceIndex(const GlmModel& model, const std::string& name);

// Die Matrix eines Bolzens: wo die Tagflaeche gerade steht und wie sie
// ausgerichtet ist.
//
// `world` sind die Knochenmatrizen der Figur, wie sie auch das Skinning
// benutzt; `anim` liefert die Grundstellung dazu.
//
// Gibt false bei einer Flaeche, die kein Dreieck ist, oder bei einem
// entarteten - dann steht in `out` nichts Brauchbares.
[[nodiscard]] bool boltMatrix(const GlmModel& model, int surface,
                              const std::vector<BoneMatrix>& world,
                              const GlaAnimation& anim, BoneMatrix& out);

// Dasselbe fuer ein STARRES Modell - einen Griff etwa, der kein eigenes
// Skelett hat, das sich bewegt. Rechnet direkt mit den Vertexkoordinaten.
[[nodiscard]] bool boltMatrixRigid(const GlmModel& model, int surface,
                                   BoneMatrix& out);

}  // namespace bhed
#endif
