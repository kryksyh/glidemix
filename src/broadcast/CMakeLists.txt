#
# GlideMix
#
declare_module(broadcast)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/broadcastmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/broadcastmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/ibroadcaster.h
    ${CMAKE_CURRENT_LIST_DIR}/ibroadcastconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/broadcaster.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/broadcaster.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/broadcastconfiguration.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/broadcastconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/icecastsource.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/icecastsource.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mp3encoder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mp3encoder.h
)

set(MODULE_LINK
    au3-mixtape-engine
    libmp3lame::libmp3lame
    Qt::Network
)

setup_module()
