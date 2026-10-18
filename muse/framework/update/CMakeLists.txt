# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

muse_create_module(muse_update ALIAS muse::update)

target_sources(muse_update PRIVATE
    updatemodule.cpp
    updatemodule.h
    updatetypes.h
    updateerrors.h
    iupdateconfiguration.h
    iupdaterequestparamsprovider.h
    iappupdatescenario.h
    iappupdateservice.h
    updatecommands.h

    internal/updateconfiguration.cpp
    internal/updateconfiguration.h
    internal/updateuiactions.cpp
    internal/updateuiactions.h
    internal/updateactioncontroller.cpp
    internal/updateactioncontroller.h
    internal/updatecommandsregister.cpp
    internal/updatecommandsregister.h
    internal/updatecommandsstate.cpp
    internal/updatecommandsstate.h
    internal/appupdatescenario.cpp
    internal/appupdatescenario.h
    internal/appupdateservice.cpp
    internal/appupdateservice.h
)

if (MUSE_QT_SUPPORT)
    target_link_libraries(muse_update PRIVATE Qt::Concurrent)
endif()

# GlideMix: on macOS, Sparkle does the updating when its framework is there
# (thirdparty-mac/sparkle, see buildscripts/packaging/MacOS/get_sparkle.sh).
if (APPLE AND GLIDEMIX_SPARKLE_DIR AND EXISTS "${GLIDEMIX_SPARKLE_DIR}/Sparkle.framework")
    target_sources(muse_update PRIVATE
        internal/platform/macos/sparkleupdater.h
        internal/platform/macos/sparkleupdater.mm
    )
    set_source_files_properties(internal/platform/macos/sparkleupdater.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc" SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON)
    target_compile_definitions(muse_update PUBLIC GLIDEMIX_SPARKLE)
    target_compile_options(muse_update PRIVATE "-F${GLIDEMIX_SPARKLE_DIR}")
    target_link_options(muse_update PUBLIC "-F${GLIDEMIX_SPARKLE_DIR}")
    target_link_libraries(muse_update PUBLIC "-framework Sparkle")
endif()

if (MUSE_MODULE_UPDATE_TESTS)
    add_subdirectory(tests)
endif()

if (MUSE_MODULE_UPDATE_QML)
    add_subdirectory(qml/Muse/Update)
endif()