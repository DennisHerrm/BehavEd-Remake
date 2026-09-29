// Ersatz fuer die zwei Zugaenge aus gui/backend_win32.cpp.
//
// Der Bindeprueflauf (tools/check_gui.sh) braucht sie, um gpumap_win32.cpp
// zu binden - die echte Datei laesst sich hier nicht uebersetzen, sie
// braucht Windows und ImGui. Fuer die Frage, die der Prueflauf beantwortet
// (passen Erklaerung und Definition zusammen?), reicht ein Rumpf.
namespace bhed::render {
void* d3dDevice() { return nullptr; }
void* d3dContext() { return nullptr; }
}  // namespace bhed::render
