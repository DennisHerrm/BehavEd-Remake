// Kommen aus einer Orientierung wieder dieselben Winkel heraus?
//
// Das Drehgizmo im WELT-System geht diesen Weg:
//
//     Winkel -> Basis -> um eine Weltachse drehen -> Winkel
//
// Der erste und der letzte Schritt muessen zueinander passen, sonst
// springt die Kamera beim ersten Anfassen irgendwohin. Genau an dieser
// Stelle ist in diesem Projekt schon mehrfach die HAENDIGKEIT falsch
// gewesen - in MaxScript rechnet der Winkel-Achse-Konstruktor linkshaendig,
// und ein Kreuzprodukt in der falschen Reihenfolge dreht seit seiner
// Einfuehrung in die verkehrte Richtung. Deshalb prueft diese Probe nicht
// nur "es kommt etwas heraus", sondern die RICHTUNG.
#include "bhed/mapview.h"

#include <cmath>
#include <cstdio>

namespace {

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

// Winkel sind zyklisch: -180 und 180 sind derselbe Wert.
bool gleichWinkel(float a, float b, float toleranz = 0.05F) {
    float d = std::fmod(a - b + 540.0F, 360.0F) - 180.0F;
    return std::fabs(d) <= toleranz;
}

bool gleichVec(const float a[3], const float b[3], float toleranz = 0.001F) {
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(a[k] - b[k]) > toleranz) { return false; }
    }
    return true;
}

}  // namespace

int main() {
    using bhed::Camera;

    // --- 1. Rundlauf Winkel -> Basis -> Winkel --------------------------
    //
    // Ohne Roll zuerst, weil Camera::right()/up() den Roll gar nicht
    // kennen: sie bauen die Basis aus pitch und yaw. Ein Rundlauf muss
    // trotzdem auf denselben Winkeln landen.
    const float proben[][3] = {
        {0.0F, 0.0F, 0.0F},     {0.0F, 90.0F, 0.0F},
        {0.0F, -135.0F, 0.0F},  {30.0F, 45.0F, 0.0F},
        {-60.0F, 200.0F, 0.0F}, {45.0F, 12.5F, 0.0F},
        {-20.0F, -170.0F, 0.0F},
    };
    for (const auto& w : proben) {
        Camera c;
        for (int k = 0; k < 3; ++k) { c.angles[k] = w[k]; }
        float f[3];
        float u[3];
        c.forward(f);
        c.up(u);
        float zurueck[3];
        bhed::anglesFromBasis(f, u, zurueck);
        char text[96];
        std::snprintf(text, sizeof(text),
                      "Rundlauf  p=%.1f y=%.1f  ->  p=%.1f y=%.1f r=%.1f",
                      static_cast<double>(w[0]), static_cast<double>(w[1]),
                      static_cast<double>(zurueck[0]),
                      static_cast<double>(zurueck[1]),
                      static_cast<double>(zurueck[2]));
        expect(text, gleichWinkel(zurueck[0], w[0]) &&
                         gleichWinkel(zurueck[1], w[1]) &&
                         gleichWinkel(zurueck[2], 0.0F));
    }

    // --- 2. Der Roll kommt zurueck --------------------------------------
    //
    // Die Basis mit Roll bauen wir selbst: die Obenrichtung um die
    // Blickrichtung drehen. Dann muss anglesFromBasis genau diesen Roll
    // wiederfinden - mit dem richtigen VORZEICHEN.
    {
        Camera c;
        c.angles[0] = 15.0F;
        c.angles[1] = 40.0F;
        float f[3];
        float u[3];
        c.forward(f);
        c.up(u);
        for (float roll : {25.0F, -25.0F, 90.0F, -110.0F}) {
            float uRoll[3];
            bhed::rotateAboutAxis(u, f, roll, uRoll);
            float zurueck[3];
            bhed::anglesFromBasis(f, uRoll, zurueck);
            char text[96];
            std::snprintf(text, sizeof(text), "Roll %.0f kommt als %.1f zurueck",
                          static_cast<double>(roll),
                          static_cast<double>(zurueck[2]));
            expect(text, gleichWinkel(zurueck[2], roll) &&
                             gleichWinkel(zurueck[0], 15.0F) &&
                             gleichWinkel(zurueck[1], 40.0F));
        }
    }

    // --- 3. Rodrigues dreht in die RICHTIGE Richtung --------------------
    //
    // Um die Welt-Z-Achse (nach oben) muss +90 Grad die X-Achse auf die
    // Y-Achse legen - gegen den Uhrzeigersinn von oben gesehen. Das ist
    // dieselbe Drehrichtung, die auch der Yaw hat: bei yaw 0 blickt die
    // Kamera nach +X, bei yaw 90 nach +Y (siehe gizmotest).
    {
        const float z[3] = {0.0F, 0.0F, 1.0F};
        const float x[3] = {1.0F, 0.0F, 0.0F};
        float raus[3];
        bhed::rotateAboutAxis(x, z, 90.0F, raus);
        const float erwartet[3] = {0.0F, 1.0F, 0.0F};
        std::printf("  +90 um Welt-Z: %.2f %.2f %.2f\n",
                    static_cast<double>(raus[0]), static_cast<double>(raus[1]),
                    static_cast<double>(raus[2]));
        expect("X wird zu Y - Drehrichtung stimmt mit dem Yaw ueberein",
               gleichVec(raus, erwartet));
    }

    // --- 4. Eine ganze Umdrehung aendert nichts -------------------------
    {
        const float achse[3] = {0.0F, 1.0F, 0.0F};
        const float v[3] = {0.3F, -0.5F, 0.81F};
        float raus[3];
        bhed::rotateAboutAxis(v, achse, 360.0F, raus);
        expect("360 Grad landen wieder am Anfang", gleichVec(raus, v, 0.0005F));
    }

    // --- 5. Die Laenge bleibt erhalten ----------------------------------
    //
    // Eine Drehung darf nicht skalieren. Bliebe hier ein Fehler stehen,
    // wuerde die Kamera bei jedem Zug ein Stueck "einlaufen".
    {
        const float achse[3] = {0.57735F, 0.57735F, 0.57735F};
        const float v[3] = {1.0F, 0.0F, 0.0F};
        float raus[3];
        bhed::rotateAboutAxis(v, achse, 137.0F, raus);
        const float len = std::sqrt(raus[0] * raus[0] + raus[1] * raus[1] +
                                    raus[2] * raus[2]);
        std::printf("  Laenge nach 137 Grad: %.5f\n", static_cast<double>(len));
        expect("die Laenge bleibt eins", std::fabs(len - 1.0F) < 0.0005F);
    }

    // --- 6. Weltdrehung um Z ist eine reine Yaw-Aenderung ---------------
    //
    // Das ist der Fall, an dem man sofort merkt, ob Welt und Lokal
    // verwechselt sind: um die Welt-Z-Achse zu drehen heisst schwenken,
    // und der Pitch darf sich dabei NICHT aendern.
    {
        Camera c;
        c.angles[0] = 25.0F;
        c.angles[1] = 10.0F;
        float f[3];
        float u[3];
        c.forward(f);
        c.up(u);
        const float z[3] = {0.0F, 0.0F, 1.0F};
        float f2[3];
        float u2[3];
        bhed::rotateAboutAxis(f, z, 50.0F, f2);
        bhed::rotateAboutAxis(u, z, 50.0F, u2);
        float raus[3];
        bhed::anglesFromBasis(f2, u2, raus);
        std::printf("  um Welt-Z +50: p=%.1f y=%.1f r=%.1f\n",
                    static_cast<double>(raus[0]), static_cast<double>(raus[1]),
                    static_cast<double>(raus[2]));
        expect("der Yaw waechst um genau 50", gleichWinkel(raus[1], 60.0F));
        expect("der Pitch bleibt stehen", gleichWinkel(raus[0], 25.0F));
        expect("und es entsteht kein Roll", gleichWinkel(raus[2], 0.0F));
    }

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Winkelproben bestanden" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
