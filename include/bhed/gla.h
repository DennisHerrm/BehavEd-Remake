// gla.h - Skelett und Bewegung (.gla)
//
// Die .glm enthaelt die Geometrie, die .gla das Skelett und alle
// Bewegungsdaten. Beide zusammen ergeben eine animierte Figur.
//
// Aufbau, nach g2c/src/mdxa.cpp:
//
//   Kopf       "2LGA", Fassung 6, Name, Massstab, numFrames, numBones,
//              ofsFrames, ofsCompBonePool, ofsSkel
//   Skelett    je Bone: Name, Flaggen, Elternteil, Grundstellung und ihre
//              Umkehrung, Kinderliste
//   Indizes    je Bild und Bone drei Byte - ein Verweis in den Bonevorrat
//   Vorrat     je Eintrag VIERZEHN Byte: vier Quaternionanteile und drei
//              Verschiebungen, jeweils als uint16
//
// Die Kompression ist verlustbehaftet und liegt fest:
//     Drehung:      wert = roh / 16383 - 2
//     Verschiebung: wert = roh / 64 - 512
//
// Der Vorrat wird GETEILT: gleiche Bonestellungen in verschiedenen Bildern
// verweisen auf denselben Eintrag. Deshalb ist eine .gla mit tausenden
// Bildern trotzdem klein.
#ifndef BHED_GLA_H
#define BHED_GLA_H

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

// Eine 3x4-Matrix: Drehung und Verschiebung, wie sie die Engine benutzt.
struct BoneMatrix {
    float m[3][4]{};

    // noexcept: reine Rechnung auf float-Feldern, keine Allokation.
    // Das ist keine Zierde - der Uebersetzer darf damit den Aufruf
    // einbetten und die Ausnahmetabellen weglassen, und die Matrizen
    // werden je Bild fuer 53 Knochen aufgerufen.
    static BoneMatrix identity() noexcept;
    [[nodiscard]] BoneMatrix operator*(const BoneMatrix& rhs) const noexcept;
    void transform(const float in[3], float out[3]) const noexcept;
};

struct GlaBone {
    std::string name;
    int parent = -1;
    BoneMatrix basePose;        // Grundstellung
    BoneMatrix basePoseInv;     // und ihre Umkehrung
};

// Ein eigenes Bild fuer einen Knochen und seinen ganzen Teilbaum.
struct BoneFrameOverride {
    int bone = -1;
    int frame = -1;   // negativ heisst: kein Vorrang, es gilt das geerbte
    // Das naechste Bild und der Anteil daran - eine Oberkoerperanimation
    // laeuft im eigenen Takt und wird ebenso zwischen zwei Bildern
    // gemischt wie der Koerper (tr_ghoul2.cpp:1531, je Knochenanimation
    // ein eigenes Bildpaar). -1: kein Mischen, es gilt nur `frame`.
    int next = -1;
    float fraction = 0.0F;
};

struct GlaAnimation {
    std::string name;
    std::string skeletonName;
    float scale = 0.0F;
    int numFrames = 0;
    std::vector<GlaBone> bones;

    // Der geteilte Vorrat, je Eintrag 14 Byte.
    std::vector<std::uint8_t> bonePool;
    // Je Bild und Bone ein Verweis in den Vorrat.
    std::vector<std::uint32_t> indexes;

    // Das Kino-Skelett der Karte, HINTER diesem angehaengt.
    //
    // NPC_stats.cpp:1245 laedt fuer jeden Standard-Humanoiden zusaetzlich
    // models/players/_humanoid_<karte>/_humanoid_<karte>.gla als zweite
    // Animationsdatei (glaIndex 1) mit eigener animation.cfg - daher kommen
    // die BOTH_CIN_*-Animationen der Zwischensequenzen. Bild n >= numFrames
    // ist hier Bild n - numFrames des Anhangs. Gleiche Knochen vorausgesetzt
    // (wird beim Anhaengen geprueft).
    const GlaAnimation* anhang = nullptr;

    [[nodiscard]] bool empty() const noexcept {
        return bones.empty() || numFrames == 0;
    }

    // Die Stellung eines Bones in einem Bild, relativ zum Elternteil.
    [[nodiscard]] BoneMatrix localMatrix(int frame, int bone) const;
    // Dieselbe Matrix, zwischen zwei Bildern gemischt. Siehe
    // worldMatricesLerp.
    [[nodiscard]] BoneMatrix localMatrixLerp(int frameA, int frameB, float t,
                                             int bone) const;

    // Die Reihenfolge, in der die Bones berechnet werden muessen.
    //
    // NICHT nach Index: in Ravens _humanoid.gla haben acht Bones ihren
    // Elternteil HINTER sich. Wer nach Index rechnet, benutzt eine noch
    // nicht berechnete Elternmatrix - und die Figur verdreht sich still.
    [[nodiscard]] std::vector<int> topologicalOrder() const;

    // Weltmatrizen aller Bones fuer ein Bild.
    //
    //     Wurzel:  X(b) = A(b) * B(b)
    //     sonst:   X(b) = X(p) * B(p)^-1 * A(b) * B(b)
    //
    // A ist die Stellung aus der Datei, B die Grundstellung.
    void worldMatrices(int frame, std::vector<BoneMatrix>& out) const;

    // --- Zwischen zwei Bildern ------------------------------------------
    //
    // Gemeldet: "er stottert immer noch beim Laufen."
    //
    // Das war der Grund. behaved sprang von Bild zu Bild - bei 20 Bildern
    // je Sekunde sind das zwanzig Stufen, waehrend der Bildschirm sechzig
    // oder mehr zeigt.
    //
    // Die Engine mischt zwischen zwei Bildern, und zwar denkbar einfach
    // (tr_ghoul2.cpp:1531 ff.):
    //
    //     for ( j = 0 ; j < 12 ; j++ )
    //         tbone[2][j] = (TB.backlerp * tbone[0][j])
    //                     + (frontlerp   * tbone[1][j]);
    //
    // Zwoelf Werte, linear, kein Quaternion. Und zwar auf den LOKALEN
    // Matrizen, VOR dem Zusammensetzen der Kette - wer erst zusammensetzt
    // und dann mischt, bekommt bei gebeugten Gliedern zu kurze Knochen.
    //
    // `t` ist der Anteil von `frameB`: 0 gibt genau frameA.
    void worldMatricesLerp(int frameA, int frameB, float t,
                           std::vector<BoneMatrix>& out) const;
    // Mit Knochenueberschreibungen, wie die andere Fassung von
    // worldMatrices. Ohne sie mischte ein Bein gegen einen Oberkoerper.
    void worldMatricesLerp(int frameA, int frameB, float t,
                           const std::vector<BoneFrameOverride>& over,
                           std::vector<BoneMatrix>& out) const;

    // Dasselbe, aber mit einem eigenen Bild fuer einzelne Teilbaeume.
    //
    // So teilt die Engine Ober- und Unterkoerper: SET_ANIM_LOWER setzt die
    // Animation auf "model_root" (das ganze Skelett), SET_ANIM_UPPER auf
    // "lower_lumbar" - und dessen Teilbaum ist alles oberhalb der Taille
    // (bg_panimate.cpp:4848, 4864; g_client.cpp:1353, 1544).
    //
    // Die Vererbung ist in tr_ghoul2.cpp, CBoneCache::EvalLow nachgelesen:
    // ein Knochen erbt erst das Bild seines Elternteils und ueberschreibt es
    // nur, wenn fuer IHN eine Animation gesetzt wurde.
    //
    // Derselbe Weg traegt spaeter den Mund: dort liegt die Animation auf
    // dem Knochen "face" (cg_players.cpp:5132).
    void worldMatrices(int frame, const std::vector<BoneFrameOverride>& over,
                       std::vector<BoneMatrix>& out) const;
};

[[nodiscard]] bool readGla(const std::string& bytes, GlaAnimation& out,
                           std::string* error = nullptr);

// Ein Abschnitt aus der animation.cfg.
//
//     BOTH_WALK1          30293   30   0   20
//     Name              Anfang  Zahl Schleife Tempo
// Zu einem .gla-Pfad die Namen, unter denen die animation.cfg zu suchen ist.
//
// Die Reihenfolge stammt aus der Engine: code/game/NPC_stats.cpp,
// G_ParseAnimationFile() versucht ERST "models/players/<skel>/<skel>.cfg"
// und DANN "models/players/<skel>/animation.cfg".
//
// <skel> ist dabei der letzte Teil des ORDNERS, nicht der Dateiname. Der
// Unterschied ist keine Spitzfindigkeit: Movie Duels legt sein Skelett als
// models/players/_humanoid_md/_humanoid.gla ab - Ordner und Datei heissen
// verschieden. Wer den Dateinamen nimmt, sucht "_humanoid.cfg" und findet
// nichts.
// Sieht die Datei so aus, als waere sie im TEXT-Modus uebertragen worden?
//
// Zwei Dateien in Movie Duels sind genau das: in
// scripts/tantive/r2d2_use.IBI und
// models/map_objects/gunship/republicgunship.gla steht KEIN EINZIGES
// Nullbyte - jede Null ist durch ein Leerzeichen (0x20) ersetzt. So sieht
// eine FTP-Uebertragung in ASCII aus. Das Spiel scheitert daran genauso;
// es ist kein Fehler des Lesers.
//
// Ohne diesen Hinweis meldet der Leser nur "unsinnige Angaben im Kopf",
// und man sucht die Ursache im eigenen Programm.
[[nodiscard]] bool looksTextMangled(const std::string& bytes);

[[nodiscard]] std::string skeletonNameFor(const std::string& animFile);
[[nodiscard]] std::vector<std::string> animCfgCandidates(const std::string& animFile);

struct AnimEntry {
    std::string name;
    int firstFrame = 0;
    int numFrames = 0;
    int loopFrame = -1;
    int fps = 20;
};

[[nodiscard]] std::vector<AnimEntry> parseAnimationCfg(const std::string& text);

// Welches Bild der .gla zeigt diese Animation gerade?
//
// -1 heisst: DIESES Skelett kennt den Namen nicht. Die Herleitung steht
// bei der Umsetzung in gla.cpp - kurz: die Engine behandelt einen
// unbekannten Namen als nicht geschehen, eine 0 waere ein gueltiges Bild.
//
// Steht seit rc247 im Kern und nicht mehr in der Oberflaeche: die Regel
// stammt aus der Engine, also gehoert sie dorthin, wo Proben sie fassen
// koennen.
[[nodiscard]] // Der letzte Parameter ist WIRKUNGSLOS und nur noch da, damit die
// Aufrufstellen nicht alle angefasst werden muessen.
//
// Frueher entschied er ueber Wiederholen oder Stehenbleiben. Nachgelesen
// ist das falsch: die Haltezeit sagt, wie lange eine Animation nicht
// ersetzt werden darf, nicht ob sie einfriert. Darueber entscheidet allein
// loopFrames aus der animation.cfg (bg_panimate.cpp:4723).
int frameForAnimationIn(const std::vector<AnimEntry>& sections,
                                      const std::string& name, double sinceMs,
                                      bool unused_hold = false,
                                      float speed = 1.0F,
                                      float* fraction = nullptr,
                                      int* nextFrame = nullptr);


// Der Index eines Knochens, -1 wenn es ihn nicht gibt.
[[nodiscard]] int boneIndex(const GlaAnimation& anim, const std::string& name);

// Den Kopf drehen, ohne die Figur zu drehen.
//
// Gemeldet: "das mit den Kopfbewegungen klappt noch nicht."
//
// SET_LOOK_TARGET dreht in der Engine NICHT den Koerper. cg_players.cpp
// setzt damit renderInfo.headAngles, und die wirken ueber
// CG_UpdateAngleClamp (ebenda:3319 f.) nur auf den Kopf - mit Anschlaegen:
//
//     CG_UpdateAngleClamp( lookAngles[YAW],
//                          headYawClampMin/1.25, headYawClampMax/1.25, ... )
//
// headYawRangeLeft und -Right stehen in NPC_stats.cpp:2019 f. auf je 80
// Grad. Durch 1.25 bleiben 64 - so weit dreht ein Kopf, und keinen Grad
// weiter. Wer weiter schauen soll, muss den Koerper drehen, und das tut
// SET_WATCHTARGET.
//
// Gedreht wird um die SENKRECHTE durch den Ursprung des Knochens, samt
// aller Nachkommen: der cranium traegt die Gesichtsknochen, und ohne sie
// bekaeme die Figur ein verdrehtes Gesicht statt eines gedrehten Kopfes.
//
// Nur der Gierwinkel. Der Nickwinkel (headPitchRange 45, also 36 nach dem
// Teiler) fehlt noch - bei Figuren auf gleicher Hoehe faellt er kaum ins
// Gewicht, und ihn ohne Beleg dazuzuerfinden waere geraten.
void rotateBoneSubtreeYaw(const GlaAnimation& anim,
                          std::vector<BoneMatrix>& world, int bone,
                          float yawDeg);
}  // namespace bhed
#endif
