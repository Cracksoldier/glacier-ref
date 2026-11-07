#include "glacier/editing/CommandController.h"

#include "glacier/core/BoardDocument.h"

#include <QDebug>
#include <QUndoCommand>

#include <atomic>

namespace glacier::editing {

namespace {

CommandController::Marker nextMarker()
{
    static std::atomic<CommandController::Marker> counter{0};
    return ++counter;
}

class DocumentCommand : public QUndoCommand
{
public:
    DocumentCommand(BoardDocument *document, DocumentChange forward, DocumentChange inverse, const QString &text)
        : QUndoCommand(text)
        , m_document(document)
        , m_forward(std::move(forward))
        , m_inverse(std::move(inverse))
        , m_marker(nextMarker())
    {
    }

    CommandController::Marker marker() const { return m_marker; }

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
    CommandController::Marker m_marker; // unique for the process
    bool m_alreadyApplied = true;
};

} // namespace

CommandController::CommandController(BoardDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    m_stack.setUndoLimit(DefaultUndoLimit);
    m_baseMarker = nextMarker();
    m_cleanMarker = m_baseMarker;
    connect(&m_stack, &QUndoStack::indexChanged, this, &CommandController::updateDirty);
}

CommandController::Marker CommandController::currentMarker() const
{
    const int index = m_stack.index();
    if (index == 0)
        return m_baseMarker;
    return static_cast<const DocumentCommand *>(m_stack.command(index - 1))->marker();
}

void CommandController::markClean(Marker marker)
{
    m_cleanMarker = marker;
    updateDirty();
}

void CommandController::resetHistory()
{
    m_stack.clear();
    m_baseMarker = nextMarker();
    m_cleanMarker = m_baseMarker;
    updateDirty();
}

void CommandController::updateDirty()
{
    const bool dirty = isDirty();
    if (dirty != m_dirty) {
        m_dirty = dirty;
        emit dirtyChanged(dirty);
    }
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
    // At the undo limit, pushing drops the oldest command; the state at
    // index 0 then is the one after it.
    const int limit = m_stack.undoLimit();
    const bool drops = limit > 0 && m_stack.index() == m_stack.count() && m_stack.count() == limit;
    const Marker dropped = drops ? static_cast<const DocumentCommand *>(m_stack.command(0))->marker() : 0;
    m_stack.push(new DocumentCommand(m_document, change, std::move(*inverse), text)); // stack owns it
    if (drops) {
        m_baseMarker = dropped;
        updateDirty();
    }
    return true;
}

} // namespace glacier::editing
