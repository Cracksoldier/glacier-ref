#include "glacier/editing/CommandController.h"

#include "glacier/core/BoardDocument.h"

#include <QDebug>
#include <QUndoCommand>

namespace glacier::editing {

namespace {

class DocumentCommand : public QUndoCommand
{
public:
    DocumentCommand(BoardDocument *document, DocumentChange forward, DocumentChange inverse, const QString &text)
        : QUndoCommand(text)
        , m_document(document)
        , m_forward(std::move(forward))
        , m_inverse(std::move(inverse))
    {
    }

    void redo() override
    {
        if (m_alreadyApplied) { // applied by CommandController::run before push()
            m_alreadyApplied = false;
            return;
        }
        replay(m_forward);
    }

    void undo() override { replay(m_inverse); }

private:
    void replay(const DocumentChange &change)
    {
        if (!m_document)
            return;
        QString error;
        if (!m_document->apply(change, &error)) {
            // Cannot happen for exact inverses; never leave a broken command.
            qWarning("glacier: undo history entry could not be replayed: %s", qPrintable(error));
            setObsolete(true);
        }
    }

    QPointer<BoardDocument> m_document;
    DocumentChange m_forward;
    DocumentChange m_inverse;
    bool m_alreadyApplied = true;
};

} // namespace

CommandController::CommandController(BoardDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    m_stack.setUndoLimit(DefaultUndoLimit);
    connect(&m_stack, &QUndoStack::cleanChanged, this, [this](bool clean) { emit dirtyChanged(!clean); });
}

bool CommandController::run(const OpResult &result, const QString &text, QString *error)
{
    if (!result.change) {
        if (error)
            *error = result.error;
        return false;
    }
    return run(*result.change, text, error);
}

bool CommandController::run(const DocumentChange &change, const QString &text, QString *error)
{
    if (!m_document || change.isEmpty())
        return false;
    QString problem;
    std::optional<DocumentChange> inverse = m_document->apply(change, &problem);
    if (!inverse) {
        if (error)
            *error = problem;
        return false;
    }
    m_stack.push(new DocumentCommand(m_document, change, std::move(*inverse), text)); // stack owns it
    return true;
}

} // namespace glacier::editing
