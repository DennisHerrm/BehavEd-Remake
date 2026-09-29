// Die Zerlegung aus `helferAusWert` (gui/app.cpp), eigenstaendig geprueft.
//
// Warum hier und nicht dort: `gui/app.cpp` braucht ImGui und laesst sich
// auf der Linux-Seite gar nicht uebersetzen. Die Zerlegung ist aber reine
// Textarbeit - also steht sie hier noch einmal, Zeile fuer Zeile gleich,
// und wird gegen genau die drei Formen geprueft, die die Knoepfe Get, Tag
// und Rnd schreiben.
//
// shank zu rc552: "die werte die ich bei den einzelnen parameters eintrage
// werden nicht mehr gespeichert." Der Helfer baut aus seinen vier Zeilen
// einen Ausdruck und schreibt ihn ins Feld; gespeichert wird das FELD, die
// vier Zeilen nicht. Beim naechsten Oeffnen standen sie wieder auf den
// Vorgaben, obwohl im Feld `get( FLOAT, "SET_PARM1" )` stand.
//
// Aendert sich die Zerlegung in app.cpp, muss sie hier mitgeaendert werden -
// das ist der Preis dafuer, dass die Oberflaeche hier nicht baubar ist.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
static std::string ohneRand(const std::string& t){
    std::size_t a=0,b=t.size();
    while(a<b&&(t[a]==' '||t[a]=='\t'))++a;
    while(b>a&&(t[b-1]==' '||t[b-1]=='\t'))--b;
    return t.substr(a,b-a);
}
struct H { std::string typ="FLOAT",name="SET_PARM1",tagName="",tagTyp="ORIGIN";
           float von=0.0F,bis=1.0F; };
static H lies(const std::string& v){
    H h;
    if(v.rfind("get(",0)==0){
        const auto k=v.find(','); const auto a1=v.find('"');
        const auto a2=(a1==std::string::npos)?std::string::npos:v.find('"',a1+1);
        if(k!=std::string::npos&&a1!=std::string::npos&&a2!=std::string::npos&&k>4){
            h.typ=ohneRand(v.substr(4,k-4)); h.name=v.substr(a1+1,a2-a1-1); }
        return h;
    }
    if(v.rfind("tag(",0)==0){
        const auto a1=v.find('"');
        const auto a2=(a1==std::string::npos)?std::string::npos:v.find('"',a1+1);
        const auto k=v.find(',',(a2==std::string::npos)?0:a2);
        const auto zu=v.rfind(')');
        if(a1!=std::string::npos&&a2!=std::string::npos&&k!=std::string::npos&&
           zu!=std::string::npos&&zu>k){
            h.tagName=v.substr(a1+1,a2-a1-1); h.tagTyp=ohneRand(v.substr(k+1,zu-k-1)); }
        return h;
    }
    if(v.rfind("random(",0)==0){
        float a=0,b=0;
        if(std::sscanf(v.c_str(),"random( %f , %f )",&a,&b)==2){h.von=a;h.bis=b;}
    }
    return h;
}
static int fehl=0;
static void pruef(const char* was,bool ok){
    printf("  %s  %s\n", ok?"ok  ":"FEHL", was); if(!ok)++fehl; }
int main(){
    // GENAU die Formen, die die drei Knoepfe schreiben
    { H h=lies("get( FLOAT, \"SET_PARM1\" )");
      pruef("get: Typ FLOAT", h.typ=="FLOAT");
      pruef("get: Name SET_PARM1", h.name=="SET_PARM1"); }
    { H h=lies("get( STRING, \"SET_LEADER\" )");
      pruef("get: Typ STRING", h.typ=="STRING");
      pruef("get: Name SET_LEADER", h.name=="SET_LEADER"); }
    { H h=lies("tag( \"marke1\", ORIGIN )");
      pruef("tag: Name marke1", h.tagName=="marke1");
      pruef("tag: Art ORIGIN", h.tagTyp=="ORIGIN"); }
    { H h=lies("tag( \"\", ANGLES )");
      pruef("tag: leerer Name", h.tagName=="");
      pruef("tag: Art ANGLES", h.tagTyp=="ANGLES"); }
    { H h=lies("random( 0.000, 2.500 )");
      pruef("random: von 0", h.von==0.0F);
      pruef("random: bis 2.5", h.bis==2.5F); }
    // Was NICHT passt, darf nichts verstellen
    { H h=lies("exp1");
      pruef("Handgetipptes laesst die Vorgaben stehen",
            h.typ=="FLOAT"&&h.name=="SET_PARM1"&&h.tagTyp=="ORIGIN"&&h.bis==1.0F); }
    { H h=lies("get(");
      pruef("abgeschnittenes get() stuerzt nicht und aendert nichts",
            h.typ=="FLOAT"&&h.name=="SET_PARM1"); }
    { H h=lies("tag(");
      pruef("abgeschnittenes tag() ebenso", h.tagTyp=="ORIGIN"); }
    { H h=lies("random(");
      pruef("abgeschnittenes random() ebenso", h.von==0.0F&&h.bis==1.0F); }
    { H h=lies("");
      pruef("leerer Wert", h.typ=="FLOAT"); }
    printf(fehl? "  %d Fehlschlag(e)\n":"  alle Proben bestanden\n", fehl);
    return fehl?1:0;
}
