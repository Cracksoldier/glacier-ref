#include "glacier/io/ImportService.h"

#include "glacier/core/SessionAssetStore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentMap>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("glacier::io", text);
}

QString cancelledReason()
{
    return tr("Import was cancelled.");
}

} // namespace

ImportService::ImportService(std::shared_ptr<SessionAssetStore> store, QThreadPool *pool, Limits limits,
                             QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
    , m_pool(pool)
    , m_limits(limits)
{
}

ImportService::~ImportService()
{
    // Watchers are children and get destroyed with us; running jobs finish on
    // their own (they only hold value data and shared ownership of the store).
    cancelAll();
}

quint64 ImportService::start(const ImportPlan &plan, const Id &documentToken, QPointF anchor)
{
    if (plan.sources.isEmpty())
        return 0;

    const quint64 id = m_nextBatchId++;
    Batch &batch = m_batches[id];
    batch.result.batchId = id;
    batch.result.documentToken = documentToken;
    batch.result.anchor = anchor;
    batch.result.notices = plan.notices;
    batch.cancelled = std::make_shared<std::atomic_bool>(false);
    batch.outcomes.resize(plan.sources.size());

    auto *watcher = new QFutureWatcher<JobOutcome>(this);
    batch.watcher = watcher;
    const int total = int(plan.sources.size());
    connect(watcher, &QFutureWatcherBase::resultReadyAt, this, [this, id, watcher](int index) {
        const auto it = m_batches.find(id);
        if (it == m_batches.end())
            return;
        it->outcomes[index] = watcher->resultAt(index);
    });
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this, [this, id, total](int done) {
        if (m_batches.contains(id))
            emit progress(id, done, total);
    });
    connect(watcher, &QFutureWatcherBase::finished, this, [this, id] { finishBatch(id); });

    auto store = m_store;
    const Limits limits = m_limits;
    auto cancelled = batch.cancelled;
    watcher->setFuture(QtConcurrent::mapped(m_pool, plan.sources,
        [store, limits, cancelled](const ImportSource &source) {
            return runJob(source, store, limits, cancelled);
        }));
    emit progress(id, 0, total);
    return id;
}

void ImportService::cancel(quint64 batchId)
{
    const auto it = m_batches.find(batchId);
    if (it == m_batches.end())
        return;
    it->cancelled->store(true);
    it->result.cancelled = true;
    it->watcher->cancel();
}

void ImportService::cancelAll()
{
    const QList<quint64> ids = m_batches.keys();
    for (quint64 id : ids)
        cancel(id);
}

void ImportService::finishBatch(quint64 batchId)
{
    const auto it = m_batches.find(batchId);
    if (it == m_batches.end())
        return;
    Batch batch = std::move(*it);
    m_batches.erase(it);
    batch.watcher->deleteLater();

    for (const std::optional<JobOutcome> &outcome : std::as_const(batch.outcomes)) {
        if (!outcome)
            continue; // not run because the batch was cancelled
        if (outcome->image)
            batch.result.images.append(*outcome->image);
        else if (outcome->failure.reason != cancelledReason())
            batch.result.failures.append(outcome->failure);
    }
    emit batchFinished(batch.result);
}

ImportService::JobOutcome ImportService::runJob(const ImportSource &source,
                                                const std::shared_ptr<SessionAssetStore> &store,
                                                const Limits &limits,
                                                const std::shared_ptr<std::atomic_bool> &cancelled)
{
    JobOutcome outcome;
    outcome.failure.displayName = source.displayName;
    auto fail = [&outcome](const QString &reason) {
        outcome.failure.reason = reason;
        return outcome;
    };
    if (cancelled->load())
        return fail(cancelledReason());

    QByteArray bytes;
    if (source.kind == ImportSource::Kind::File) {
        const QFileInfo info(source.filePath);
        if (!info.isFile())
            return fail(tr("The file could not be found."));
        if (info.size() > limits.maxAssetBytes)
            return fail(tr("The file is larger than the %1 MiB limit.").arg(limits.maxAssetBytes / (1024 * 1024)));
        QFile file(source.filePath);
        if (!file.open(QIODevice::ReadOnly))
            return fail(tr("The file could not be opened."));
        bytes = file.read(limits.maxAssetBytes + 1);
        if (bytes.size() > limits.maxAssetBytes)
            return fail(tr("The file is larger than the %1 MiB limit.").arg(limits.maxAssetBytes / (1024 * 1024)));
    } else {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!QImageWriter(&buffer, "png").write(source.image))
            return fail(tr("The pasted image could not be encoded."));
    }
    if (cancelled->load())
        return fail(cancelledReason());

    const ProbeOutcome probe = probeImage(bytes, limits);
    if (!probe.info)
        return fail(probe.error);

    QString error;
    QImage thumbnail = decodeImage(bytes, *probe.info, ThumbnailSide, limits, &error);
    if (thumbnail.isNull())
        return fail(error);
    if (cancelled->load())
        return fail(cancelledReason());

    const auto stored = store->put(bytes, &error);
    if (!stored)
        return fail(error);

    ImportedImage image;
    image.displayName = source.displayName;
    image.sha256 = stored->sha256;
    image.mediaType = mediaType(probe.info->format);
    image.orientedSize = probe.info->orientedSize;
    image.byteLength = bytes.size();
    image.info = *probe.info;
    image.thumbnail = std::move(thumbnail);
    image.notices = probe.notices;
    outcome.image = std::move(image);
    return outcome;
}

} // namespace glacier::io
