# MixHouse DJ 2 -- QML module for the library panel.

muse_create_qml_module(mixhouse_qml FOR mixhouse)

qt_add_qml_module(mixhouse_qml
    URI Audacity.MixHouse
    VERSION 1.0
    SOURCES
        mixhousekeywheel.cpp
        mixhousekeywheel.h
        mixhousetracklistmodel.cpp
        mixhousetracklistmodel.h
        mixhousetempolane.cpp
        mixhousetempolane.h
        mixhousetempolanemodel.cpp
        mixhousetempolanemodel.h
        mixhousetransitionzonesmodel.cpp
        mixhousetransitionzonesmodel.h
        mixhouseprojectlistmodel.cpp
        mixhouseprojectlistmodel.h
        mixhousegrideditormodel.cpp
        mixhousegrideditormodel.h
        mixhousegrideditorview.cpp
        mixhousegrideditorview.h
        mixhousegrideditorwave.cpp
        mixhousegrideditorwave.h
        mixhouseperformancemodel.cpp
        mixhouseperformancemodel.h
        mixhouselicencemodel.cpp
        mixhouselicencemodel.h
        mixhousestatusmodel.cpp
        mixhousestatusmodel.h
    QML_FILES
        MixHouseLibraryPanel.qml
        MixHouseProjectPanel.qml
        MixHouseGridEditorPanel.qml
        SmartPlaylistEditor.qml
        MixHouseStatusLine.qml
    RESOURCES
        icons/ic_broadcast.svg
        icons/ic_library_tracks.svg
        icons/ic_library_autodj.svg
        icons/ic_library_playlist.svg
        icons/ic_library_smart.svg
        icons/ic_library_crates.svg
        icons/ic_library_computer.svg
        icons/ic_library_recordings.svg
        icons/ic_library_history.svg
        icons/ic_library_history_current.svg
        icons/ic_library_folder.svg
        icons/ic_library_prepare.svg
        icons/ic_library_rekordbox.svg
        icons/ic_library_lexicon.png
)

# The Beatgrid editor's deck (au3-mixtape-engine) and the playback device
# setting; au3 headers stay out of the unity build.
target_link_libraries(mixhouse_qml PRIVATE au3-mixtape-engine)
set_source_files_properties(mixhousegrideditormodel.cpp mixhousegrideditorwave.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
