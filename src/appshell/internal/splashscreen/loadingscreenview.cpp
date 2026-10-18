/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "loadingscreenview.h"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

#include "translation.h"

using namespace au::appshell;
using namespace muse;

static const QString imagePath(":/resources/LoadingScreen.svg");
static const QString artworkPath(":/resources/LoadingScreenArt.jpg");

static constexpr QSize loadingScreenSize(800, 380);

static const QColor messageColor("#99FFFFFF");
static constexpr QRectF messageRect(loadingScreenSize.width() / 2, 269, 0, 0);

static const QString appName("GlideMix");
static constexpr QRectF appNameRect(38, 190, 0, 0);
static constexpr int appNamePixelSize = 44;
static constexpr QRectF versionRect(38, 322, 0, 0);


LoadingScreenView::LoadingScreenView(const muse::modularity::ContextPtr& ctx, QWidget* parent)
    : QWidget(parent), muse::Contextable(ctx),
    m_backgroundRenderer(new QSvgRenderer(imagePath, this)), m_artwork(artworkPath)
{
    setAttribute(Qt::WA_TranslucentBackground);
    resize(loadingScreenSize);

    m_message = qtrc("appshell", "Loading…\u200e");
}

bool LoadingScreenView::event(QEvent* event)
{
    if (event->type() == QEvent::Paint) {
        QPainter painter(this);
        painter.setLayoutDirection(layoutDirection());
        draw(&painter);
    }

    return QWidget::event(event);
}

void LoadingScreenView::draw(QPainter* painter)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

    // Draw background
    m_backgroundRenderer->render(painter);

    // GlideMix: the turntable artwork, right of centre, fading into the dark
    // on the left where the name and version are written.
    if (!m_artwork.isNull()) {
        const qreal artHeight = loadingScreenSize.height() + 40;
        const qreal artWidth = artHeight * m_artwork.width() / m_artwork.height();
        const QRectF artRect(loadingScreenSize.width() * 0.68 - artWidth / 2, -20, artWidth, artHeight);
        painter->drawImage(artRect, m_artwork);

        const QColor dark("#0B0B10");
        QLinearGradient fade(artRect.left(), 0, artRect.left() + 220, 0);
        fade.setColorAt(0.0, dark);
        fade.setColorAt(1.0, QColor(11, 11, 16, 0));
        painter->fillRect(QRectF(0, 0, artRect.left(), loadingScreenSize.height()), dark);
        painter->fillRect(QRectF(artRect.left(), 0, 220, loadingScreenSize.height()), fade);
    }

    // Draw message
    QFont font(QString::fromStdString(uiConfiguration()->fontFamily()));
    font.setPixelSize(uiConfiguration()->fontSize());

    painter->setFont(font);

    QPen pen(messageColor);
    painter->setPen(pen);

    // painter->drawText(messageRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextDontClip, m_message);

    //! TODO AU4
    Qt::AlignmentFlag alignment = Qt::AlignLeft;
    // Qt::AlignmentFlag alignment = languagesService()->currentLanguage().direction == Qt::RightToLeft
    //                               ? Qt::AlignLeft : Qt::AlignRight;

    // Draw app name (no logo until the app icon is final)
    QFont nameFont(font);
    nameFont.setPixelSize(appNamePixelSize);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    pen.setColor(Qt::white);
    painter->setPen(pen);
    painter->drawText(appNameRect, Qt::AlignBottom | alignment | Qt::TextDontClip, appName);
    painter->setFont(font);

    // Draw version number
    pen.setColor(uiConfiguration()->currentTheme().extra["logo_main_color"].value<QColor>());
    painter->setPen(pen);

    painter->drawText(versionRect, Qt::AlignBottom | alignment | Qt::TextDontClip,
                      qtrc("appshell", "Beta v%1.%2.%3").arg(application()->version().major())
                      .arg(application()->version().minor()).arg(application()->version().patch()));
}
