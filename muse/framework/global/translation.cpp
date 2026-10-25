/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include "translation.h"

#ifndef NO_QT_SUPPORT
#include <QCoreApplication>
#include <QString>
#endif

#include "types/bytearray.h"
#include "types/string.h"

#include "log.h"

using namespace muse;

#ifndef NO_QT_SUPPORT
namespace {
//! GlideMix: without a translation, Qt leaves English plurals as "song(s)";
//! with a count, make it "1 song" / "2 songs".
QString englishPlural(QString text, int n)
{
    if (n >= 0 && text.contains(QLatin1String("(s)"))) {
        text.replace(QLatin1String("(s)"), n == 1 ? QString() : QStringLiteral("s"));
    }
    return text;
}
}

#endif

static String translate(const char* context, const char* key, const char* disambiguation, int n)
{
#ifndef NO_QT_SUPPORT
    return String::fromQString(englishPlural(QCoreApplication::translate(context, key, disambiguation, n), n));
#else
    UNUSED(context);
    UNUSED(disambiguation);
    UNUSED(n);
    return String::fromUtf8(key);
#endif
}

std::string muse::trc(const char* context, const char* key, const char* disambiguation, int n)
{
    return translate(context, key, disambiguation, n).toStdString();
}

#ifndef NO_QT_SUPPORT
QString muse::qtrc(const char* context, const char* key, const char* disambiguation, int n)
{
    return englishPlural(QCoreApplication::translate(context, key, disambiguation, n), n);
}

QString muse::qtrc(const char* context, const String& key, const char* disambiguation, int n)
{
    ByteArray utf8 = key.toUtf8();
    return englishPlural(QCoreApplication::translate(context, utf8.constChar(), disambiguation, n), n);
}

QString muse::qtrc(const char* context, const String& key, const String& disambiguation, int n)
{
    ByteArray keyutf8 = key.toUtf8();
    ByteArray disutf8 = disambiguation.toUtf8();
    return englishPlural(QCoreApplication::translate(context, keyutf8.constChar(), disutf8.empty() ? nullptr : disutf8.constChar(), n), n);
}

#endif

String muse::mtrc(const char* context, const char* key, const char* disambiguation, int n)
{
    return translate(context, key, disambiguation, n);
}

String muse::mtrc(const char* context, const String& key, const char* disambiguation, int n)
{
    ByteArray keyutf8 = key.toUtf8();
    return translate(context, keyutf8.constChar(), disambiguation, n);
}

String muse::mtrc(const char* context, const String& key, const String& disambiguation, int n)
{
    ByteArray keyutf8 = key.toUtf8();
    ByteArray disutf8 = disambiguation.toUtf8();
    return translate(context, keyutf8.constChar(), disutf8.empty() ? nullptr : disutf8.constChar(), n);
}
