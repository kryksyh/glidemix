/*
 * GlideMix
 */
#include "mixhousegrideditorwave.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QVector>

#include "au3-mixtape-engine/MixtapeDeck.h"
#include "global/types/color.h"

using namespace au::mixhouse;

namespace {
const QColor kBackground(0x00, 0x00, 0x00);   // rekordbox: black behind the waveform
const QColor kRuler(0x17, 0x1A, 0x21);
const QColor kLow(0x2A, 0x6B, 0xFF);     // rekordbox 3-band: lows blue,
const QColor kMid(0xFF, 0x9A, 0x1A);     // mids orange,
const QColor kHigh(0xF2, 0xF2, 0xF2);    // highs white
const QColor kBeatLine(255, 255, 255, 60);
const QColor kBarLine(0xE5, 0x39, 0x35);
const QColor kCue(0xFF, 0x8C, 0x00);
const QColor kLoop(0x3F, 0xB9, 0x50, 60);
constexpr double kRulerHeight = 18.0;
constexpr int kTileWidth = 1024;

//! Owns the textures the child nodes show; lives and dies on the render thread.
class RootNode : public QSGNode
{
public:
    ~RootNode() override
    {
        clearTiles();
        for (auto& [key, texture] : texts) {
            delete texture;
        }
    }

    void clearTiles()
    {
        for (auto& [index, texture] : tiles) {
            delete texture;
        }
        tiles.clear();
    }

    void clearChildren()
    {
        while (QSGNode* child = firstChild()) {
            removeChildNode(child);
            delete child;
        }
    }

    std::map<long long, QSGTexture*> tiles;
    std::map<QString, QSGTexture*> texts;
    double tilePxPerSec = 0.0;
    double tileHeight = 0.0;
    qreal dpr = 0.0;
    const mixtape::DeckAudio* audio = nullptr;
    QRgb body = 0;
};

void addRect(QSGNode* parent, const QRectF& rect, const QColor& color)
{
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return;
    }
    parent->appendChildNode(new QSGSimpleRectNode(rect, color));
}

void addTexture(QSGNode* parent, QSGTexture* texture, const QRectF& rect, const QRectF& source = QRectF())
{
    auto* node = new QSGSimpleTextureNode();
    node->setTexture(texture);
    node->setOwnsTexture(false);
    node->setFiltering(QSGTexture::Linear);
    node->setRect(rect);
    if (source.isValid()) {
        node->setSourceRect(source);
    }
    parent->appendChildNode(node);
}

//! One strip of waveform at a zoom, rekordbox-style: each column's height is
//! the real sample peak there, its colour the bass/mid/high balance (lows
//! red, mids green, highs blue), with a brighter core at the RMS. Zoomed past
//! one sample a pixel it draws the sample line in the local colour.
QImage renderTile(const mixtape::DeckAudio& audio, const MixHouseGridEditorModel::Waveform& bands, long long index,
                  double pxPerSec, double height, qreal dpr)
{
    QImage image(static_cast<int>(kTileWidth * dpr), static_cast<int>(std::max(1.0, height) * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    const int64_t frames = audio.Frames();
    const unsigned channels = audio.channels;
    const size_t bins = bands.low.size();
    if (frames <= 0 || channels == 0 || bins == 0 || bands.binSec <= 0.0) {
        return image;
    }
    const auto mono = [&](int64_t f) {
        double sum = 0.0;
        for (unsigned c = 0; c < channels; ++c) {
            sum += audio.samples[static_cast<size_t>(f) * channels + c];
        }
        return static_cast<float>(sum / channels);
    };
    const auto colourAt = [&](double seconds, double spanSec) {
        const size_t b0 = std::min(bins - 1, static_cast<size_t>(std::max(0.0, seconds) / bands.binSec));
        const size_t b1 = std::min(bins, std::max(b0 + 1, static_cast<size_t>((seconds + spanSec) / bands.binSec)));
        double l = 0, m = 0, h = 0;
        for (size_t k = b0; k < b1; ++k) {
            l += bands.low[k];
            m += bands.mid[k];
            h += bands.high[k];
        }
        const double n = double(b1 - b0);
        return MixHouseGridEditorModel::bandColor(float(l / n), float(m / n), float(h / n));
    };
    QPainter p(&image);
    const double midY = height / 2.0;
    const double half = height / 2.0 - 2.0;
    const double step = 1.0 / dpr;
    const int columns = static_cast<int>(kTileWidth * dpr);
    const double rate = audio.sampleRate;
    const double samplesPerColumn = rate * step / pxPerSec;
    const double tileStart = index * kTileWidth / pxPerSec;   // seconds

    if (samplesPerColumn >= 1.0) {
        for (int c = 0; c < columns; ++c) {
            const double a = (tileStart + c * step / pxPerSec) * rate;
            auto f0 = static_cast<int64_t>(std::floor(a));
            auto f1 = static_cast<int64_t>(std::floor(a + samplesPerColumn));
            if (f1 <= 0) {
                continue;
            }
            f0 = std::max<int64_t>(0, f0);
            f1 = std::min(frames, std::max(f1, f0 + 1));
            if (f0 >= frames) {
                break;
            }
            float lo = 1.0f, hi = -1.0f;
            double squares = 0.0;
            for (int64_t f = f0; f < f1; ++f) {
                const float v = mono(f);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                squares += double(v) * v;
            }
            const double r = std::sqrt(squares / double(f1 - f0));
            const double x = c * step + step / 2.0;
            const QColor colour = colourAt(f0 / rate, (f1 - f0) / rate);
            p.setPen(QPen(colour, step));
            p.drawLine(QPointF(x, midY - hi * half), QPointF(x, midY - lo * half));
            p.setPen(QPen(colour.lighter(135), step));
            p.drawLine(QPointF(x, midY - std::min<double>(r, hi) * half), QPointF(x, midY - std::max<double>(-r, lo) * half));
        }
    } else {
        const double t0 = tileStart;
        const double t1 = tileStart + kTileWidth / pxPerSec;
        const int64_t f0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor(t0 * rate)) - 1);
        const int64_t f1 = std::min(frames - 1, static_cast<int64_t>(std::ceil(t1 * rate)) + 1);
        if (f1 > f0) {
            p.setRenderHint(QPainter::Antialiasing);
            const double spacing = pxPerSec / rate;
            QPointF previous((f0 / rate - t0) * pxPerSec, midY - mono(f0) * half);
            for (int64_t f = f0 + 1; f <= f1; ++f) {
                const QPointF point((f / rate - t0) * pxPerSec, midY - mono(f) * half);
                const QColor colour = colourAt(f / rate, bands.binSec);
                p.setPen(QPen(colour, 1.5));
                p.drawLine(previous, point);
                if (spacing >= 6.0) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(colour);
                    p.drawEllipse(point, 2.2, 2.2);
                }
                previous = point;
            }
        }
    }
    return image;
}

QImage renderText(const QString& text, int pixelSize, const QColor& color, qreal dpr, QSizeF& logicalSize)
{
    QFont font;
    font.setPixelSize(pixelSize);
    font.setBold(true);
    const QFontMetricsF fm(font);
    logicalSize = QSizeF(std::ceil(fm.horizontalAdvance(text)) + 2.0, std::ceil(fm.height()));
    QImage image(static_cast<int>(logicalSize.width() * dpr), static_cast<int>(logicalSize.height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRectF(QPointF(0, 0), logicalSize), Qt::AlignLeft | Qt::AlignVCenter, text);
    return image;
}

QImage renderTriangle(const QColor& color, bool pointingDown, qreal dpr)
{
    QImage image(static_cast<int>(10 * dpr), static_cast<int>(8 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    if (pointingDown) {
        path.moveTo(0, 0);
        path.lineTo(10, 0);
        path.lineTo(5, 8);
    } else {
        path.moveTo(0, 8);
        path.lineTo(10, 8);
        path.lineTo(5, 0);
    }
    path.closeSubpath();
    p.fillPath(path, color);
    return image;
}
}

MixHouseGridEditorWave::MixHouseGridEditorWave(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

MixHouseGridEditorWave::Hit MixHouseGridEditorWave::hitTest(const QPointF& pos) const
{
    Hit hit;
    if (!m_model || !m_model->loaded()) {
        return hit;
    }
    constexpr double kGrab = 9.0;
    const double pps = m_model->pxPerSec();
    const auto xOf = [&](double t) { return std::round(width() / 2.0) + (t - m_model->livePosition()) * pps; };
    if (pos.y() >= height() - m_ribbonHeight) {
        const auto& phrases = m_model->phraseList();
        for (size_t i = 0; i < phrases.size(); ++i) {
            if (std::abs(pos.x() - xOf(phrases[i].startSec)) <= kGrab) {
                return { Drag::PhraseEdge, static_cast<int>(i), 0 };
            }
            if (std::abs(pos.x() - xOf(phrases[i].endSec)) <= kGrab && i + 1 == phrases.size()) {
                return { Drag::PhraseEdge, static_cast<int>(i), 1 };
            }
        }
        const int index = [&]() {
            const double t = timeAtX(pos.x());
            for (size_t i = 0; i < phrases.size(); ++i) {
                if (t >= phrases[i].startSec && t < phrases[i].endSec) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }();
        return index >= 0 ? Hit { Drag::PhraseBody, index, 0 } : hit;
    }
    if (m_model->loopActive()) {
        if (std::abs(pos.x() - xOf(m_model->loopStartSec())) <= kGrab) {
            return { Drag::LoopEdge, -1, 0 };
        }
        if (std::abs(pos.x() - xOf(m_model->loopEndSec())) <= kGrab) {
            return { Drag::LoopEdge, -1, 1 };
        }
    }
    return { Drag::Scratch, -1, 0 };
}

void MixHouseGridEditorWave::hoverMoveEvent(QHoverEvent* event)
{
    const Hit hit = hitTest(event->position());
    setCursor(hit.drag == Drag::PhraseEdge || hit.drag == Drag::LoopEdge ? Qt::SizeHorCursor
              : hit.drag == Drag::PhraseBody ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void MixHouseGridEditorWave::setModel(MixHouseGridEditorModel* model)
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
                                   &MixHouseGridEditorModel::cueChanged, &MixHouseGridEditorModel::trackChanged,
                                   &MixHouseGridEditorModel::playingChanged }) {
            connect(m_model, signal, this, [this]() { update(); });
        }
    }
    emit modelChanged();
    update();
}

void MixHouseGridEditorWave::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode* MixHouseGridEditorWave::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* root = static_cast<RootNode*>(oldNode);
    if (!root) {
        root = new RootNode();
    }
    root->clearChildren();
    const double w = width();
    const double h = height();
    addRect(root, QRectF(0, 0, w, h), kBackground);
    if (!m_model || !m_model->loaded() || !window() || w <= 0.0 || h <= 0.0) {
        return root;
    }
    QQuickWindow* win = window();
    const qreal dpr = win->effectiveDevicePixelRatio();
    const double pps = m_model->pxPerSec();
    const double cx = std::round(w / 2.0);
    const double position = m_model->livePosition();
    const double t0 = position - cx / pps;
    const double t1 = position + (w - cx) / pps;
    const double ribbon = m_ribbonHeight;
    const QRectF wave(0, kRulerHeight, w, std::max(10.0, h - kRulerHeight - ribbon));
    // Whole device pixels, so strips and lines don't shimmer while scrolling.
    const auto snap = [dpr](double x) { return std::round(x * dpr) / dpr; };

    addRect(root, QRectF(0, 0, w, kRulerHeight), kRuler);
    if (m_model->loopActive()) {
        const double lx1 = (m_model->loopStartSec() - t0) * pps;
        const double lx2 = (m_model->loopEndSec() - t0) * pps;
        addRect(root, QRectF(lx1, wave.top(), lx2 - lx1, wave.height()), kLoop);
    }

    // Waveform strips
    const auto audio = m_model->audio();
    const auto bands = m_model->waveform();
    const QColor body = QColor(0, 0, 0);
    if (audio && bands) {
        if (root->tilePxPerSec != pps || root->tileHeight != wave.height() || root->dpr != dpr
            || root->audio != audio.get() || root->body != body.rgba()) {
            root->clearTiles();
            root->tilePxPerSec = pps;
            root->tileHeight = wave.height();
            root->dpr = dpr;
            root->audio = audio.get();
            root->body = body.rgba();
        }
        const double absoluteLeft = t0 * pps;
        const long long first = static_cast<long long>(std::floor(absoluteLeft / kTileWidth));
        const long long last = static_cast<long long>(std::floor((absoluteLeft + w) / kTileWidth));
        for (long long i = first; i <= last; ++i) {
            QSGTexture*& texture = root->tiles[i];
            if (!texture) {
                texture = win->createTextureFromImage(renderTile(*audio, *bands, i, pps, wave.height(), dpr));
            }
            addTexture(root, texture, QRectF(snap(i * kTileWidth - absoluteLeft), wave.top(), kTileWidth, wave.height()));
        }
        for (auto it = root->tiles.begin(); it != root->tiles.end();) {
            if (it->first < first - 2 || it->first > last + 2) {
                delete it->second;
                it = root->tiles.erase(it);
            } else {
                ++it;
            }
        }
    }

    const auto text = [&](const QString& string, int pixelSize, const QColor& color) -> std::pair<QSGTexture*, QSizeF> {
        const QString key = QStringLiteral("%1|%2|%3|%4").arg(string).arg(pixelSize).arg(color.rgba()).arg(dpr);
        QSGTexture*& texture = root->texts[key];
        QSizeF size;
        const QImage image = texture ? QImage() : renderText(string, pixelSize, color, dpr, size);
        if (!texture) {
            texture = win->createTextureFromImage(image);
        }
        return { texture, QSizeF(texture->textureSize()) / dpr };
    };
    const auto triangle = [&](const QColor& color, bool down) {
        const QString key = QStringLiteral("triangle|%1|%2|%3").arg(color.rgba()).arg(down).arg(dpr);
        QSGTexture*& texture = root->texts[key];
        if (!texture) {
            texture = win->createTextureFromImage(renderTriangle(color, down, dpr));
        }
        return texture;
    };

    // Beat grid: white beats, red numbered bars counted from bar 1.
    const double beat = m_model->beatSec();
    if (m_model->bpm() > 0.0 && beat * pps >= 3.0) {
        const double firstBeat = m_model->firstBeatSec();
        const double downbeat = m_model->downbeatSec();
        const long long k0 = static_cast<long long>(std::floor((t0 - firstBeat) / beat)) - 1;
        const long long k1 = static_cast<long long>(std::ceil((t1 - firstBeat) / beat)) + 1;
        const bool labelEveryBar = 4.0 * beat * pps >= 28.0;
        for (long long k = std::max(0LL, k0); k <= k1; ++k) {
            const double t = firstBeat + k * beat;
            const double x = snap((t - t0) * pps);
            const long long fromBarOne = std::llround((t - downbeat) / beat);
            const bool barStart = ((fromBarOne % 4) + 4) % 4 == 0;
            if (barStart) {
                addRect(root, QRectF(x - 0.75, wave.top(), 1.5, wave.height()), kBarLine);
                addTexture(root, triangle(kBarLine, true), QRectF(x - 5.0, wave.top(), 10.0, 8.0));
                const long long bar = fromBarOne >= 0 ? fromBarOne / 4 + 1 : -((-fromBarOne - 1) / 4 + 1);
                if (labelEveryBar || bar % 4 == 1) {
                    const auto [texture, size] = text(QString::number(bar), 11, QColor(0xE8, 0xEA, 0xEE));
                    addTexture(root, texture, QRectF(x + 3.0, (kRulerHeight - size.height()) / 2.0, size.width(), size.height()));
                }
            } else {
                addRect(root, QRectF(x, wave.top(), 1.0, wave.height()), kBeatLine);
            }
        }
    }

    const double cueX = (m_model->cueSec() - t0) * pps;
    if (cueX > -10.0 && cueX < w + 10.0) {
        addTexture(root, triangle(kCue, false), QRectF(snap(cueX) - 6.0, wave.bottom() - 9.0, 12.0, 9.0));
    }

    // Phrase ribbon
    const QRectF area(0, h - ribbon, w, ribbon);
    const auto& phrases = m_model->phraseList();
    for (size_t i = 0; i < phrases.size(); ++i) {
        const MixHousePhrase& phrase = phrases[i];
        const double x1 = snap((phrase.startSec - t0) * pps);
        const double x2 = snap((phrase.endSec - t0) * pps);
        if (x2 < 0.0 || x1 > w) {
            continue;
        }
        const double left = std::max(0.0, x1);
        const double right = std::min(w, x2);
        addRect(root, QRectF(left, area.top(), right - left, area.height()), phrase.color);
        if (x1 >= 0.0) {
            addRect(root, QRectF(x1, area.top(), 1.0, area.height()), QColor(0, 0, 0, 200));
        }
        if (phrase.fillStartSec >= 0.0) {
            const double fx = std::max(left, snap((phrase.fillStartSec - t0) * pps));
            const double y = area.center().y();
            const double line = std::max(2.0, std::round(area.height() * 0.3));
            addRect(root, QRectF(fx, y - line / 2.0, right - fx, line), QColor(0, 0, 0, 220));
            const double dot = std::max(1.0, line - 2.0);
            // Dots keep their place on the track while it scrolls.
            const double phase = std::fmod(std::fmod(fx + t0 * pps, dot * 2.0) + dot * 2.0, dot * 2.0);
            for (double x = fx + 1.0 + (dot * 2.0 - phase); x + dot <= right - 1.0; x += dot * 2.0) {
                addRect(root, QRectF(x, y - dot / 2.0, dot, dot), Qt::white);
            }
        }
        if (right - left > 24.0) {
            const auto [texture, size] = text(phrase.label.toUpper(), 11, Qt::white);
            const double labelX = left + 4.0;
            const double visible = std::min(size.width(), right - labelX - 2.0);
            if (visible > 4.0) {
                addTexture(root, texture, QRectF(labelX, area.top() + (area.height() - size.height()) / 2.0, visible, size.height()),
                           QRectF(0, 0, visible * dpr, size.height() * dpr));
            }
        }
        if (static_cast<int>(i) == m_model->selectedPhrase()) {
            const QRectF r(x1, area.top(), x2 - x1, area.height());
            addRect(root, QRectF(r.left(), r.top(), r.width(), 2.0), Qt::white);
            addRect(root, QRectF(r.left(), r.bottom() - 2.0, r.width(), 2.0), Qt::white);
            addRect(root, QRectF(r.left(), r.top(), 2.0, r.height()), Qt::white);
            addRect(root, QRectF(r.right() - 2.0, r.top(), 2.0, r.height()), Qt::white);
        }
    }

    // The playhead stays in the middle.
    addRect(root, QRectF(cx - 1.0, 0, 2.0, h), Qt::white);

    if (m_model->playing()) {
        update();   // next displayed frame
    }
    return root;
}

double MixHouseGridEditorWave::timeAtX(double x) const
{
    if (!m_model) {
        return 0.0;
    }
    return m_model->livePosition() + (x - std::round(width() / 2.0)) / m_model->pxPerSec();
}

void MixHouseGridEditorWave::mousePressEvent(QMouseEvent* event)
{
    if (!m_model || !m_model->loaded()) {
        event->ignore();
        return;
    }
    m_pressPos = event->position();
    m_pressTime = m_model->livePosition();
    m_dragging = false;
    m_hit = hitTest(event->position());
    event->accept();
}

void MixHouseGridEditorWave::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_model || !m_model->loaded()) {
        return;
    }
    const double dx = event->position().x() - m_pressPos.x();
    if (!m_dragging && std::abs(dx) > 3.0) {
        m_dragging = true;
        if (m_hit.drag == Drag::Scratch) {
            m_model->scratchBegin();
        } else if (m_hit.drag == Drag::PhraseEdge || m_hit.drag == Drag::PhraseBody) {
            m_model->beginPhraseDrag();
            if (m_hit.drag == Drag::PhraseBody) {
                setCursor(Qt::ClosedHandCursor);
            }
        }
    }
    if (!m_dragging) {
        return;
    }
    switch (m_hit.drag) {
    case Drag::Scratch:
        // Grab the waveform: dragging right pulls earlier audio under the playhead.
        m_model->scratchTo(m_pressTime - dx / m_model->pxPerSec());
        break;
    case Drag::PhraseEdge:
        m_model->dragPhraseEdge(m_hit.index, m_hit.edge, timeAtX(event->position().x()));
        break;
    case Drag::PhraseBody:
        m_model->dragPhrase(m_hit.index, dx / m_model->pxPerSec());
        break;
    case Drag::LoopEdge:
        m_model->dragLoopEdge(m_hit.edge, timeAtX(event->position().x()));
        break;
    case Drag::None:
        break;
    }
}

void MixHouseGridEditorWave::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_model || !m_model->loaded()) {
        return;
    }
    const Hit hit = std::exchange(m_hit, Hit {});
    if (m_dragging) {
        m_dragging = false;
        if (hit.drag == Drag::Scratch) {
            m_model->scratchEnd();
        } else if (hit.drag == Drag::PhraseEdge || hit.drag == Drag::PhraseBody) {
            m_model->endPhraseDrag();
        }
        setCursor(Qt::ArrowCursor);
        return;
    }
    const double t = timeAtX(event->position().x());
    if (event->position().y() >= height() - m_ribbonHeight) {
        m_model->selectPhraseAt(t);
    } else {
        m_model->seek(t);
    }
}

void MixHouseGridEditorWave::wheelEvent(QWheelEvent* event)
{
    if (!m_model) {
        event->ignore();
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) > 1e-6) {
        m_model->zoomBy(std::pow(1.15, steps));
    }
    event->accept();
}
