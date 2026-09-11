#include "tags/TagUndoStack.h"

TagUndoStack::Step* TagUndoStack::byId(quint64 id) {
    for (Step& s : undo)
        if (s.id == id) return &s;
    return nullptr;
}

void TagUndoStack::prune() {
    while (undo.size() > kMaxSteps || (bytes > kMaxBytes && undo.size() > 1)) {
        bytes -= undo.first().bytes;
        undo.removeFirst();
    }
    if (bytes < 0) bytes = 0;
}

void TagUndoStack::clear() {
    const bool hatteEtwas = !undo.isEmpty() || !redo.isEmpty();
    undo.clear();
    redo.clear();
    bytes = 0;
    if (hatteEtwas) emit changed();
}
