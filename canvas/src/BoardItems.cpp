#include "glacier/canvas/BoardItems.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/editing/NoteLayout.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPaintDevice>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

using glacier::editing::NoteLayout;

namespace glacier::canvas {

namespace {
const QColor PlaceholderColor(0x50, 0x53, 0x57);
const QColor CropShade(0, 0, 0, 150);

QColor argb(quint32 v)
{
    return QColor::fromRgba(v);
}
} // namespace

QTransform toQTransform(const Affine2D &t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy);
}

Affine2D fromQTransform(const QTransform &t)
{
    return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}

// --- ImageItem ---------------------------------------------------------------

ImageItem::ImageItem(const ImageItemData &data, const QByteArray &sha256, ImageCache *cache)
    : BoardItem(data.id)
    , m_sha256(sha256)
    , m_cache(cache)
{
    setData(data);
}

void ImageItem::setData(const ImageItemData &data)
{
    prepareGeometryChange();
    m_displaySize = data.displaySize;
    m_crop = data.crop;
    setLocked(data.locked);
    setTransform(toQTransform(data.transform));
    update();
}

QRectF ImageItem::localRect() const
{
    const QRectF c = crop();
    return QRectF(c.x() * m_displaySize.width(), c.y() * m_displaySize.height(),
                  c.width() * m_displaySize.width(), c.height() * m_displaySize.height());
}

QRectF ImageItem::boundingRect() const
{
    return m_cropEditing ? QRectF(QPointF(0, 0), m_displaySize) : localRect();
}

void ImageItem::setGrayscale(bool grayscale)
{
    if (m_grayscale != grayscale) {
        m_grayscale = grayscale;
        update();
    }
}

void ImageItem::setCropEditing(bool editing)
{
    prepareGeometryChange();
    m_cropEditing = editing;
    update();
}

void ImageItem::setPreviewCrop(std::optional<QRectF> crop)
{
    prepareGeometryChange();
    m_previewCrop = crop;
    update();
}

int ImageItem::neededSide(qreal levelOfDetail, qreal devicePixelRatio) const
{
    const qreal longest = std::max(m_displaySize.width(), m_displaySize.height());
    return std::max(1, int(std::ceil(longest * levelOfDetail * devicePixelRatio)));
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap pixmap = m_cache ? m_cache->best(m_sha256, neededSide(lod, dpr), m_grayscale) : QPixmap();

    const QRectF full(QPointF(0, 0), m_displaySize);
    const QRectF visible = localRect();
    const QRectF target = m_cropEditing ? full : visible;
    if (pixmap.isNull()) {
        painter->fillRect(target, PlaceholderColor);
    } else {
        const double sx = pixmap.width() / m_displaySize.width();
        const double sy = pixmap.height() / m_displaySize.height();
        const QRectF source(target.x() * sx, target.y() * sy, target.width() * sx, target.height() * sy);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(target, pixmap, source);
    }
    if (m_cropEditing) {
        // Darken what the crop removes.
        QPainterPath outside;
        outside.addRect(full);
        QPainterPath inside;
        inside.addRect(visible);
        painter->fillPath(outside.subtracted(inside), CropShade);
    }
}

// --- NoteItem ----------------------------------------------------------------

NoteItem::NoteItem(const NoteData &data)
    : BoardItem(data.id)
{
    setData(data);
}

void NoteItem::setData(const NoteData &data)
{
    prepareGeometryChange();
    m_data = data;
    setLocked(data.locked);
    setTransform(toQTransform(data.transform));
    syncEditorGeometry();
    update();
}

void NoteItem::setPreviewWidth(std::optional<double> width)
{
    prepareGeometryChange();
    m_previewWidth = width;
    syncEditorGeometry();
    update();
}

QRectF NoteItem::localRect() const
{
    const double h = m_editor ? std::max(m_editingHeight, m_data.height)
                              : (m_previewWidth ? NoteLayout::height(m_data.text, *m_previewWidth, m_data.fontSize)
                                                : m_data.height);
    return QRectF(0, 0, width(), h);
}

QRectF NoteItem::boundingRect() const
{
    return localRect();
}

void NoteItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QRectF rect = localRect();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(argb(m_data.backgroundColor));
    painter->drawRoundedRect(rect, 4, 4);
    if (!m_editor)
        NoteLayout::paint(painter, m_data.text, width(), m_data.fontSize, argb(m_data.textColor));
}

NoteEditor *NoteItem::beginEditing()
{
    if (m_editor)
        return m_editor;
    m_editor = new NoteEditor(this);
    m_editor->setFont(NoteLayout::font(m_data.fontSize));
    m_editor->setDefaultTextColor(argb(m_data.textColor));
    m_editor->setPlainText(m_data.text);
    m_editor->setPos(NoteLayout::Padding, NoteLayout::Padding);
    m_editor->document()->setDocumentMargin(0);
    QObject::connect(m_editor->document(), &QTextDocument::contentsChanged, m_editor, [this] {
        prepareGeometryChange();
        m_editingHeight = NoteLayout::height(m_editor->toPlainText(), width(), m_data.fontSize);
        update();
    });
    m_editingHeight = m_data.height;
    syncEditorGeometry();
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    update();
    return m_editor;
}

QString NoteItem::endEditing()
{
    if (!m_editor)
        return m_data.text;
    const QString text = m_editor->toPlainText();
    prepareGeometryChange();
    delete m_editor; // child item
    m_editor = nullptr;
    update();
    return text;
}

void NoteItem::syncEditorGeometry()
{
    if (m_editor)
        m_editor->setTextWidth(std::max(1.0, width() - 2 * NoteLayout::Padding));
}

NoteEditor::NoteEditor(QGraphicsItem *parent)
    : QGraphicsTextItem(parent)
{
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setAcceptDrops(false);
}

void NoteEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Paste)) {
        // Plain text only (spec §3): never insert rich text or HTML.
        textCursor().insertText(QGuiApplication::clipboard()->text());
        event->accept();
        return;
    }
    QGraphicsTextItem::keyPressEvent(event);
}

// --- GroupItem ---------------------------------------------------------------

GroupItem::GroupItem(const GroupData &data)
    : BoardItem(data.id)
{
    setFlag(ItemHasNoContents);
    setData(data);
}

void GroupItem::setData(const GroupData &data)
{
    prepareGeometryChange();
    m_data = data;
    setLocked(data.locked);
    setTransform(toQTransform(data.transform));
}

QRectF GroupItem::localRect() const
{
    QRectF bounds;
    const auto children = childItems();
    for (const QGraphicsItem *child : children) {
        if (const auto *item = dynamic_cast<const BoardItem *>(child)) {
            const QRectF b = child->transform().mapRect(item->localRect());
            bounds = bounds.isNull() ? b : bounds.united(b);
        }
    }
    return bounds;
}

QRectF GroupItem::boundingRect() const
{
    return localRect();
}

} // namespace glacier::canvas
