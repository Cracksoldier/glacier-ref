#pragma once

#include "glacier/core/BoardDocument.h"

#include <QByteArray>
#include <QGraphicsItem>
#include <QGraphicsTextItem>
#include <QPointer>

namespace glacier::canvas {

class ImageCache;

// Base of all board graphics items: presentation of one document entity.
// Geometry comes from the BoardDocument; items never own persistent state.
class BoardItem : public QGraphicsItem
{
public:
    explicit BoardItem(const Id &entityId) : m_id(entityId) {}

    const Id &entityId() const { return m_id; }
    // The entity's visible rectangle in item-local coordinates.
    virtual QRectF localRect() const = 0;
    bool isLocked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

private:
    Id m_id;
    bool m_locked = false;
};

class ImageItem : public BoardItem
{
public:
    enum { Type = UserType + 1 };

    ImageItem(const ImageItemData &data, const QByteArray &sha256, ImageCache *cache);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QRectF localRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setData(const ImageItemData &data);
    const QByteArray &sha256() const { return m_sha256; }
    QSizeF displaySize() const { return m_displaySize; }
    QRectF crop() const { return m_previewCrop ? *m_previewCrop : m_crop; }
    void setGrayscale(bool grayscale);

    // Crop editing: show the full image with the crop highlighted.
    void setCropEditing(bool editing);
    void setPreviewCrop(std::optional<QRectF> crop);

    // Device pixels needed for the longest side at the given level of detail.
    int neededSide(qreal levelOfDetail, qreal devicePixelRatio) const;

private:
    QByteArray m_sha256;
    QSizeF m_displaySize;
    QRectF m_crop{0, 0, 1, 1};
    std::optional<QRectF> m_previewCrop;
    bool m_cropEditing = false;
    bool m_grayscale = false;
    QPointer<ImageCache> m_cache;
};

class NoteEditor;

class NoteItem : public BoardItem
{
public:
    enum { Type = UserType + 2 };

    explicit NoteItem(const NoteData &data);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QRectF localRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setData(const NoteData &data);
    const NoteData &data() const { return m_data; }
    void setPreviewWidth(std::optional<double> width);

    // Text editing (spec §3: plain text; Escape leaves editing).
    NoteEditor *beginEditing();
    QString endEditing();
    bool isEditing() const { return m_editor != nullptr; }
    NoteEditor *editor() const { return m_editor; }

private:
    void syncEditorGeometry();
    double width() const { return m_previewWidth ? *m_previewWidth : m_data.width; }

    NoteData m_data;
    std::optional<double> m_previewWidth;
    double m_editingHeight = 0.0;
    NoteEditor *m_editor = nullptr; // child item, owned via parent item
};

// Plain-text editor used inside a NoteItem: pasting inserts plain text only
// and drops are refused.
class NoteEditor : public QGraphicsTextItem
{
    Q_OBJECT

public:
    explicit NoteEditor(QGraphicsItem *parent);

protected:
    void keyPressEvent(QKeyEvent *event) override;
};

class GroupItem : public BoardItem
{
public:
    enum { Type = UserType + 3 };

    explicit GroupItem(const GroupData &data);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QRectF localRect() const override;
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}

    void setData(const GroupData &data);
    const GroupData &data() const { return m_data; }

private:
    GroupData m_data;
};

QTransform toQTransform(const Affine2D &t);
Affine2D fromQTransform(const QTransform &t);

} // namespace glacier::canvas
