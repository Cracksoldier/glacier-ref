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

    bool isDirty() const { return !m_stack.isClean(); }
    void markClean() { m_stack.setClean(); }

signals:
    void dirtyChanged(bool dirty);

private:
    QPointer<BoardDocument> m_document;
    QUndoStack m_stack;
};

} // namespace glacier::editing
