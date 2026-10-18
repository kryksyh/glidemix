/*
 * GlideMix
 */
#include "mixhousegrideditorview.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QVector>

using namespace au::mixhouse;

namespace {
const QColor kBackground(0x00, 0x00, 0x00);
const QColor kLow(0x2A, 0x6B, 0xFF);     // rekordbox 3-band: lows blue,
const QColor kCue(0xFF, 0x8C, 0x00);
const QColor kLoop(0x3F, 0xB9, 0x50, 60);
}

MixHouseGridEditorView::MixHouseGridEditorView(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setOpaquePainting(true);
}

void MixHouseGridEditorView::setModel(MixHouseGridEditorModel* model)
{
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        for (const auto signal : { &MixHouseGridEditorModel::positionChanged, &MixHouseGridEditorModel::gridChanged,
                                   &MixHouseGridEditorModel::phrasesChanged, &MixHouseGridEditorModel::zoomChanged,
                                   &MixHouseGridEditorModel::waveformChanged, &MixHouseGridEditorModel::loopChanged,
                                   &MixHouseGridEditorModel::cueChanged, &MixHouseGridEditorModel::trackChanged }) {
            connect(m_model, signal, this, [this]() { update(); });
        }
    }
    emit modelChanged();
    update();
}

void MixHouseGridEditorView::setOverview(bool overview)
{
    if (m_overview != overview) {
        m_overview = overview;
        emit modelChanged();
        update();
    }
}

double MixHouseGridEditorView::timeAtX(double x) const
{
    if (!m_model) {
        return 0.0;
    }
    return width() > 0 ? x / width() * m_model->durationSec() : 0.0;
}

void MixHouseGridEditorView::paint(QPainter* painter)
{
    QPainter& p = *painter;
    p.fillRect(QRectF(0, 0, width(), height()), kBackground);
    if (!m_model || !m_model->loaded()) {
        return;
    }
    paintOverview(p);
}

void MixHouseGridEditorView::paintPhrases(QPainter& p, const QRectF& area, double t0, double pxPerSec, bool labels)
{
    const auto& phrases = m_model->phraseList();
    QFont font = p.font();
    font.setPixelSize(std::clamp(static_cast<int>(area.height() * 0.6), 8, 12));
    font.setBold(true);
    p.setFont(font);
    const QFontMetricsF fm(font);
    for (size_t i = 0; i < phrases.size(); ++i) {
        const MixHousePhrase& phrase = phrases[i];
        const double x1 = (phrase.startSec - t0) * pxPerSec;
        const double x2 = (phrase.endSec - t0) * pxPerSec;
        if (x2 < area.left() || x1 > area.right()) {
            continue;
        }
        const QRectF block(std::max(area.left(), x1), area.top(), std::min(area.right(), x2) - std::max(area.left(), x1),
                           area.height());
        p.fillRect(block, phrase.color);
        p.fillRect(QRectF(x1, area.top(), 1.0, area.height()), QColor(0, 0, 0, 200));
        if (phrase.fillStartSec >= 0.0) {
            const double fx = std::max(block.left(), (phrase.fillStartSec - t0) * pxPerSec);
            const double y = area.center().y();
            const double line = std::max(2.0, std::round(area.height() * 0.3));
            p.fillRect(QRectF(fx, y - line / 2.0, block.right() - fx, line), QColor(0, 0, 0, 220));
            const double dot = std::max(1.0, line - 2.0);
            for (double x = fx + 1.0; x + dot <= block.right() - 1.0; x += dot * 2.0) {
                p.fillRect(QRectF(x, y - dot / 2.0, dot, dot), Qt::white);
            }
        }
        if (labels && block.width() > 24.0) {
            const QString text = fm.elidedText(phrase.label.toUpper(), Qt::ElideRight, block.width() - 8.0);
            p.setPen(Qt::white);
            p.drawText(block.adjusted(4.0, 0.0, -2.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, text);
        }
        if (static_cast<int>(i) == m_model->selectedPhrase()) {
            p.setPen(QPen(Qt::white, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(x1 + 1.0, area.top() + 1.0, x2 - x1 - 2.0, area.height() - 2.0));
        }
    }
}

void MixHouseGridEditorView::paintOverview(QPainter& p)
{
    const double w = width();
    const double h = height();
    const double duration = m_model->durationSec();
    if (duration <= 0.0) {
        return;
    }
    const double pps = w / duration;
    const double ribbon = std::min(8.0, h * 0.25);
    const QRectF wave(0, 0, w, h - ribbon - 1.0);
    const double half = wave.height() / 2.0;

    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize imageSize(static_cast<int>(w * dpr), static_cast<int>(wave.height() * dpr));
    const auto waveform = m_model->waveform();
    if (waveform && (m_overviewImage.size() != imageSize || m_overviewWaveform != waveform.get() || m_overviewImage.isNull())) {
        m_overviewImage = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
        m_overviewImage.setDevicePixelRatio(dpr);
        m_overviewImage.fill(Qt::transparent);
        m_overviewWaveform = waveform.get();
        QPainter ip(&m_overviewImage);
        const size_t bins = waveform->all.size();
        const double localMid = wave.height() / 2.0;
        for (int x = 0; x < static_cast<int>(w); ++x) {
            const size_t b0 = static_cast<size_t>(x / pps / waveform->binSec);
            const size_t b1 = std::min(bins, static_cast<size_t>((x + 1) / pps / waveform->binSec) + 1);
            float a = 0;
            double l = 0, m = 0, h = 0;
            for (size_t b = b0; b < b1; ++b) {
                a = std::max(a, waveform->all[b]);
                l += waveform->low[b];
                m += waveform->mid[b];
                h += waveform->high[b];
            }
            const double n = std::max<double>(1.0, double(b1 - b0));
            ip.setPen(QPen(MixHouseGridEditorModel::bandColor(float(l / n), float(m / n), float(h / n)), 1.0));
            ip.drawLine(QLineF(x + 0.5, localMid - a * half, x + 0.5, localMid + a * half));
        }
    }
    if (!m_overviewImage.isNull()) {
        p.drawImage(QPointF(0, wave.top()), m_overviewImage);
    }
    if (m_model->loopActive()) {
        p.fillRect(QRectF(m_model->loopStartSec() * pps, 0, (m_model->loopEndSec() - m_model->loopStartSec()) * pps, wave.height()),
                   kLoop);
    }
    paintPhrases(p, QRectF(0, h - ribbon, w, ribbon), 0.0, pps, false);
    p.fillRect(QRectF(m_model->cueSec() * pps - 1.0, 0, 2.0, wave.height()), kCue);
    p.fillRect(QRectF(m_model->positionSec() * pps - 1.0, 0, 2.0, h), Qt::white);
}

void MixHouseGridEditorView::mousePressEvent(QMouseEvent* event)
{
    if (!m_model || !m_model->loaded()) {
        event->ignore();
        return;
    }
    m_model->seek(timeAtX(event->position().x()));
    event->accept();
}

void MixHouseGridEditorView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_model && m_model->loaded()) {
        m_model->seek(timeAtX(std::clamp(event->position().x(), 0.0, width())));
    }
}
