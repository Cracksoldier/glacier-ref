#pragma once

#include "glacier/editing/Operations.h"

#include <QObject>
#include <QPointer>
#include <QUndoStack>

namespace glacier {
class BoardDocument;
}

namespace glacier::editing {

// Per-document undo history (spec §6, M2.02). Every edit is a DocumentChange
// applied to the document; the inverse returned by BoardDocument::apply() is
// stored with it, so undo/redo replay exact changes (A02).
class CommandController : public QObject
{
    Q_OBJECT

public:
    static constexpr int DefaultUndoLimit = 200;

    explicit CommandController(BoardDocument *document, QObject *parent = nullptr);

    QUndoStack *undoStack() { return &m_stack; }
    BoardDocument *document() const { return m_document; }

    // Applies the change and records it as one undoable step named `text`.
    // Returns false (recording nothing) when the result has no change or the
    // document rejects it; `error` then explains why.
    bool run(const OpResult &result, const QString &text, QString *error = nullptr);
    bool run(const DocumentChange &change, const QString &text, QString *error = nullptr);

    // Dirty state (spec §5). A marker identifies a state of the history: the
    // command at the top of the stack. An asynchronous save records the
    // marker when it takes its snapshot and passes it to markClean() when it
    // completes, so edits made during the save stay dirty (A05).
    using Marker = quint64;
    static constexpr Marker Unreachable = ~Marker(0);

    Marker currentMarker() const;
    bool isDirty() const { return currentMarker() != m_cleanMarker; }
    void markClean() { markClean(currentMarker()); }
    void markClean(Marker marker);
    // Dirty until the next save, whatever is undone (e.g. a recovered board).
    void markNeverClean() { markClean(Unreachable); }
    // Forgets the history; the current state becomes the clean one (e.g. a
    // board was loaded into this window).
    void resetHistory();

signals:
    void dirtyChanged(bool dirty);

private:
    void updateDirty();

    QPointer<BoardDocument> m_document;
    QUndoStack m_stack;
    Marker m_baseMarker = 0;  // the state at index 0 (after dropped commands)
    Marker m_cleanMarker = 0;
    bool m_dirty = false;
};

} // namespace glacier::editing
