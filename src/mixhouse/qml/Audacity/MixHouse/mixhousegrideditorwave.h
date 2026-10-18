/*
 * GlideMix
 */
#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration>

#include "mixhousegrideditormodel.h"

namespace au::mixhouse {
/// The Beatgrid editor's big waveform: the track around the playhead (which
/// stays in the middle, as on a DJ deck) with its beat grid, numbered bars,
/// cue, loop and phrase ribbon.
///
/// Drawn on the GPU so it scrolls smoothly: the waveform is rendered once per
/// 1024 px strip at the current zoom into textures that only move while
/// playing; grid, phrases and playhead are plain rectangles. Repaints every
/// displayed frame while the deck plays.
///
/// Drag to scratch, click to seek (or to pick a phrase in the ribbon), wheel
/// to zoom.
class MixHouseGridEditorWave : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(au::mixhouse::MixHouseGridEditorModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(double ribbonHeight MEMBER m_ribbonHeight NOTIFY modelChanged)

public:
    explicit MixHouseGridEditorWave(QQuickItem* parent = nullptr);

    MixHouseGridEditorModel* model() const { return m_model; }
    void setModel(MixHouseGridEditorModel* model);

signals:
    void modelChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;

private:
    double timeAtX(double x) const;

    enum class Drag {
        None,
        Scratch,
        PhraseEdge,
        PhraseBody,
        LoopEdge
    };
    struct Hit {
        Drag drag = Drag::None;
        int index = -1;
        int edge = 0;
    };
    //! What a press at `pos` would grab: a phrase edge or body in the ribbon, a loop edge, or the waveform.
    Hit hitTest(const QPointF& pos) const;
    Hit m_hit;

    QPointer<MixHouseGridEditorModel> m_model;
    double m_ribbonHeight = 20.0;
    QPointF m_pressPos;
    double m_pressTime = 0.0;
    bool m_dragging = false;
};
}
