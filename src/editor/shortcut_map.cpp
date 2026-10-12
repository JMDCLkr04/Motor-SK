#include "shortcut_map.h"

namespace sk::editor {

const std::vector<Shortcut>& ShortcutMap::all() {
    static const std::vector<Shortcut> table = {
        {"tool1", "Herramienta: Seleccionar", '1', false, false, false},
        {"tool2", "Herramienta: Mover", '2', false, false, false},
        {"tool3", "Herramienta: Escalar", '3', false, false, false},
        {"tool4", "Herramienta: Rotar", '4', false, false, false},
        {"undo", "Deshacer", 'Z', true, false, false},
        {"redo", "Rehacer", 'Y', true, false, false},
        {"rename", "Renombrar parte", kKeyF2, false, false, false},
        {"focus", "Foco en la seleccion", 'F', false, false, false},
        {"copy", "Copiar", 'C', true, false, false},
        {"cut", "Cortar", 'X', true, false, false},
        {"paste", "Pegar", 'V', true, false, false},
        {"duplicate", "Clonar", 'D', true, false, false},
        {"rotateY", "Rotar 90 en Y", 'R', true, false, false},
        {"rotateX", "Rotar 90 en X", 'T', true, false, false},
        {"delete", "Eliminar", kKeyDelete, false, false, false},
        {"group", "Agrupar", 'G', true, false, false},
        {"ungroup", "Desagrupar", 'U', true, false, false},
        {"lock", "Bloquear (Lock)", 'L', true, false, false},
        {"anchor", "Anclar (Anchor)", 'A', false, false, true},
        {"classes", "Referencia de clases", kKeyF1, false, false, false},
        {"escape", "Volver al panel", kKeyEscape, false, false, false},
    };
    return table;
}

const Shortcut* ShortcutMap::find(const char* action) {
    for (const Shortcut& s : all()) {
        if (std::string(s.action) == action) return &s;
    }
    return nullptr;
}

bool ShortcutMap::matches(const char* action, int vk, bool ctrl, bool shift,
                          bool alt) {
    const Shortcut* s = find(action);
    if (!s || s->vk != vk) return false;
    if (s->ctrl && !ctrl) return false;
    if (s->shift && !shift) return false;
    if (s->alt && !alt) return false;
    return true;
}

std::vector<std::pair<std::string, std::string>> ShortcutMap::collisions() {
    std::vector<std::pair<std::string, std::string>> out;
    const std::vector<Shortcut>& table = all();
    for (size_t i = 0; i < table.size(); ++i) {
        for (size_t j = i + 1; j < table.size(); ++j) {
            const Shortcut& a = table[i];
            const Shortcut& b = table[j];
            if (a.vk == b.vk && a.ctrl == b.ctrl && a.shift == b.shift &&
                a.alt == b.alt) {
                out.emplace_back(a.action, b.action);
            }
        }
    }
    return out;
}

std::string ShortcutMap::keyText(const Shortcut& shortcut) {
    std::string text;
    if (shortcut.ctrl) text += "Ctrl+";
    if (shortcut.shift) text += "Shift+";
    if (shortcut.alt) text += "Alt+";
    if (shortcut.vk == kKeyDelete) {
        text += "Supr";
    } else if (shortcut.vk == kKeyEscape) {
        text += "Esc";
    } else if (shortcut.vk == kKeyF1) {
        text += "F1";
    } else if (shortcut.vk == kKeyF2) {
        text += "F2";
    } else {
        text += static_cast<char>(shortcut.vk);
    }
    return text;
}

std::string ShortcutMap::describe() {
    std::string text = "Atajos actuales:\n\n";
    for (const Shortcut& s : all()) {
        text += s.label;
        const size_t len = std::string(s.label).size();
        for (size_t i = len; i < 24; ++i) text += '.';
        text += ' ';
        text += keyText(s);
        text += '\n';
    }
    return text;
}

} // namespace sk::editor
