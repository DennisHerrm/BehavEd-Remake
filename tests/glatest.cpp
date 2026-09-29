// glatest.cpp - Skelett und Bewegung
//
// Ohne eine echte .gla laesst sich der LESER nicht pruefen, die RECHNUNG
// dahinter aber vollstaendig - und die ist der Teil, an dem so etwas
// schiefgeht: eine Figur, die sich still verdreht, weil die Bones in der
// falschen Reihenfolge berechnet wurden.
#include "bhed/gla.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
bool near(float a, float b, float eps = 0.001F) { return std::fabs(a - b) < eps; }

bhed::BoneMatrix translation(float x, float y, float z) {
    bhed::BoneMatrix m = bhed::BoneMatrix::identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}
}  // namespace

int main() {
    // --- Matrizen ---------------------------------------------------------
    {
        const bhed::BoneMatrix id = bhed::BoneMatrix::identity();
        float in[3] = {3.0F, 4.0F, 5.0F};
        float out[3] = {0, 0, 0};
        id.transform(in, out);
        expect("die Einheitsmatrix laesst den Punkt stehen",
               near(out[0], 3.0F) && near(out[1], 4.0F) && near(out[2], 5.0F));

        const bhed::BoneMatrix t = translation(10.0F, 0.0F, 0.0F);
        t.transform(in, out);
        expect("Verschiebung wirkt", near(out[0], 13.0F) && near(out[1], 4.0F));

        // Zwei Verschiebungen hintereinander addieren sich.
        const bhed::BoneMatrix both = t * translation(0.0F, 5.0F, 0.0F);
        both.transform(in, out);
        expect("zwei Verschiebungen addieren sich",
               near(out[0], 13.0F) && near(out[1], 9.0F));

        // Eine Drehung um 90 Grad um Z: X wird zu Y.
        bhed::BoneMatrix rot = bhed::BoneMatrix::identity();
        rot.m[0][0] = 0.0F;
        rot.m[0][1] = -1.0F;
        rot.m[1][0] = 1.0F;
        rot.m[1][1] = 0.0F;
        float unit[3] = {1.0F, 0.0F, 0.0F};
        rot.transform(unit, out);
        expect("Drehung um 90 Grad dreht X auf Y",
               near(out[0], 0.0F) && near(out[1], 1.0F));

        // Reihenfolge zaehlt: erst drehen, dann schieben ist nicht dasselbe
        // wie umgekehrt.
        const bhed::BoneMatrix a = rot * translation(2.0F, 0.0F, 0.0F);
        const bhed::BoneMatrix b = translation(2.0F, 0.0F, 0.0F) * rot;
        float ra[3];
        float rb[3];
        float zero[3] = {0, 0, 0};
        a.transform(zero, ra);
        b.transform(zero, rb);
        expect("die Reihenfolge der Matrizen zaehlt",
               !(near(ra[0], rb[0]) && near(ra[1], rb[1])));
    }

    // --- Topologische Reihenfolge -----------------------------------------
    //
    // Der Fall aus g2c: in Ravens _humanoid.gla haben acht Bones ihren
    // Elternteil HINTER sich. Wer nach Index rechnet, benutzt eine noch
    // nicht berechnete Elternmatrix - und die Figur verdreht sich still.
    {
        bhed::GlaAnimation a;
        a.bones.resize(4);
        a.bones[0].parent = 2;    // Elternteil steht HINTER dem Kind
        a.bones[1].parent = 0;
        a.bones[2].parent = -1;   // Wurzel
        a.bones[3].parent = 1;

        const std::vector<int> order = a.topologicalOrder();
        expect("alle Bones kommen vor", order.size() == 4);

        // Jeder Bone muss NACH seinem Elternteil kommen.
        std::vector<int> position(4, -1);
        for (std::size_t i = 0; i < order.size(); ++i) {
            position[static_cast<std::size_t>(order[i])] = static_cast<int>(i);
        }
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
            const int p = a.bones[static_cast<std::size_t>(i)].parent;
            if (p >= 0 && position[static_cast<std::size_t>(p)] >
                              position[static_cast<std::size_t>(i)]) {
                ok = false;
            }
        }
        expect("jeder Bone kommt nach seinem Elternteil", ok);
        expect("die Wurzel kommt zuerst", !order.empty() && order[0] == 2);

        // Ein Kreis darf nicht in eine Endlosschleife fuehren.
        bhed::GlaAnimation c;
        c.bones.resize(3);
        c.bones[0].parent = 1;
        c.bones[1].parent = 2;
        c.bones[2].parent = 0;
        const std::vector<int> circle = c.topologicalOrder();
        expect("ein Kreis wird nicht verschluckt", circle.size() == 3);
    }

    // --- Weltmatrizen -----------------------------------------------------
    {
        bhed::GlaAnimation a;
        a.bones.resize(2);
        a.bones[0].parent = -1;
        a.bones[0].basePose = bhed::BoneMatrix::identity();
        a.bones[0].basePoseInv = bhed::BoneMatrix::identity();
        a.bones[1].parent = 0;
        a.bones[1].basePose = translation(0.0F, 0.0F, 10.0F);
        a.bones[1].basePoseInv = translation(0.0F, 0.0F, -10.0F);
        a.numFrames = 0;   // ohne Bilder: localMatrix liefert die Einheit

        std::vector<bhed::BoneMatrix> world;
        a.worldMatrices(0, world);
        expect("zwei Weltmatrizen", world.size() == 2);
        // Ohne Bewegung steht das Kind in seiner Grundstellung.
        float out[3];
        float zero[3] = {0, 0, 0};
        world[1].transform(zero, out);
        expect("das Kind steht in seiner Grundstellung", near(out[2], 10.0F));
    }

    // --- Kaputte Eingaben -------------------------------------------------
    {
        bhed::GlaAnimation a;
        expect("leere Datei wird abgewiesen", !bhed::readGla("", a, nullptr));
        expect("Muell wird abgewiesen",
               !bhed::readGla(std::string(300, '\x01'), a, nullptr));
        std::string fake = "2LGA";
        fake.resize(300, '\0');
        (void)bhed::readGla(fake, a, nullptr);   // darf nur nicht knallen
        expect("richtige Kennung ohne Inhalt ueberstanden", true);
    }

    // --- animation.cfg ----------------------------------------------------
    {
        const char* cfg =
            "// 30384 frames; 1683 sequences\r\n"
            "//\r\n"
            "BOTH_STAND1         \t14500\t2\t0\t20\r\n"
            "BOTH_WALK1          \t30293\t30\t0\t20\r\n"
            "BOTH_DEATH1         \t3179\t41\t-1\t20\r\n"
            "kaputt ohne Zahlen\r\n";
        const std::vector<bhed::AnimEntry> a = bhed::parseAnimationCfg(cfg);
        expect("drei Abschnitte gelesen", a.size() == 3);
        if (a.size() == 3) {
            expect("BOTH_STAND1 ab Bild 14500",
                   a[0].name == "BOTH_STAND1" && a[0].firstFrame == 14500 &&
                       a[0].numFrames == 2);
            expect("BOTH_WALK1 laeuft in einer Schleife", a[1].loopFrame == 0);
            expect("BOTH_DEATH1 nicht", a[2].loopFrame == -1);
            expect("Tempo gelesen", a[2].fps == 20);
        }
        for (const char* bad : {"", "//", "nur_name", "a b", "\n\n"}) {
            (void)bhed::parseAnimationCfg(bad);
        }
        expect("kaputte cfg ueberstanden", true);
    }

    // --- Ein unbekannter Animationsname ist KEIN Bild 0 --------------------
    //
    // Gemeldet: "die Jango-Figur hat keine Animation, sie steht in der
    // Root-Pose fest."
    //
    // Der Grund war ein Tippfehler in der Mission - intro_jedi.ibi setzt
    // "BOTH_MD_CIN4", die Animation heisst aber "BOTH_MD_CIN_4" mit
    // Unterstrich. Die Engine tut in so einem Fall NICHTS
    // (Q3_Interface.cpp:2237 und bg_panimate.cpp:5531, beide return
    // qfalse), behaved lieferte Bild 0 - und Bild 0 ist ein GUELTIGES Bild.
    // In _humanoid_jango liegt dort FACE_ALERT, eine Gesichtsanimation ohne
    // Koerperhaltung. Also die Grundstellung.
    //
    // Die Zahlen stammen aus der echten _humanoid_jango/animation.cfg.
    {
        const std::vector<bhed::AnimEntry> a = bhed::parseAnimationCfg(
            "FACE_ALERT           0\t2\t-1\t1\r\n"
            "BOTH_MD_CIN_4    39417\t100\t0\t35\r\n"
            "BOTH_STAND1      47982\t1\t0\t30\r\n"
            "ROOT             48436\t2\t-1\t29\r\n");
        expect("die cfg hat vier Abschnitte", a.size() == 4);
        expect("ein bekannter Name liefert sein erstes Bild",
               bhed::frameForAnimationIn(a, "BOTH_STAND1", 0.0, false) == 47982);
        // DAS ist die Probe. Eine 0 hier waere der gemeldete Fehler.
        expect("der Tippfehler aus der Mission liefert -1, nicht 0",
               bhed::frameForAnimationIn(a, "BOTH_MD_CIN4", 0.0, false) == -1);
        expect("und der richtige Name daneben liefert sein Bild",
               bhed::frameForAnimationIn(a, "BOTH_MD_CIN_4", 0.0, false) == 39417);
        expect("ein leerer Name ebenfalls -1",
               bhed::frameForAnimationIn(a, "", 0.0, false) == -1);
        expect("und eine leere cfg auch",
               bhed::frameForAnimationIn({}, "BOTH_STAND1", 0.0, false) == -1);
        // Der Unterschied zwischen -1 und 0 muss ECHT sein: FACE_ALERT liegt
        // wirklich auf Bild 0. Waere hier keine Animation auf Bild 0, koennte
        // die Probe oben auch mit der alten Fassung bestehen.
        expect("Bild 0 ist ein gueltiges Bild dieser cfg",
               bhed::frameForAnimationIn(a, "FACE_ALERT", 0.0, false) == 0);
    }

    // --- Den Kopf drehen, ohne die Figur zu drehen -------------------------
    //
    // Gemeldet: "das mit den Kopfbewegungen klappt noch nicht."
    //
    // Ein Skelett von Hand, mit den Namen aus _humanoid und einem Kopf, der
    // NICHT im Nullpunkt steht: 100/200 in der Ebene. Genau darauf kommt es
    // an - wer um den Weltnullpunkt statt um den Knochen dreht, faellt bei
    // einer Figur nahe am Nullpunkt nicht auf.
    {
        bhed::GlaAnimation a;
        a.bones.resize(4);
        a.bones[0].name = "pelvis";   a.bones[0].parent = -1;
        a.bones[1].name = "thoracic"; a.bones[1].parent = 0;
        a.bones[2].name = "cranium";  a.bones[2].parent = 1;
        a.bones[3].name = "face";     a.bones[3].parent = 2;

        expect("cranium wird gefunden", bhed::boneIndex(a, "cranium") == 2);
        expect("ein unbekannter Name gibt -1",
               bhed::boneIndex(a, "gibtsnicht") == -1);

        std::vector<bhed::BoneMatrix> ruhe(4, bhed::BoneMatrix::identity());
        ruhe[0].m[0][3] = 100.0F; ruhe[0].m[1][3] = 200.0F;  // Huefte
        ruhe[1].m[0][3] = 100.0F; ruhe[1].m[1][3] = 200.0F; ruhe[1].m[2][3] = 40.0F;
        ruhe[2].m[0][3] = 100.0F; ruhe[2].m[1][3] = 200.0F; ruhe[2].m[2][3] = 70.0F;
        // Das Gesicht sitzt zehn Einheiten VOR dem Kopf, in +x.
        ruhe[3].m[0][3] = 110.0F; ruhe[3].m[1][3] = 200.0F; ruhe[3].m[2][3] = 70.0F;

        std::vector<bhed::BoneMatrix> g = ruhe;
        bhed::rotateBoneSubtreeYaw(a, g, 2, 90.0F);

        expect("der Kopf bleibt an seinem Ort",
               near(g[2].m[0][3], 100.0F, 0.01F) &&
                   near(g[2].m[1][3], 200.0F, 0.01F) &&
                   near(g[2].m[2][3], 70.0F, 0.01F));
        // Um 90 Grad um die Senkrechte: aus (x,y) wird (-y,x).
        expect("und er ist um 90 Grad gedreht",
               near(g[2].m[0][0], 0.0F, 0.01F) &&
                   near(g[2].m[1][0], 1.0F, 0.01F));
        // Das Gesicht stand 10 in +x vor dem Kopf, jetzt steht es 10 in +y.
        // Ohne diese Zusicherung bestuende die Probe auch, wenn nur der eine
        // Knochen gedreht wuerde und das Gesicht stehenbliebe.
        expect("das Gesicht dreht mit",
               near(g[3].m[0][3], 100.0F, 0.01F) &&
                   near(g[3].m[1][3], 210.0F, 0.01F));
        // Und die Huefte liegt UEBER dem Kopf in der Kette, nicht darunter.
        expect("die Huefte bleibt unberuehrt",
               near(g[0].m[0][3], 100.0F, 0.001F) &&
                   near(g[0].m[0][0], 1.0F, 0.001F));
        expect("der Oberkoerper ebenfalls",
               near(g[1].m[0][0], 1.0F, 0.001F));

        std::vector<bhed::BoneMatrix> null = ruhe;
        bhed::rotateBoneSubtreeYaw(a, null, 2, 0.0F);
        expect("null Grad laesst alles stehen",
               near(null[3].m[0][3], 110.0F, 0.0001F));

        std::vector<bhed::BoneMatrix> fehlt = ruhe;
        bhed::rotateBoneSubtreeYaw(a, fehlt, -1, 45.0F);
        expect("ein unbekannter Knochen laesst alles stehen",
               near(fehlt[3].m[0][3], 110.0F, 0.0001F));
    }

    // --- Ein eigenes Bild fuer einen Teilbaum ------------------------------
    //
    // So teilt die Engine Ober- und Unterkoerper. Die Vererbungsregel steht
    // in tr_ghoul2.cpp, CBoneCache::EvalLow (Zeile 138 ff.): ein Knochen
    // erbt erst das Bild seines Elternteils und ueberschreibt es nur, wenn
    // fuer IHN eine Animation gesetzt wurde.
    //
    // Geprueft wird an einem Skelett mit ZWEI Bildern, die sich unter-
    // scheiden - sonst waere jede Aufteilung unsichtbar.
    {
        bhed::GlaAnimation a;
        a.bones.resize(4);
        a.bones[0].name = "model_root";   a.bones[0].parent = -1;
        a.bones[1].name = "ltibia";       a.bones[1].parent = 0;   // Bein
        a.bones[2].name = "lower_lumbar"; a.bones[2].parent = 0;   // Taille
        a.bones[3].name = "cranium";      a.bones[3].parent = 2;   // Kopf
        for (auto& b : a.bones) {
            b.basePose = bhed::BoneMatrix::identity();
            b.basePoseInv = bhed::BoneMatrix::identity();
        }
        // Zwei Bilder, ein Knochenvorrat: Bild 0 verschiebt um x=0,
        // Bild 1 um x=100. localMatrix liest aus bonePool/indexes.
        a.numFrames = 2;
        a.indexes.assign(2 * 4, 0);
        // Eintrag 0 = Ruhe, Eintrag 1 = verschoben. Alle Knochen in Bild 1
        // nehmen Eintrag 1.
        a.bonePool.assign(2 * 14, 0);
        // Die Aufloesung des Vorrats ist Sache von localMatrix; hier
        // genuegt, dass die beiden Eintraege VERSCHIEDEN sind.
        a.bonePool[14] = 0x7F;
        // WICHTIG: nur die Knochen AB der Taille unterscheiden sich
        // zwischen den beiden Bildern. Wurzel und Bein bleiben gleich.
        //
        // Sonst vergleicht die Probe Aepfel mit Birnen: out[b] enthaelt die
        // Kette bis zur Wurzel, und wenn schon die Wurzel ein anderes Bild
        // hat, unterscheidet sich JEDER Knochen - egal ob die Aufteilung
        // funktioniert. Der erste Anlauf ist genau daran gescheitert, und
        // zwar zu Recht.
        a.indexes[static_cast<std::size_t>(4 + 2)] = 1;   // Bild 1, Taille
        a.indexes[static_cast<std::size_t>(4 + 3)] = 1;   // Bild 1, Kopf

        std::vector<bhed::BoneMatrix> nur0;
        std::vector<bhed::BoneMatrix> nur1;
        a.worldMatrices(0, nur0);
        a.worldMatrices(1, nur1);
        const bool unterschiedlich =
            !near(nur0[3].m[0][0], nur1[3].m[0][0], 0.0001F) ||
            !near(nur0[3].m[0][3], nur1[3].m[0][3], 0.0001F);
        expect("die beiden Bilder unterscheiden sich ueberhaupt",
               unterschiedlich);

        // Jetzt: Koerper auf Bild 0, Teilbaum ab lower_lumbar auf Bild 1.
        std::vector<bhed::BoneMatrix> geteilt;
        a.worldMatrices(0, {bhed::BoneFrameOverride{2, 1}}, geteilt);

        expect("das Bein folgt dem Koerperbild",
               near(geteilt[1].m[0][0], nur0[1].m[0][0], 0.0001F) &&
                   near(geteilt[1].m[0][3], nur0[1].m[0][3], 0.0001F));
        expect("die Taille nimmt das eigene Bild",
               near(geteilt[2].m[0][0], nur1[2].m[0][0], 0.0001F) &&
                   near(geteilt[2].m[0][3], nur1[2].m[0][3], 0.0001F));
        // Das ist die Vererbung: der Kopf hat KEINEN eigenen Eintrag und
        // muss trotzdem dem Oberkoerper folgen, nicht dem Koerper.
        expect("der Kopf erbt das Bild der Taille",
               near(geteilt[3].m[0][0], nur1[3].m[0][0], 0.0001F) &&
                   near(geteilt[3].m[0][3], nur1[3].m[0][3], 0.0001F));
        // Und die Wurzel bleibt beim Koerperbild.
        expect("die Wurzel bleibt beim Koerperbild",
               near(geteilt[0].m[0][3], nur0[0].m[0][3], 0.0001F));

        // Ein Vorrang auf einen unbekannten Knochen aendert nichts.
        std::vector<bhed::BoneMatrix> egal;
        a.worldMatrices(0, {bhed::BoneFrameOverride{-1, 1}}, egal);
        expect("ein Vorrang ohne Knochen aendert nichts",
               near(egal[3].m[0][3], nur0[3].m[0][3], 0.0001F));

        // Der Oberkoerper wird im EIGENEN Takt zwischen zwei Bildern
        // gemischt, der Koerper bleibt dabei stehen.
        std::vector<bhed::BoneMatrix> halb;
        bhed::BoneFrameOverride zwischen{2, 0};
        zwischen.next = 1;
        zwischen.fraction = 0.5F;
        a.worldMatricesLerp(0, 0, 0.0F, {zwischen}, halb);
        expect("Oberkoerper: halb zwischen seinen zwei Bildern",
               near(halb[2].m[0][3], 0.5F * (nur0[2].m[0][3] + nur1[2].m[0][3]), 0.01F) &&
                   near(halb[2].m[0][0], 0.5F * (nur0[2].m[0][0] + nur1[2].m[0][0]), 0.01F));
        expect("... und das Bein bleibt beim Koerperbild",
               near(halb[1].m[0][3], nur0[1].m[0][3], 0.0001F));
    }

    // --- Wiederholen entscheidet loopFrames, NICHT die Haltezeit -----------
    //
    // Gemeldet: "die Animationen hoeren zum Teil auf zu loopen."
    //
    // Die Stelle hatte zwei verschiedene Dinge vermengt. Nachgelesen sind
    // es zwei Paar Schuhe:
    //
    // **loopFrames entscheidet, OB sie laeuft** (bg_panimate.cpp:4723):
    //
    //     animFlags = (curAnim.loopFrames != -1) ? BONE_ANIM_OVERRIDE_LOOP
    //                                            : BONE_ANIM_OVERRIDE_FREEZE;
    //
    // **Die Haltezeit entscheidet, WIE LANGE sie nicht ersetzt werden
    // darf** (PM_SetLegsAnimTimer, bg_panimate.cpp:4390). Der Kommentar
    // dort sagt es ausdruecklich: "let it be -1 if that was intentional".
    // Von Einfrieren steht nichts.
    //
    // Weil die -1 einundsiebzig Prozent aller Haltezeiten ausmacht, traf
    // der Fehler fast alles.
    {
        std::vector<bhed::AnimEntry> secs;
        // Eine LAUFENDE Animation: loopFrame 0, zehn Bilder, 10 je Sekunde.
        secs.push_back(bhed::AnimEntry{"BOTH_WALK1", 100, 10, 0, 10});
        // Eine EINMALIGE: loopFrame -1.
        secs.push_back(bhed::AnimEntry{"BOTH_STAND1", 200, 10, -1, 10});

        // Nach 1,5 Sekunden ist Bild 15 faellig - bei zehn Bildern also
        // wieder Bild 5.
        expect("die laufende wiederholt sich",
               bhed::frameForAnimationIn(secs, "BOTH_WALK1", 1500.0) == 105);
        // DAS ist der Fall, der falsch war: mit Haltezeit blieb sie stehen.
        expect("und zwar AUCH mit Haltezeit",
               bhed::frameForAnimationIn(secs, "BOTH_WALK1", 1500.0, true) ==
                   105);
        // Die einmalige bleibt auf dem letzten Bild - mit und ohne.
        expect("die einmalige bleibt stehen",
               bhed::frameForAnimationIn(secs, "BOTH_STAND1", 1500.0) == 209);
        expect("auch ohne Haltezeit",
               bhed::frameForAnimationIn(secs, "BOTH_STAND1", 1500.0, false) ==
                   209);
        // Ein Name, den es nicht gibt, meldet -1 statt Bild 0 - sonst
        // stuende die Figur stumm in einer fremden Pose.
        expect("ein unbekannter Name meldet nichts",
               bhed::frameForAnimationIn(secs, "GIBTS_NICHT", 0.0) == -1);
    }

    // --- Die Animation laeuft MIT dem Tempo --------------------------------
    //
    // Gemeldet: "statt sauber ueber den Boden zu laufen slidet er dauerhaft
    // ein bisschen."
    //
    // bg_panimate.cpp:4824:
    //
    //     animSpeed *= (gent->resultspeed / moveSpeedOfAnim);
    //
    // Die Animation ist fuer EINE Geschwindigkeit gebaut - 50 beim Gehen,
    // 150 beim Laufen (ebenda:4778 ff.). Wer schneller geht, muss sie
    // schneller abspielen, sonst machen die Fuesse zu wenig Schritte fuer
    // den Weg.
    //
    // Der Schalter dafuer ist standardmaessig AN (g_noFootSlide, Vorgabe
    // "1", g_main.cpp:640) - es ist der Normalfall, kein Sonderweg.
    {
        std::vector<bhed::AnimEntry> secs;
        // Zwanzig Bilder bei 10 fps: ein Bild alle 100 ms.
        secs.push_back(bhed::AnimEntry{"BOTH_WALK1", 100, 20, 0, 10});

        // Mit Faktor 1 ist nach 500 ms Bild 5 faellig.
        expect("mit Faktor 1 wie in der .cfg",
               bhed::frameForAnimationIn(secs, "BOTH_WALK1", 500.0, false,
                                         1.0F) == 105);
        // Mit Faktor 2 doppelt so weit: Bild 10.
        expect("mit Faktor 2 doppelt so schnell",
               bhed::frameForAnimationIn(secs, "BOTH_WALK1", 500.0, false,
                                         2.0F) == 110);
        // Mit Faktor 0,5 halb so weit.
        expect("mit Faktor 0,5 halb so schnell",
               bhed::frameForAnimationIn(secs, "BOTH_WALK1", 500.0, false,
                                         0.5F) == 102);
        // Null oder negativ darf NICHT durch null teilen. Die Engine
        // klemmt bei 0.01 (bg_panimate.cpp:4826); hier genuegt, dass ein
        // gueltiges Bild herauskommt statt eines Absturzes.
        const int b = bhed::frameForAnimationIn(secs, "BOTH_WALK1", 500.0,
                                                false, 0.0F);
        expect("Faktor null stuerzt nicht ab", b >= 100 && b < 120);
    }

    // --- Der Anteil zwischen zwei Bildern ---------------------------------
    //
    // Gemeldet: "er stottert immer noch beim Laufen."
    //
    // behaved sprang von Bild zu Bild. Bei 20 Bildern je Sekunde sind das
    // zwanzig Stufen, waehrend der Bildschirm sechzig oder mehr zeigt -
    // und genau das sieht man als Stottern.
    //
    // Die Engine mischt zwischen zwei Bildern (tr_ghoul2.cpp:1531 ff.):
    //
    //     for ( j = 0 ; j < 12 ; j++ )
    //         tbone[2][j] = backlerp*tbone[0][j] + frontlerp*tbone[1][j];
    {
        std::vector<bhed::AnimEntry> secs;
        // Zehn Bilder bei 10 fps: eines alle 100 ms.
        secs.push_back(bhed::AnimEntry{"BOTH_WALK1", 100, 10, 0, 10});
        float f = -1.0F;
        int nx = -99;

        (void)bhed::frameForAnimationIn(secs, "BOTH_WALK1", 0.0, false, 1.0F, &f,
                                  &nx);
        expect("am Bildanfang ist der Anteil null",
               near(f, 0.0F, 0.001F) && nx == 101);
        (void)bhed::frameForAnimationIn(secs, "BOTH_WALK1", 50.0, false, 1.0F, &f,
                                  &nx);
        expect("in der Mitte ist er ein halb", near(f, 0.5F, 0.001F));

        // DER Fall, der leicht falsch wird: am Ende des Zyklus muss das
        // naechste Bild wieder das ERSTE sein, nicht eines dahinter.
        // Sonst ruckt es genau einmal je Umlauf - und das waere ein
        // Stottern, das man fuer geloest haelt.
        (void)bhed::frameForAnimationIn(secs, "BOTH_WALK1", 950.0, false, 1.0F, &f,
                                  &nx);
        expect("am Zyklusende geht es auf das erste Bild zurueck",
               nx == 100 && near(f, 0.5F, 0.001F));

        // Bei einer EINMALIGEN Animation bleibt es auf dem letzten stehen.
        std::vector<bhed::AnimEntry> einmal;
        einmal.push_back(bhed::AnimEntry{"BOTH_STAND1", 200, 10, -1, 10});
        (void)bhed::frameForAnimationIn(einmal, "BOTH_STAND1", 5000.0, false, 1.0F,
                                  &f, &nx);
        expect("die einmalige mischt nicht ueber das Ende hinaus",
               nx == 209);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Skelettproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
