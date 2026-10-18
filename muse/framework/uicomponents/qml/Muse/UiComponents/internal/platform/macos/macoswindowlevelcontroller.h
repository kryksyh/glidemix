/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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
#pragma once

class QWindow;

namespace muse::uicomponents {
class MacOSWindowLevelController
{
public:
    static void setAlwaysAboveApp(QWindow* window);

    //! Like setAlwaysAboveApp, but the window also stays visible while another
    //! application is in front - for panels that have to be watchable while the
    //! user works elsewhere.
    static void setAlwaysOnTop(QWindow* window);
};
}
