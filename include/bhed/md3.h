// md3.h - Raven/id-Modelle im .md3-Format lesen
//
// Das ist das Modellformat fuer alles, was KEINE Figur ist: Konsolen,
// Kisten, Rohre, Raumschiffe, Truemmer. Von den Karten der Mod verweisen 438
// Dateien darauf - nach den Texturen der zweithaeufigste Dateityp.
//
// Der Aufbau steht vollstaendig in code/qcommon/qfiles.h:
//
//   md3Header_t   ident "IDP3", version 15, name[64], flags,
//                 numFrames, numTags, numSurfaces, numSkins,
//                 ofsFrames, ofsTags, ofsSurfaces, ofsEnd
//   md3Surface_t  ident, name[64], flags, numFrames, numShaders,
//                 numVerts, numTriangles, ofsTriangles, ofsShaders,
//                 ofsSt, ofsXyzNormals, ofsEnd
//   md3Shader_t   name[64], shaderIndex
//   md3Triangle_t indexes[3]
//   md3St_t       st[2]
//   md3XyzNormal_t  short xyz[3], short normal
//
// Zwei Dinge sind daran eigen:
//
// 1. Die Ecken stehen als KURZE GANZZAHLEN, mit 1/64 skaliert
//    (MD3_XYZ_SCALE). Ein Wert von 640 heisst also 10 Einheiten.
//
// 2. Die Normale ist in ZWEI BYTES gepackt, als Laengen- und Breitengrad
//    auf der Einheitskugel. Die Engine packt sie so aus
//    (tr_surface.cpp, Zeile 1212):
//
//        lat = ( normal >> 8 ) & 0xff;
//        lng = ( normal      ) & 0xff;
//        x = cos(lat) * sin(lng)
//        y = sin(lat) * sin(lng)
//        z = cos(lng)
//
//    wobei lat und lng von 0..255 auf den vollen Kreis laufen.
//
// Ein .md3 kann mehrere Bilder (frames) enthalten - so entstehen die
// wehenden Fahnen und die drehenden Teile. Wir lesen ALLE und koennen
// zwischen ihnen ueberblenden, so wie die Engine es tut.
#ifndef BHED_MD3_H
#define BHED_MD3_H

#include "bhed/bspgeo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct Md3Vertex {
    float xyz[3]{};
    float normal[3]{};
    float st[2]{};
};

struct Md3Surface {
    std::string name;
    // Der Shadername der ersten Stufe. Ueber ihn kommt die Textur, genau
    // wie bei den Flaechen der Karte.
    std::string shader;
    int numVerts = 0;
    int numFrames = 0;
    // numVerts * numFrames Ecken, bildweise hintereinander. Die
    // Texturkoordinaten gelten fuer ALLE Bilder und stehen deshalb nur
    // einmal je Ecke - hier trotzdem in jeder Ecke, das spart eine zweite
    // Liste.
    std::vector<Md3Vertex> verts;
    std::vector<std::uint32_t> indexes;   // 3 je Dreieck, gilt fuer alle Bilder

    // Die Ecken EINES Bildes. frame laeuft um, damit ein zu grosser Wert
    // nicht ins Leere greift.
    //
    // Sass frueher an Md3Model, obwohl sie das Modell gar nicht anfasst -
    // nur die Flaeche. clang-tidy hat es gemeldet
    // (readability-convert-member-functions-to-static), und der Hinweis war
    // mehr wert als er aussah: nicht "mach sie static", sondern "sie haengt
    // an der falschen Klasse".
    [[nodiscard]] const Md3Vertex* frame(int index) const;
};

// Ein Tag: eine benannte Stelle im Modell samt Ausrichtung.
//
// Gefragt: "wo bekommen wir das Projektil her?"
//
// Von hier. Die Muendung einer Waffe ist ein Tag namens "tag_flash", und
// an ihr haengen der Muendungsblitz und der Startpunkt des Geschosses.
// An models/weapons2/blaster_r/blaster.md3 nachgemessen: zwei Tags,
// "tag_flash" bei -1.3/-11.7/-3.9 und "tag_weapon" im Ursprung.
//
// Der Satz ist md3Tag_t (qfiles.h:118) und misst 112 Byte:
//
//     char  name[64];
//     vec3  origin;
//     vec3  axis[3];
//
// Er steht JE BILD - ein Modell mit zehn Bildern und zwei Tags hat zwanzig
// Saetze. Bei Waffen ist numFrames 1, aber verlassen darf man sich darauf
// nicht.
struct Md3Tag {
    std::string name;
    float origin[3]{};
    // Zeilenweise: axis[0] ist die Vorwaertsachse. In JKA zeigt ein
    // tag_flash damit in Schussrichtung.
    float axis[3][3]{};
};

struct Md3Model {
    std::string name;
    int numFrames = 0;
    std::vector<Md3Surface> surfaces;
    // numTags * numFrames Saetze, BILDWEISE hintereinander - erst alle Tags
    // von Bild 0, dann alle von Bild 1.
    std::vector<Md3Tag> tags;
    int numTags = 0;

    [[nodiscard]] bool empty() const noexcept { return surfaces.empty(); }

    // Einen Tag bei Namen suchen, in einem bestimmten Bild.
    // Gibt nullptr, wenn es ihn nicht gibt.
    [[nodiscard]] const Md3Tag* tag(const std::string& name,
                                    int frame = 0) const;
};

[[nodiscard]] bool readMd3(const std::string& bytes, Md3Model& out,
                           std::string* error = nullptr);

// Ein Bild des Modells als BspMesh - damit es durch denselben Zeichenweg
// laeuft wie die Karte, die beweglichen Teile und die Effekte.
//
// Kein eigener Modellzeichner: die Ecken eines .md3 sind dieselbe Art
// Dreieck wie die einer Wand, nur aus einer anderen Datei. Je Flaeche
// entsteht ein Zeichenaufruf; shaderBase ist die Nummer, ab der die
// Texturen dieses Modells in TextureSet::byShader stehen.
//
// lightmap wird auf -3 gesetzt: Vertexfarben. Ein .md3 bringt keine
// Lightmap mit, und die der Karte passt nicht dazu.
[[nodiscard]] BspMesh md3ToMesh(const Md3Model& m, int frame, int shaderBase);

}  // namespace bhed
#endif
