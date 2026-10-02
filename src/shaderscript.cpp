#include <cstdlib>
#include "bhed/shaderscript.h"

#include "bhed/num.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace bhed {

namespace {

// NameToSrcBlendMode (tr_shader.cpp:314) und NameToDstBlendMode (:361).
// Die Engine hat zwei getrennte Tabellen, weil OpenGL nicht jeden Faktor auf
// beiden Seiten erlaubt; fuer uns genuegt eine, weil wir ohnehin nur rechnen.
BlendFactor faktor(const std::string& name, BlendFactor wenn_unbekannt) {
    if (name == "gl_one") { return BlendFactor::One; }
    if (name == "gl_zero") { return BlendFactor::Zero; }
    if (name == "gl_src_color") { return BlendFactor::SrcColor; }
    if (name == "gl_one_minus_src_color") {
        return BlendFactor::OneMinusSrcColor;
    }
    if (name == "gl_src_alpha") { return BlendFactor::SrcAlpha; }
    if (name == "gl_one_minus_src_alpha") {
        return BlendFactor::OneMinusSrcAlpha;
    }
    if (name == "gl_dst_color") { return BlendFactor::DstColor; }
    if (name == "gl_one_minus_dst_color") {
        return BlendFactor::OneMinusDstColor;
    }
    if (name == "gl_dst_alpha") { return BlendFactor::DstAlpha; }
    if (name == "gl_one_minus_dst_alpha") {
        return BlendFactor::OneMinusDstAlpha;
    }
    if (name == "gl_src_alpha_saturate") {
        return BlendFactor::SrcAlphaSaturate;
    }
    return wenn_unbekannt;
}

}  // namespace
namespace {

// Eine Zahl aus einem Wort. Kein Wert heisst null - so macht es auch
// atof(), auf das sich der Shaderleser der Engine stuetzt.
float readNum(const std::string& w) {
    float v = 0.0F;
    (void)parseFloat(w, v);
    return v;
}

}  // namespace

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < b && sp(s[a])) { ++a; }
    while (b > a && sp(s[b - 1])) { --b; }
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Erstes Wort einer Zeile.
std::string firstWord(const std::string& s) {
    const std::size_t sp = s.find_first_of(" \t");
    return sp == std::string::npos ? s : s.substr(0, sp);
}

std::string secondWord(const std::string& s) {
    const std::size_t sp = s.find_first_of(" \t");
    if (sp == std::string::npos) {
        return {};
    }
    return trim(s.substr(sp + 1));
}

}  // namespace

void parseFogParms(const std::string& text, NebelMap& out) {
    std::string name;
    int depth = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = trim(line.substr(0, slash));
        }
        if (line.empty()) {
            continue;
        }
        // Klammern koennen am Zeilenende oder allein stehen.
        if (line == "{") { ++depth; continue; }
        if (line == "}") { depth = std::max(0, depth - 1); continue; }
        if (depth == 0) {
            if (line.back() == '{') {
                name = trim(line.substr(0, line.size() - 1));
                depth = 1;
            } else {
                name = line;
            }
            continue;
        }
        if (depth == 1 && lower(firstWord(line)) == "fogparms") {
            std::string zahlen = secondWord(line);
            for (char& c : zahlen) {
                if (c == '(' || c == ')') { c = ' '; }
            }
            NebelParms p;
            float v[4] = {1.0F, 0.0F, 0.0F, 250.0F};
            int n = 0;
            std::size_t pos = 0;
            while (n < 4 && pos < zahlen.size()) {
                const char* anfang = zahlen.c_str() + pos;
                char* ende = nullptr;
                const float x = std::strtof(anfang, &ende);
                if (ende == anfang) {
                    ++pos;
                    continue;
                }
                v[n++] = x;
                pos = static_cast<std::size_t>(ende - zahlen.c_str());
            }
            if (n == 4 && !name.empty()) {
                p.farbe[0] = v[0];
                p.farbe[1] = v[1];
                p.farbe[2] = v[2];
                p.tiefe = v[3];
                out[lower(name)] = p;
            }
        }
        if (line.back() == '{') { ++depth; }
        if (line.front() == '}') { depth = std::max(0, depth - 1); }
    }
}

void parseShaderScript(const std::string& text, ShaderMap& out) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        // Kommentare abschneiden. // kommt in .shader-Dateien haeufig vor.
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = trim(line.substr(0, slash));
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }

    std::string name;
    int depth = 0;
    std::string found;
    // tcMod scroll der Stufe, aus der auch das Bild stammt. Weitere Stufen
    // zeichnen wir ohnehin nicht, also interessiert ihre Bewegung nicht.
    TexMod texMods[kMaxTexMods];
    int numTexMods = 0;
    // Sind wir gerade IN der Stufe, aus der das Bild kam?
    bool inImageStage = false;
    // Das tcMod DIESER Stufe, gesammelt bis zu ihrem Ende.
    TexMod stageTexMods[kMaxTexMods];
    int stageNumTexMods = 0;
    std::vector<std::string> stageAnim;
    float stageAnimFreq = 0.0F;
    bool stageAnimOneShot = false;
    std::vector<std::string> animImages;
    float animFreq = 0.0F;
    bool animOneShot = false;
    BlendMode blend = BlendMode::Opaque;
    BlendFactor stageSrc = BlendFactor::One;
    BlendFactor stageDst = BlendFactor::Zero;
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
    // Wie viele Bildstufen dieser Shader hat - siehe ShaderInfo::imageStages.
    int stufen = 0;
    AlphaTest alphaTest = AlphaTest::None;
    AlphaTest stageAlpha = AlphaTest::None;
    float stageAlphaConst = -1.0F;   // alphaGen const, negativ = nicht gesetzt
    float alphaConst = -1.0F;
    bool autosprite = false;
    // Die einzelne Stufe im Bau, und die fertige Liste - siehe
    // ShaderInfo::stages.
    // Hat DIESE Stufe das gemeldete Bild geliefert? Nur dann gelten ihre
    // Einzelfelder - siehe beim Stufenende.
    bool stageHatFound = false;
    std::string stageImage;
    bool stageLightmap = false;
    std::vector<ShaderInfo::Stage> stages;
    BlendMode stageBlend = BlendMode::Opaque;
    CullMode cull = CullMode::Front;
    Wave rgbWave;
    Wave stageRgb;
    Wave deformWave;
    float deformSpread = 0.0F;
    bool polygonOffset = false;
    std::string skyBox;
    // `glow` dieser Stufe, und ob irgendeine Stufe des Shaders es traegt.
    bool stageGlow = false;
    bool glow = false;
    bool hasGlow = false;
    // tcGen dieser Stufe und der Bildstufe - siehe TexGen.
    TexGen stageTexGen = TexGen::Base;
    TexGen texGen = TexGen::Base;
    AlphaGen stageAlphaGen = AlphaGen::FromImage;
    float stageRgbConst[3]{-1.0F, -1.0F, -1.0F};
    float rgbConst[3]{-1.0F, -1.0F, -1.0F};
    // rgbGen vertex/exactVertex/... - siehe ShaderInfo::rgbVertex.
    bool stageRgbVertex = false;
    bool rgbVertexImage = false;
    bool stageClamp = false;
    bool clampImage = false;
    bool stageDepthWrite = false;
    DepthFunc stageDepthFunc = DepthFunc::LEqual;
    bool depthWriteImage = false;
    DepthFunc depthFuncImage = DepthFunc::LEqual;
    AlphaGen alphaGenImage = AlphaGen::FromImage;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (line == "{") {
            ++depth;
            if (depth == 2) {
                stageNumTexMods = 0;
                stageAnim.clear();
                stageAnimFreq = 0.0F;
                stageAnimOneShot = false;
                stageBlend = BlendMode::Opaque;
                stageAlpha = AlphaTest::None;
                stageRgb = Wave{};
            }
            continue;
        }
        if (line == "}") {
            --depth;
            if (depth == 1) {
                // Stufenende. Hat DIESE Stufe das Bild geliefert, gilt ihr
                // tcMod - egal, ob es vor oder nach dem map-Befehl stand.
                // Deshalb wird es die ganze Stufe lang gesammelt und erst
                // hier uebernommen.
                // Die EINZELFELDER nur aus der Stufe, die auch `found`
                // geliefert hat.
                //
                // Beim Oeffnen des Lesers fuer alle Stufen war das zuerst
                // nicht so - und zwei bestehende Zusicherungen haben es
                // sofort gemeldet ("tcMod einer anderen Stufe gilt nicht",
                // "der Test ueberlebt die spaeteren Stufen"). Sie halten
                // genau richtig fest, dass eine spaetere Stufe den
                // Alphatest und das Scrollen der Bildstufe nicht
                // ueberschreiben darf.
                //
                // Die Stufenliste sammelt trotzdem ALLE - sie ist der neue
                // Weg, die Einzelfelder sind der alte, und beide bleiben
                // fuer sich richtig.
                // ZWEI verschiedene Dinge, und sie duerfen nicht
                // dieselbe Bedingung haben.
                //
                // Die EINZELFELDER (`blend`, `alphaTest`, `scroll`) gelten
                // nur aus der Stufe, die auch das gemeldete Bild geliefert
                // hat - sonst ueberschreibt eine spaetere Stufe sie, und
                // genau das haben in rc342 zwei bestehende Proben gemeldet.
                if (inImageStage && stageHatFound) {
                    for (int k = 0; k < stageNumTexMods; ++k) {
                        texMods[k] = stageTexMods[k];
                    }
                    numTexMods = stageNumTexMods;
                    animImages = stageAnim;
                    animFreq = stageAnimFreq;
                    animOneShot = stageAnimOneShot;
                    blend = stageBlend;
                    alphaTest = stageAlpha;
                    rgbWave = stageRgb;
                    alphaConst = stageAlphaConst;
                    glow = stageGlow;
                    texGen = stageTexGen;
                    srcFactor = stageSrc;
                    dstFactor = stageDst;
                    for (int k = 0; k < 3; ++k) {
                        rgbConst[k] = stageRgbConst[k];
                    }
                    rgbVertexImage = stageRgbVertex;
                    clampImage = stageClamp;
                    depthWriteImage = stageDepthWrite;
                    depthFuncImage = stageDepthFunc;
                    alphaGenImage = stageAlphaGen;
                }
                // Die STUFENLISTE sammelt dagegen ALLE.
                //
                // In rc342 stand das Sammeln im Block darueber - also mit
                // derselben Bedingung. Damit kam nur noch EINE Stufe in die
                // Liste, und `table_blue` meldete 1 statt 3.
                //
                // Sichtbar wurde es erst daran, dass in deinem Protokoll
                // die Zeile "N zusaetzliche Stufe(n) geladen" fehlte: die
                // Liste war leer, also lud nichts, also zeichnete nichts.
                // Der Tiefentest, den ich verdaechtigt hatte, kam gar nicht
                // erst an die Reihe.
                if (inImageStage) {
                    ShaderInfo::Stage st;
                    st.image = stageImage;
                    st.lightmap = stageLightmap;
                    st.blend = stageBlend;
                    st.alphaTest = stageAlpha;
                    st.alphaConst = stageAlphaConst;
                    for (int k = 0; k < stageNumTexMods; ++k) {
                        st.texMods[k] = stageTexMods[k];
                    }
                    st.numTexMods = stageNumTexMods;
                    st.animImages = stageAnim;
                    st.animFreq = stageAnimFreq;
                    st.animOneShot = stageAnimOneShot;
                    st.rgbWave = stageRgb;
                    st.glow = stageGlow;
                    st.texGen = stageTexGen;
                    st.alphaGen = stageAlphaGen;
                    st.srcFactor = stageSrc;
                    st.dstFactor = stageDst;
                    for (int k = 0; k < 3; ++k) {
                        st.rgbConst[k] = stageRgbConst[k];
                    }
                    st.rgbVertex = stageRgbVertex;
                    st.clamp = stageClamp;
                    st.depthWrite = stageDepthWrite;
                    st.depthFunc = stageDepthFunc;
                    stages.push_back(std::move(st));
                }
                if (stageGlow) {
                    // Auch eine Stufe ohne eigenes Bild zaehlt fuer
                    // hasGlow - die Engine fragt beim Ueberspringen der
                    // FLAECHE nur, ob irgendeine Stufe glueht.
                    hasGlow = true;
                }
                stageImage.clear();
                stageLightmap = false;
                stageHatFound = false;
                inImageStage = false;
                stageNumTexMods = 0;
                stageAnim.clear();
                stageAnimFreq = 0.0F;
                stageAnimOneShot = false;
                stageBlend = BlendMode::Opaque;
                stageAlpha = AlphaTest::None;
                stageAlphaConst = -1.0F;
                stageGlow = false;
                stageTexGen = TexGen::Base;
                stageAlphaGen = AlphaGen::FromImage;
                stageRgbVertex = false;
                stageSrc = BlendFactor::One;
                stageDst = BlendFactor::Zero;
                for (int k = 0; k < 3; ++k) {
                    stageRgbConst[k] = -1.0F;
                }
                stageClamp = false;
                stageDepthWrite = false;
                stageDepthFunc = DepthFunc::LEqual;
            }
            if (depth == 0) {
                // --- Ein Shader OHNE Bildstufe ist trotzdem einer -------
                //
                // Hier stand `!found.empty()`: ohne ein Bild wurde der
                // Shader gar nicht abgelegt.
                //
                // Fuer einen Himmelsshader ist das falsch. Er hat keine
                // Stufe:
                //
                //     textures/skies/pmus
                //     {
                //         surfaceparm sky
                //         q3map_nolightmap
                //         skyParms textures/skies/pmus 2048 -
                //     }
                //
                // Genau dieser stand in md_am_sith. Weil er nie abgelegt
                // wurde, sah der Texturensucher nur den Namen, fand keine
                // Datei und meldete "keine Textur fuer
                // textures/skies/pmus" - so stand es im Protokoll des
                // Nutzers. Der Himmel fehlte also nicht am Zeichner, er
                // fehlte am LESER.
                //
                // Ich hatte zuerst ein Leerzeichen hinter der schliessenden
                // Klammer im Verdacht (`} ` steht wirklich in der Datei).
                // Das war es nicht - trim() nimmt es laengst weg. Erst der
                // Blick auf die Bedingung selbst hat es gezeigt.
                if (!name.empty() && (!found.empty() || !skyBox.empty())) {
                    ShaderInfo info;
                    info.image = found;
                    for (int k = 0; k < numTexMods; ++k) {
                        info.texMods[k] = texMods[k];
                    }
                    info.numTexMods = numTexMods;
                    info.animImages = animImages;
                    info.animFreq = animFreq;
                    info.animOneShot = animOneShot;
                    info.skyBox = skyBox;
                    info.blend = blend;
                info.imageStages = stufen;
                info.alphaConst = alphaConst;
                info.autosprite = autosprite;
                info.stages = stages;
                    info.alphaTest = alphaTest;
                    info.cull = cull;
                    info.rgbWave = rgbWave;
                    info.deformWave = deformWave;
                    info.deformSpread = deformSpread;
                    info.polygonOffset = polygonOffset;
                    info.glow = glow;
                    info.hasGlow = hasGlow;
                    info.texGen = texGen;
                    info.srcFactor = srcFactor;
                    info.dstFactor = dstFactor;
                    for (int k = 0; k < 3; ++k) {
                        info.rgbConst[k] = rgbConst[k];
                    }
                    info.rgbVertex = rgbVertexImage;
                    info.lightmapStufe = false;
                    for (const ShaderInfo::Stage& s : stages) {
                        if (s.lightmap) { info.lightmapStufe = true; }
                    }
                    info.clamp = clampImage;
                    info.alphaGen = alphaGenImage;
                    info.depthWrite = depthWriteImage;
                    info.depthFunc = depthFuncImage;
                    out[lower(name)] = info;
                }
                name.clear();
                found.clear();
                numTexMods = 0;
                animImages.clear();
                animFreq = 0.0F;
                animOneShot = false;
                skyBox.clear();
                // Auch die Stufenzahl - sonst zaehlt sie ueber alle Shader
                // der Datei hinweg weiter, und die Zahl im Protokoll waere
                // schlimmer als keine.
                stufen = 0;
                alphaConst = -1.0F;
                autosprite = false;
                stages.clear();
                stageImage.clear();
                stageLightmap = false;
                stageHatFound = false;
                blend = BlendMode::Opaque;
                cull = CullMode::Front;
                stageGlow = false;
                glow = false;
                hasGlow = false;
                stageTexGen = TexGen::Base;
                texGen = TexGen::Base;
                stageAlphaGen = AlphaGen::FromImage;
                stageSrc = BlendFactor::One;
                stageDst = BlendFactor::Zero;
                srcFactor = BlendFactor::One;
                dstFactor = BlendFactor::Zero;
                for (int k = 0; k < 3; ++k) {
                    stageRgbConst[k] = -1.0F;
                    rgbConst[k] = -1.0F;
                }
                stageClamp = false;
                clampImage = false;
                alphaGenImage = AlphaGen::FromImage;
                stageDepthWrite = false;
                stageDepthFunc = DepthFunc::LEqual;
                depthWriteImage = false;
                depthFuncImage = DepthFunc::LEqual;
            }
            continue;
        }

        // deformVertexes wave <div> <func> <base> <amp> <phase> <freq>
        //
        // Steht auf der obersten Ebene und gilt fuer die ganze Flaeche.
        if (depth == 1) {
            const std::string key = lower(firstWord(line));
            if (key == "deformvertexes") {
                std::string rest = secondWord(line);
                // autosprite: jedes Viereck wird durch ein
                // kamerazugewandtes Plaettchen um seinen Mittelpunkt
                // ersetzt (AutospriteDeform, tr_shade_calc.cpp:377).
                //
                // Gemeldet: eine grosse blaue Scheibe quer durchs Bild.
                // Die Holo-Flaechen sind als grosse Rechtecke gebaut und
                // sollen zur Laufzeit zu Sprites zusammenklappen.
                //
                // `autosprite2` ist NICHT dasselbe - es dreht das Viereck
                // um seine lange Achse - und wird bewusst nicht
                // mitgefangen. Der Vergleich ist deshalb auf Gleichheit,
                // nicht auf einen Praefix.
                if (lower(firstWord(rest)) == "autosprite") {
                    autosprite = true;
                    continue;
                }
                if (lower(firstWord(rest)) == "wave") {
                    rest = secondWord(rest);
                    const float div = readNum(firstWord(rest));
                    // tr_shader.cpp: deformationSpread = 1.0f / div, und
                    // bei div == 0 nimmt die Engine 100 mit einer Warnung.
                    deformSpread = (div != 0.0F) ? (1.0F / div) : 100.0F;
                    rest = secondWord(rest);
                    deformWave.func = waveFuncFromName(lower(firstWord(rest)));
                    rest = secondWord(rest);
                    deformWave.base = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    deformWave.amplitude = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    deformWave.phase = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    deformWave.frequency = readNum(firstWord(rest));
                }
                continue;
            }
        }

        // rgbGen wave <func> <base> <amp> <phase> <freq>
        //
        // Steht in einer STUFE. Genommen wird die der Stufe, aus der auch
        // das Bild kommt - dieselbe Regel wie bei tcMod und blendFunc.
        if (depth >= 2) {
            const std::string key = lower(firstWord(line));
            if (key == "rgbgen") {
                std::string rest = secondWord(line);
                if (lower(firstWord(rest)) == "const") {
                    // rgbGen const ( 0.85 0.65 0.65 )
                    //
                    // Die Klammern sind KEIN Muss - in den pk3 stehen beide
                    // Schreibweisen. Deshalb werden sie wie Trennzeichen
                    // behandelt statt erwartet.
                    std::string zahlen = trim(secondWord(rest));
                    for (char& c : zahlen) {
                        if (c == '(' || c == ')' || c == ',') {
                            c = ' ';
                        }
                    }
                    zahlen = trim(zahlen);
                    for (int k = 0; k < 3; ++k) {
                        const std::string w = firstWord(zahlen);
                        if (w.empty()) {
                            break;
                        }
                        stageRgbConst[k] = readNum(w);
                        zahlen = trim(secondWord(zahlen));
                    }
                } else if (lower(firstWord(rest)) == "vertex" ||
                           lower(firstWord(rest)) == "exactvertex" ||
                           lower(firstWord(rest)) == "oneminusvertex" ||
                           lower(firstWord(rest)) == "lightingdiffuse" ||
                           lower(firstWord(rest)) == "lightingdiffuseentity") {
                    // Die Farbe kommt aus den Ecken (bzw. dem Licht, das
                    // behaved in die Ecken legt). Nur DANN gilt die
                    // Eckenfarbe einer geskripteten Stufe - ComputeColors,
                    // tr_shade.cpp. Ohne rgbGen gilt identity.
                    stageRgbVertex = true;
                } else if (lower(firstWord(rest)) == "wave") {
                    rest = secondWord(rest);
                    stageRgb.func = waveFuncFromName(lower(firstWord(rest)));
                    rest = secondWord(rest);
                    stageRgb.base = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    stageRgb.amplitude = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    stageRgb.phase = readNum(firstWord(rest));
                    rest = secondWord(rest);
                    stageRgb.frequency = readNum(firstWord(rest));
                }
                continue;
            }
        }

        // "cull" steht auf der OBERSTEN Ebene des Shaders, nicht in einer
        // Stufe - es gilt fuer die ganze Flaeche.
        if (depth == 1) {
            const std::string key = lower(firstWord(line));
            if (key == "skyparms") {
                // skyParms <aussenbox> <wolkenhoehe> <innenbox>
                //
                // Nur der erste Wert wird gebraucht. `-` heisst: keine
                // Aussenbox. Die Innenbox unterstuetzt JKA nicht (die
                // Engine warnt selbst), und ohne Wolkenschichten ist die
                // Hoehe ohne Wirkung - siehe ShaderInfo::skyBox.
                const std::string box = firstWord(secondWord(line));
                if (!box.empty() && box != "-") {
                    skyBox = box;
                }
                continue;
            }
            if (key == "polygonoffset") {
                polygonOffset = true;
                continue;
            }
            if (key == "cull") {
                const std::string what = lower(firstWord(secondWord(line)));
                if (what == "none" || what == "twosided" || what == "disable") {
                    cull = CullMode::Two;
                } else if (what == "back" || what == "backside" ||
                           what == "backsided") {
                    cull = CullMode::Back;
                }
                // Alles andere laesst die Engine mit einer Warnung stehen -
                // also bleibt es bei der Vorgabe.
                continue;
            }
        }

        if (depth == 0) {
            // Der Name steht allein auf einer Zeile vor der Klammer.
            name = line;
            found.clear();
            numTexMods = 0;
            blend = BlendMode::Opaque;
            cull = CullMode::Front;
            rgbWave = Wave{};
            stageRgb = Wave{};
            rgbVertexImage = false;
            stageRgbVertex = false;
            deformWave = Wave{};
            deformSpread = 0.0F;
            polygonOffset = false;
            inImageStage = false;
            continue;
        }

        // tcMod scroll <s> <t> - die Bewegung DER Stufe, aus der auch das
        // Bild kommt. Kommt sie vor dem map-Befehl, gilt sie trotzdem: in
        // einer Stufe ist die Reihenfolge der Zeilen frei.
        {
            const std::string key = lower(firstWord(line));
            // Gueltig ist das tcMod DER Stufe, aus der das Bild stammt -
            // und in dieser Stufe darf es vor ODER nach dem map-Befehl
            // stehen. Meine erste Fassung nahm es nur VOR dem Bild an, und
            // damit fand sie in 678 Shadern kein einziges: die Lava schreibt
            //     map textures/plasma_mustafar/lava
            //     ...
            //     tcMod scroll 0.04 0.04
            // also genau andersherum.
            // blendFunc der Stufe. Die Kurzformen add/filter/blend stehen
            // in tr_shader.cpp gleichwertig neben den GL_-Namen.
            // alphaFunc: der Bildpunkt wird VERWORFEN, wenn er die
            // Schwelle nicht erreicht. Die Namen stehen in
            // NameToAFunc (tr_shader.cpp:285 ff.).
            //
            // Anders als blendFunc gilt das AUCH fuer eine Stufe, die sonst
            // deckend ist - genau daran ist der schwarze Kasten um den
            // Geonosianer entstanden.
            // --- glow -----------------------------------------------
            //
            // Steht allein auf einer Zeile innerhalb einer Stufe. In der
            // Engine setzt es stage->glow und darueber shader.hasGlow;
            // gezeichnet wird es nicht hier, sondern in einem zweiten
            // Durchgang - siehe ShaderInfo::Stage::glow.
            // tcGen / texgen. Nur `environment` aendert etwas; die
            // uebrigen drei Formen (lightmap, texture/base, vector) liefern
            // entweder genau das, was ohnehin in der .bsp steht, oder sind
            // so selten, dass ein Raten schlechter waere als das Bekannte.
            if ((key == "tcgen" || key == "texgen") && depth >= 2) {
                if (lower(firstWord(secondWord(line))) == "environment") {
                    stageTexGen = TexGen::Environment;
                }
                continue;
            }
            // --- depthWrite und depthFunc ---------------------------
            //
            // Die Engine LOESCHT die Tiefenmaske fuer jede Stufe mit
            // blendFunc (tr_shader.cpp:1444) - ausser `depthwrite` stand
            // ausdruecklich da. Und dann hat es eine zweite Wirkung: der
            // Shader wird als SS_SEE_THROUGH einsortiert
            // (tr_shader.cpp:3165), also gleich hinter das Deckende. Der
            // Quelltext nennt den Fall beim Namen - "a grill or grate".
            if (key == "depthwrite" && depth >= 2) {
                stageDepthWrite = true;
                continue;
            }
            if (key == "depthfunc" && depth >= 2) {
                const std::string w = lower(firstWord(secondWord(line)));
                if (w == "equal") {
                    stageDepthFunc = DepthFunc::Equal;
                } else if (w == "disable") {
                    stageDepthFunc = DepthFunc::Disable;
                } else {
                    stageDepthFunc = DepthFunc::LEqual;
                }
                continue;
            }
            if (key == "glow" && depth >= 2) {
                stageGlow = true;
                continue;
            }
            if (key == "alphafunc" && depth >= 2) {
                const std::string w = lower(secondWord(line));
                AlphaTest at = AlphaTest::None;
                if (w == "gt0") { at = AlphaTest::Gt0; }
                else if (w == "lt128") { at = AlphaTest::Lt128; }
                else if (w == "ge128") { at = AlphaTest::Ge128; }
                else if (w == "ge192") { at = AlphaTest::Ge192; }
                if (at != AlphaTest::None) {
                    stageAlpha = at;
                }
                continue;
            }

            // alphaGen const <wert>: ein FESTER Alphawert fuer die ganze
            // Stufe, unabhaengig vom Bild (RB_CalcAlphaFromConst).
            //
            // Andere Formen (alphaGen wave, lightingSpecular, portal) sind
            // NICHT gebaut - sie kommen in den hier vorliegenden Shadern
            // nicht vor, und eine falsch geratene waere schlimmer als
            // keine.
            if (key == "alphagen" && depth >= 2) {
                const std::string rest = lower(secondWord(line));
                if (firstWord(rest) == "const") {
                    const std::string v = trim(secondWord(rest));
                    if (!v.empty()) {
                        stageAlphaConst = std::strtof(v.c_str(), nullptr);
                        stageAlphaGen = AlphaGen::Const;
                    }
                } else if (lower(firstWord(rest)) == "lightingspecular") {
                    stageAlphaGen = AlphaGen::LightingSpecular;
                } else if (lower(firstWord(rest)) == "vertex") {
                    stageAlphaGen = AlphaGen::Vertex;
                } else if (lower(firstWord(rest)) == "identity") {
                    stageAlphaGen = AlphaGen::Identity;
                } else if (!firstWord(rest).empty()) {
                    // wave, entity, portal, dot, vertex - nicht nachgebaut.
                    // Siehe AlphaGen: gemerkt wird es trotzdem, damit die
                    // Stufe weggelassen statt falsch gezeichnet werden kann.
                    stageAlphaGen = AlphaGen::Unsupported;
                }
                continue;
            }
            if (key == "blendfunc" && depth >= 2) {
                const std::string rest = lower(secondWord(line));
                const std::string a = firstWord(rest);
                const std::string bb = lower(secondWord(rest));
                // Die Vorgabe ist DURCHSCHEINEND, nicht deckend.
                //
                // Wer ein blendFunc schreibt, will mischen - welche Art
                // genau, sagen die Faelle darunter. Was keiner davon
                // trifft, ist jedenfalls keine deckende Flaeche, und sie
                // deckend zu zeichnen ist der sichtbarste Fehler.
                //
                // So herum geschrieben, weil clang-tidy die alte Fassung zu
                // Recht als bugprone-branch-clone gemeldet hat: der Zweig
                // fuer "blend" und der Zweig fuer alles Uebrige taten
                // dasselbe. Als Vorgabe steht es nur noch einmal da, und
                // "blend" braucht gar keinen eigenen Zweig mehr.
                // Die Faktoren einzeln merken - damit laesst sich der
                // lange Schwanz seltener Kombinationen rechnen, statt ihn
                // in eine der fuenf Schubladen zu zwingen. Die Kurzformen
                // stehen so in tr_shader.cpp:1391 ff.
                if (a == "add") {
                    stageSrc = BlendFactor::One;
                    stageDst = BlendFactor::One;
                } else if (a == "filter") {
                    stageSrc = BlendFactor::DstColor;
                    stageDst = BlendFactor::Zero;
                } else if (a == "blend") {
                    stageSrc = BlendFactor::SrcAlpha;
                    stageDst = BlendFactor::OneMinusSrcAlpha;
                } else {
                    // Unbekannte Namen: lieber deckend als geraten. Die
                    // Engine meldet an dieser Stelle einen Fehler und nimmt
                    // GL_ONE (tr_shader.cpp:354).
                    stageSrc = faktor(a, BlendFactor::One);
                    stageDst = faktor(bb, BlendFactor::Zero);
                }
                stageBlend = BlendMode::Alpha;
                if (a == "add" || (a == "gl_one" && bb == "gl_one")) {
                    stageBlend = BlendMode::Add;
                } else if (a == "gl_src_alpha" && bb == "gl_one") {
                    // Additiv, aber mit dem Alphakanal gewichtet. Fiel
                    // bisher auf `Alpha` durch und wurde damit daempfend
                    // gezeichnet statt leuchtend - siehe BlendMode.
                    stageBlend = BlendMode::AddAlpha;
                } else if (a == "filter" ||
                           (a == "gl_dst_color" &&
                            (bb.empty() || bb == "gl_zero")) ||
                           (a == "gl_zero" && bb == "gl_src_color")) {
                    stageBlend = BlendMode::Filter;
                } else if (a == "gl_one" && (bb.empty() || bb == "gl_zero")) {
                    // "implicitly assume that a GL_ONE GL_ZERO blend mask
                    // disables blending" - tr_shader.cpp, Zeile 1761.
                    stageBlend = BlendMode::Opaque;
                }
            }

            if (key == "tcmod" && depth >= 2) {
                // --- Die ganze Kette, in der Reihenfolge der Datei -------
                //
                // Bis rc353 wurde nur `scroll` gelesen und in zwei
                // Einzelfelder gelegt. Damit ging die REIHENFOLGE verloren,
                // und die entscheidet: `scroll` dann `scale` staucht den
                // schon verschobenen Wert, `scale` dann `scroll` verschiebt
                // den gestauchten. Beides kommt vor (80- gegen 57-mal).
                //
                // Zu den Zahlen: parseFloat, nicht std::from_chars von Hand.
                //
                // Mein erster Versuch rief from_chars direkt auf - und fand
                // danach KEIN einziges tcMod mehr. Der Grund steht in
                // bhed/num.h: from_chars ueberspringt fuehrende Leerzeichen
                // ausdruecklich NICHT, anders als das std::stof, das vorher
                // hier stand. Die .shader-Dateien sind aber mit Tabulatoren
                // eingerueckt. Die Probe hat es sofort gemeldet.
                //
                // Und keine Ausnahme fuer einen erwarteten Fall: dass hinter
                // `tcMod` Unsinn steht, ist bei fremden Dateien der
                // Normalfall.
                if (stageNumTexMods >= kMaxTexMods) {
                    continue;   // TR_MAX_TEXMODS - mehr nimmt die Engine auch nicht
                }
                const std::string rest = trim(secondWord(line));
                const std::string art = lower(firstWord(rest));
                std::string zahlen = trim(secondWord(rest));
                auto zahl = [&zahlen](float& out) {
                    if (zahlen.empty()) {
                        return false;
                    }
                    const bool ok = parseFloat(zahlen, out);
                    zahlen = trim(secondWord(zahlen));
                    return ok;
                };
                TexMod m;
                bool gut = false;
                if (art == "scroll") {
                    m.kind = TexModKind::Scroll;
                    gut = zahl(m.a[0]) && zahl(m.a[1]);
                } else if (art == "scale") {
                    m.kind = TexModKind::Scale;
                    gut = zahl(m.a[0]) && zahl(m.a[1]);
                } else if (art == "rotate") {
                    m.kind = TexModKind::Rotate;
                    gut = zahl(m.a[0]);
                } else if (art == "transform") {
                    m.kind = TexModKind::Transform;
                    gut = zahl(m.a[0]) && zahl(m.a[1]) && zahl(m.a[2]) &&
                          zahl(m.a[3]) && zahl(m.a[4]) && zahl(m.a[5]);
                } else if (art == "stretch" || art == "turb") {
                    m.kind = (art == "stretch") ? TexModKind::Stretch
                                                : TexModKind::Turb;
                    // stretch traegt eine Wellenform, turb nicht:
                    //
                    //     tcMod stretch <func> <base> <amp> <phase> <freq>
                    //     tcMod turb            <base> <amp> <phase> <freq>
                    //
                    // ParseTexMod (tr_shader.cpp:498) liest bei `turb` KEIN
                    // Funktionswort. Steht dort trotzdem eines, wird es als
                    // Zahl gelesen und ergibt null - das ist das Verhalten
                    // der Engine und wird hier nicht "verbessert".
                    if (m.kind == TexModKind::Stretch) {
                        m.wave.func = waveFuncFromName(lower(firstWord(zahlen)));
                        zahlen = trim(secondWord(zahlen));
                    } else {
                        m.wave.func = WaveFunc::Sin;
                    }
                    gut = zahl(m.wave.base) && zahl(m.wave.amplitude) &&
                          zahl(m.wave.phase) && zahl(m.wave.frequency);
                }
                if (gut) {
                    stageTexMods[stageNumTexMods] = m;
                    ++stageNumTexMods;
                }
            }
        }

        // --- ALLE Stufen ansehen, nicht nur bis zur ersten ---------------
        //
        // Hier stand `if (found.empty())`: sobald ein Bild gefunden war,
        // sah der Leser keine weitere Stufe mehr an.
        //
        // Das erklaert alle Messungen der letzten Runden: ein Shader mit
        // zwei Stufen meldete eine, `table_blue` mit drei meldete zwei -
        // immer die ERSTE, danach Schluss.
        //
        // Und es erklaert das helle Viereck am Holotisch: von
        //
        //     { map $lightmap }
        //     { map table_blue   blendFunc GL_DST_COLOR GL_ZERO }
        //     { map table_blue   blendFunc GL_ONE GL_ONE   glow }
        //
        // kam nie mehr als die erste Stufe an.
        //
        // `found` behaelt weiterhin das ERSTE echte Bild - alles, was heute
        // damit arbeitet, arbeitet unveraendert weiter. Nur schaut der
        // Leser jetzt zu Ende.
        {
            const std::string key = lower(firstWord(line));
            if (key == "map" || key == "clampmap" || key == "diffusemap") {
                const std::string value = secondWord(line);
                // $lightmap und $whiteimage sind keine Dateien.
                // Eine `$lightmap`-Stufe bringt kein Bild mit, belegt aber
                // einen Platz in der Reihenfolge - und die entscheidet
                // beim Uebereinanderlegen.
                if (!value.empty() && value[0] == '$') {
                    stageLightmap = (lower(value) == "$lightmap");
                    inImageStage = true;
                }
                if (!value.empty() && value[0] != '$') {
                    // clampmap: GL_CLAMP statt GL_REPEAT - siehe tapLevel.
                    stageClamp = (key == "clampmap");
                    if (found.empty()) {
                        found = value;
                        stageHatFound = true;
                    }
                    stageImage = value;
                    inImageStage = true;
                    // Gezaehlt wird HIER, am Bildbefehl selbst.
                    //
                    // Erst stand das Zaehlen am Stufenende - und lieferte
                    // fuer einen Shader mit zwei Stufen eine Eins.
                    // Gepruefte Zahl statt vermuteter: bei `map` und
                    // `animmap` gibt es genau eine Bildstufe, das laesst
                    // sich nicht verzaehlen.
                    ++stufen;
                }
            } else if (key == "animmap" || key == "clampanimmap" ||
                       key == "oneshotanimmap") {
                // animMap <freq> <bild1> <bild2> ... - das erste Bild nehmen.
                //
                // `clampanimMap` und `oneshotanimMap` sind dieselbe Stufe
                // (OpenJK tr_shader.cpp, ParseStage: ein gemeinsamer
                // Zweig). clampanimMap laedt die Bilder mit GL_CLAMP,
                // oneshotanimMap laesst die Folge einmal laufen und auf dem
                // letzten Bild stehen.
                //
                // Bis rc568 kannte dieser Leser nur `animMap`: die Stufe
                // fiel ganz weg. Der Abgleich ueber alle Shader von JKA und
                // Movie Duels (tests/shaderabgleich.cpp) fand so 72
                // Gluehstufen, die bei uns nicht leuchteten - etwa das
                // Tor in textures/byss/byss_gate_onoff.
                //
                // --- Hier standen drei Fehler, alle vom selben Ursprung ---
                //
                // Dieser Zweig war NICHT wie der von `map` gebaut, und
                // deshalb fehlten ihm drei Dinge:
                //
                // 1. `found` wurde ohne `if (found.empty())` ueberschrieben.
                //    Eine spaetere animMap-Stufe hat damit das Bild des
                //    Shaders geklaut. Bei
                //    textures/asjc_coruscant/rail_1 - Lightmap, dann das
                //    Gelaender, dann eine animMap mit sieben Leuchtbildern -
                //    wurde `image` zu `rail_1_glw1`. Das Gelaender selbst
                //    war weg, uebrig blieb sein Leuchten.
                //
                // 2. `stageImage` wurde nicht gesetzt. Damit hatte die Stufe
                //    kein Bild, kam nie in `stages` an und konnte auch
                //    keine Zusatzstufe werden - die Bildfolge wurde also
                //    nicht nur nicht bewegt, sie fehlte ganz.
                //
                // 3. `stageHatFound` wurde nicht gesetzt. Mischart, Alpha
                //    und `glow` dieser Stufe kamen deshalb nie bei den
                //    Einzelfeldern an, auch wenn sie die erste war.
                //
                // Und seit rc355 werden ALLE Bilder behalten, nicht nur das
                // erste - sonst steht die Folge still.
                //
                // Gemessen: 186 Shader der neun pk3 benutzen animMap.
                std::string rest = trim(secondWord(line));
                const std::string freq = firstWord(rest);
                rest = trim(secondWord(rest));
                std::vector<std::string> bilder;
                while (!rest.empty()) {
                    const std::string b = firstWord(rest);
                    if (b.empty()) {
                        break;
                    }
                    bilder.push_back(b);
                    const std::string weiter = trim(secondWord(rest));
                    if (weiter == rest) {
                        break;   // kein Fortschritt - lieber abbrechen
                    }
                    rest = weiter;
                }
                if (!bilder.empty()) {
                    if (found.empty()) {
                        found = bilder[0];
                        stageHatFound = true;
                    }
                    stageImage = bilder[0];
                    stageAnim = bilder;
                    stageAnimFreq = readNum(freq);
                    stageAnimOneShot = (key == "oneshotanimmap");
                    stageClamp = (key == "clampanimmap");
                    ++stufen;
                    inImageStage = true;
                }
            }
        }
    }
}

std::vector<std::string> textureCandidates(const std::string& name) {
    std::vector<std::string> out;
    // Traegt der Name schon eine Endung, zuerst so versuchen.
    const std::size_t dot = name.find_last_of('.');
    const std::size_t slash = name.find_last_of('/');
    const bool hasExt = dot != std::string::npos &&
                        (slash == std::string::npos || dot > slash);
    if (hasExt) {
        out.push_back(name);
    }
    const std::string base = hasExt ? name.substr(0, dot) : name;
    // Reihenfolge wie in JKA: erst jpg, dann png, dann tga.
    for (const char* ext : {".jpg", ".png", ".tga", ".jpeg"}) {
        out.push_back(base + ext);
    }
    return out;
}

}  // namespace bhed
