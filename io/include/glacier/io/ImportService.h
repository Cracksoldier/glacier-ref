#pragma once

#include "glacier/core/Id.h"
#include "glacier/core/Limits.h"
#include "glacier/io/ImageDecoder.h"
#include "glacier/io/MimeRouter.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointF>

#include <atomic>
#include <memory>

class QThreadPool;

namespace glacier {
class SessionAssetStore;
}

namespace glacier::io {

struct ImportedImage
{
    QString displayName;
    QByteArray sha256;
    QString mediaType;
    QSize orientedSize;
    qint64 byteLength = 0;
    ImageInfo info;
    QImage thumbnail; // longest side <= ThumbnailSide, for seeding the cache
    QStringList notices;
    QString sourcePath; // absolute path for file sources; empty for pixels
};

struct ImportFailure
{
    QString displayName;
    QString reason;
};

struct ImportBatchResult
{
    quint64 batchId = 0;
    Id documentToken;     // the document the batch was started for
    QPointF anchor;       // board position to place the batch around
    bool cancelled = false;
    QList<ImportedImage> images; // in source order
    QList<ImportFailure> failures;
    QStringList notices;  // plan-level notices (skipped folders, links, ...)
    bool linked = false;  // the plan was an Import as Link
};

// One per window. Runs bounded import jobs on a shared thread pool: read
// (bounded) -> SHA-256 -> session store -> probe -> thumbnail. Jobs capture
// only value data and shared ownership of the store, and report through a
// QFutureWatcher owned by this object, so destroying the service (window
// closed) safely discards outstanding results.
class ImportService : public QObject
{
    Q_OBJECT

public:
    static constexpr int ThumbnailSide = 256;

    ImportService(std::shared_ptr<SessionAssetStore> store, QThreadPool *pool, Limits limits = {},
                  QObject *parent = nullptr);
    ~ImportService() override;

    // Starts a batch; returns its id (0 if the plan has no sources).
    quint64 start(const ImportPlan &plan, const Id &documentToken, QPointF anchor);
    void cancel(quint64 batchId);
    void cancelAll();
    bool isBusy() const { return !m_batches.isEmpty(); }

signals:
    void progress(quint64 batchId, int done, int total);
    void batchFinished(const glacier::io::ImportBatchResult &result);

private:
    struct JobOutcome
    {
        std::optional<ImportedImage> image;
        ImportFailure failure;
    };
    struct Batch
    {
        ImportBatchResult result;
        std::shared_ptr<std::atomic_bool> cancelled;
        QList<std::optional<JobOutcome>> outcomes; // by source index
        QFutureWatcher<JobOutcome> *watcher = nullptr; // owned by this (QObject child)
    };

    static JobOutcome runJob(const ImportSource &source, const std::shared_ptr<SessionAssetStore> &store,
                             const Limits &limits, const std::shared_ptr<std::atomic_bool> &cancelled);
    void finishBatch(quint64 batchId);

    std::shared_ptr<SessionAssetStore> m_store;
    QThreadPool *m_pool;
    Limits m_limits;
    quint64 m_nextBatchId = 1;
    QHash<quint64, Batch> m_batches;
};

} // namespace glacier::io
