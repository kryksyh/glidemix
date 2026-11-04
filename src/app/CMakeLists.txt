
###########################################
# Setup main application
###########################################

###########################################
# Common
###########################################
set(EXECUTABLE_NAME audacity)

###########################################
# Platform specific
###########################################
include(GetPlatformInfo)

if (OS_IS_WIN)
    set(APP_OUTPUT_NAME ${MUSE_APP_NAME})

    include(GetCompilerInfo)

    set(WINDOWS_VERSION_RC ${PROJECT_BINARY_DIR}/windows_version.rc)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/windows_version.rc.in
        ${WINDOWS_VERSION_RC}
        @ONLY)

    if (CC_IS_MSVC)
        # MSVC recognizes a *.rc file and will invoke the resource compiler to link it
        set(WINDOWS_ICONS_RC ${PROJECT_SOURCE_DIR}/share/icons/windows_icons.rc)
    elseif(CC_IS_MINGW)
        set(WINDOWS_ICONS_RC ${PROJECT_BINARY_DIR}/windows_icons_rc.o)
        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/windows_icons_rc.o
            COMMAND ${CMAKE_RC_COMPILER} -i windows_icons.rc -o ${PROJECT_BINARY_DIR}/windows_icons_rc.o
            DEPENDS ${PROJECT_SOURCE_DIR}/share/icons/windows_icons.rc
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/share/icons
            )
        set_source_files_properties(${PROJECT_BINARY_DIR}/windows_icons_rc.o PROPERTIES generated true)

        set(WINDOWS_VERSION_RC_OBJ ${PROJECT_BINARY_DIR}/windows_version_rc.o)
        add_custom_command(
            OUTPUT ${WINDOWS_VERSION_RC_OBJ}
            COMMAND ${CMAKE_RC_COMPILER} -i windows_version.rc -o ${WINDOWS_VERSION_RC_OBJ}
            DEPENDS ${WINDOWS_VERSION_RC}
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            )
        set_source_files_properties(${WINDOWS_VERSION_RC_OBJ} PROPERTIES generated true)
        set(WINDOWS_VERSION_RC ${WINDOWS_VERSION_RC_OBJ})
    endif()

elseif(OS_IS_LIN)

    # GlideMix: the program is "glidemix" (the CMake target keeps its
    # upstream name "audacity"; Audacity is Muse Group's trademark).
    set(APP_OUTPUT_NAME "glidemix${MUSE_APP_INSTALL_SUFFIX}")

    # $ORIGIN/../lib holds the bundled extdeps libraries (see require_dep).
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib;${QT_INSTALL_LIBS}")
    if (BUILD_SHARED_LIBS)
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${SHARED_LIBS_INSTALL_DESTINATION}")
    endif(BUILD_SHARED_LIBS)

elseif(OS_IS_MAC)
    set(EXECUTABLE_NAME audacity MACOSX_BUNDLE)
    set(MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    # GlideMix: its own executable, bundle folder, id and name, so it never
    # collides with an installed Audacity (same id made LaunchServices open the
    # wrong app). The bundle folder is MUSE_APP_BUNDLE_NAME / AU4_SHARE_NAME in
    # SetupConfigure.cmake; settings are keyed by the names set in main.cpp.
    set(MACOSX_BUNDLE_GUI_IDENTIFIER ${MUSE_APP_GUI_IDENTIFIER})
    set(MACOSX_BUNDLE_BUNDLE_NAME "GlideMix")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING ${MUSE_APP_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${MUSE_APP_VERSION})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${CMAKE_BUILD_NUMBER})
    # GlideMix: updater apps (Updatest, Latest, MacUpdater...) read the build
    # version and the Sparkle feed; both were empty, so they showed "no info".
    if (NOT MACOSX_BUNDLE_BUNDLE_VERSION)
        set(MACOSX_BUNDLE_BUNDLE_VERSION ${MUSE_APP_VERSION})
    endif()
    set(SPARKLE_APPCAST_URL "https://raw.githubusercontent.com/cristitm76/glidemix-releases/main/appcast.xml")
    set(SPARKLE_PUBLIC_ED_KEY "eRbh9MVX7hof5g1t/DCwUY/HbP3m0sXR/UTiAQYhfLo=")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 1999-2026. Published under the GNU General Public License version 3.")

    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${QT_INSTALL_LIBS};@loader_path/../Frameworks")

    message("MACOSX_BUNDLE_VERSION: ${MACOSX_BUNDLE_LONG_VERSION_STRING}.${MACOSX_BUNDLE_BUNDLE_VERSION}")
endif()

###########################################
# Dependency library declaration
###########################################

macro(add_to_link_if_exists lib)

# NOTE There may be cases:
# 1. The module is enabled and the target exists.
# 2. The module is disabled, but there is a stub, the target exists
# 3. The module is disabled and the target does not exist.
# This macro also adds qml parts to the link

    if (TARGET ${lib})
        list(APPEND LINK_LIB ${lib})

        if (TARGET ${lib}_qml)
            list(APPEND LINK_LIB ${lib}_qml)
        endif()
    endif()
endmacro()

set(LINK_LIB
    ${QT_LIBRARIES}
    muse::global
    muse::draw
)

# Remove unused direct Qt dependencies
list(REMOVE_ITEM LINK_LIB
    Qt::PrintSupport
    Qt::QuickWidgets
    Qt::Xml
)

add_to_link_if_exists(muse::accessibility)
add_to_link_if_exists(muse::actions)
add_to_link_if_exists(muse::rcommand)
add_to_link_if_exists(muse::audioplugins)
add_to_link_if_exists(muse::cloud_qml)
add_to_link_if_exists(muse::cloud)
add_to_link_if_exists(muse::diagnostics)
add_to_link_if_exists(muse::dockwindow)
add_to_link_if_exists(muse::extensions)
add_to_link_if_exists(muse::graphicaleffects_qml)
add_to_link_if_exists(muse::interactive)
add_to_link_if_exists(muse::languages)
add_to_link_if_exists(muse::learn)
add_to_link_if_exists(muse::legacytreeview_qml)
add_to_link_if_exists(muse::multiwindows)
add_to_link_if_exists(muse::muse::vst)
add_to_link_if_exists(muse::network)
add_to_link_if_exists(muse::shortcuts)
add_to_link_if_exists(muse::testflow)
add_to_link_if_exists(muse::toast)
add_to_link_if_exists(muse::tours)
add_to_link_if_exists(muse::update)
add_to_link_if_exists(muse::ui_dialogs_qml)
add_to_link_if_exists(muse::ui)
add_to_link_if_exists(muse::uicomponents)
add_to_link_if_exists(muse::workspace)

add_to_link_if_exists(appshell)
add_to_link_if_exists(audacity_extensions)
add_to_link_if_exists(au3audio)
add_to_link_if_exists(au3wrap)
add_to_link_if_exists(au3cloud)
add_to_link_if_exists(audio)
add_to_link_if_exists(context)
add_to_link_if_exists(mixhouse)
add_to_link_if_exists(broadcast)
add_to_link_if_exists(mixhouse_qml)
add_to_link_if_exists(effects_audio_unit)
add_to_link_if_exists(effects_base)
add_to_link_if_exists(effects_builtin)
add_to_link_if_exists(effects_builtin_collection)
add_to_link_if_exists(effects_extensions)
add_to_link_if_exists(effects_lv2)
add_to_link_if_exists(effects_nyquist)
add_to_link_if_exists(effects_vst)
add_to_link_if_exists(exporter)
add_to_link_if_exists(iex_labels)
add_to_link_if_exists(importer)
add_to_link_if_exists(playback)
add_to_link_if_exists(preferences)
add_to_link_if_exists(project)
add_to_link_if_exists(projectscene)
add_to_link_if_exists(record)
add_to_link_if_exists(spectrogram)
add_to_link_if_exists(usageinfo)
add_to_link_if_exists(automation)

set (AUDACITY_APPEND_SRC)

list(APPEND AUDACITY_APPEND_SRC backgroundprocess.h appearanceappicon.h)
if (OS_IS_MAC)
    list(APPEND AUDACITY_APPEND_SRC internal/platform/macos/backgroundprocess.mm)
    list(APPEND AUDACITY_APPEND_SRC internal/platform/macos/appearanceappicon.mm)
else()
    list(APPEND AUDACITY_APPEND_SRC internal/platform/stub/backgroundprocess.cpp)
    list(APPEND AUDACITY_APPEND_SRC internal/platform/stub/appearanceappicon.cpp)
endif()

if (OS_IS_WIN)
    list(APPEND AUDACITY_APPEND_SRC ${WINDOWS_ICONS_RC})
    list(APPEND AUDACITY_APPEND_SRC ${WINDOWS_VERSION_RC})
endif()

###########################################
# Resources
###########################################
qt_add_resources(APP_RCC_SOURCES app.qrc)

###########################################
# Executable declaration
###########################################

qt_add_executable(${EXECUTABLE_NAME}
    WIN32 MACOSX_BUNDLE
    ${APP_RCC_SOURCES}
    ${AUDACITY_APPEND_SRC}
    # GlideMix app icons. AppIcon.icns (dark) is the bundle icon that
    # MACOSX_BUNDLE_ICON_FILE names; the Dark/Light pair is what
    # installAppearanceDependentAppIcon() swaps into the Dock at runtime.
    resources/AppIcon.icns
    resources/AppIconDark.icns
    resources/AppIconLight.icns
    main.cpp
    appfactory.cpp
    appfactory.h
    guiapp.cpp
    guiapp.h
    pluginregistrationapp.cpp
    pluginregistrationapp.h
    commandlineparser.cpp
    commandlineparser.h
    winleaktracker.cpp
    winleaktracker.h
)

if(OS_IS_MAC)
    # Places the icon at Contents/Resources/AppIcon.icns, matching what
    # MACOSX_BUNDLE_ICON_FILE names above -- without this property CMake
    # would just try (and fail) to treat the .icns as a normal source file.
    set_source_files_properties(resources/AppIcon.icns resources/AppIconDark.icns resources/AppIconLight.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

###########################################
# Includes
###########################################

target_include_directories(audacity PRIVATE
    ${PROJECT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}

    ${PROJECT_SOURCE_DIR}/src

    ${MUSE_FRAMEWORK_PATH}
    ${MUSE_FRAMEWORK_PATH}/framework
    ${MUSE_FRAMEWORK_PATH}/framework/global
)


###########################################
# Setting target properties
###########################################

if (APP_OUTPUT_NAME)
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES OUTPUT_NAME ${APP_OUTPUT_NAME})
endif (APP_OUTPUT_NAME)

if (OS_IS_WIN)
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_INSTALL_PREFIX}/bin
        PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

if (MUSE_ENABLE_UNIT_TESTS_CODE_COVERAGE)
    set(COVERAGE_FLAGS -fprofile-arcs -ftest-coverage --coverage)
    set(COVERAGE_LINK_FLAGS --coverage)
    target_compile_options(audacity PRIVATE ${COVERAGE_FLAGS})
    target_link_options(audacity PRIVATE ${COVERAGE_LINK_FLAGS})
endif()

if (OS_IS_MAC)
    # macOS localizes its own interface - the native file pickers, standard
    # buttons and the application menu - according to the localizations the
    # bundle declares, not according to the Qt catalogues we load. Declare
    # every localization macOS itself provides, so its native UI always
    # follows the system language, independently of the app language.
    file(GLOB AU4_APPKIT_LPROJ_DIRS
         "/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/*.lproj")
    list(SORT AU4_APPKIT_LPROJ_DIRS)
    set(AU4_BUNDLE_LOCALIZATIONS "")
    foreach(lproj_dir ${AU4_APPKIT_LPROJ_DIRS})
        get_filename_component(AU4_LANGUAGE_CODE ${lproj_dir} NAME_WE)
        if (NOT AU4_LANGUAGE_CODE STREQUAL "Base")
            string(APPEND AU4_BUNDLE_LOCALIZATIONS "\t\t<string>${AU4_LANGUAGE_CODE}</string>\n")
        endif()
    endforeach()

    set_target_properties(audacity
        PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/buildscripts/packaging/MacOS/MacOSXBundleInfo.plist.in
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${MACOSX_BUNDLE_GUI_IDENTIFIER}
        # GlideMix.app/Contents/MacOS/GlideMix (must match MUSE_APP_BUNDLE_NAME /
        # AU4_SHARE_NAME in SetupConfigure.cmake).
        OUTPUT_NAME GlideMix
    )
    target_link_libraries(audacity PRIVATE "-framework Cocoa")
    # GlideMix: Sparkle (macOS updates) goes inside the app, next to Qt.
    if (GLIDEMIX_SPARKLE_DIR AND EXISTS "${GLIDEMIX_SPARKLE_DIR}/Sparkle.framework")
        add_custom_command(TARGET audacity POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:audacity>/Frameworks"
            COMMAND rsync -a --delete "${GLIDEMIX_SPARKLE_DIR}/Sparkle.framework" "$<TARGET_BUNDLE_CONTENT_DIR:audacity>/Frameworks/"
        )
        # The build tree's app finds it there too, not only the installed one.
        set_property(TARGET audacity APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    endif()

    set_source_files_properties(
        internal/platform/macos/backgroundprocess.mm
        internal/platform/macos/appearanceappicon.mm
        PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON
        SKIP_PRECOMPILE_HEADERS ON
    )

    # macOS ties a permission grant -- the Documents folder, the microphone --
    # to the app's code signing identity. An ad-hoc signature has no stable
    # identity, so every rebuild looks like a brand new app and every prompt
    # comes back, blocking startup until it is answered. Signing with a local
    # certificate instead keeps those grants across rebuilds. Without one we
    # sign ad-hoc as before, so a fresh clone still builds.
    set(AU4_MAC_SIGN_IDENTITY "Mixtape Studio Local Dev" CACHE STRING
        "codesign identity for local builds; \"-\" signs ad-hoc")

    if (NOT AU4_MAC_SIGN_IDENTITY STREQUAL "-")
        execute_process(
            COMMAND security find-certificate -c "${AU4_MAC_SIGN_IDENTITY}"
            RESULT_VARIABLE AU4_MAC_SIGN_IDENTITY_FOUND
            OUTPUT_QUIET ERROR_QUIET
        )
        if (NOT AU4_MAC_SIGN_IDENTITY_FOUND EQUAL 0)
            message(STATUS "No \"${AU4_MAC_SIGN_IDENTITY}\" certificate in the keychain; signing ad-hoc. "
                           "See buildscripts/tools/macos/make_signing_certificate.sh.")
            set(AU4_MAC_SIGN_IDENTITY "-")
        endif()
    endif()

    message(STATUS "macOS codesign identity: ${AU4_MAC_SIGN_IDENTITY}")

    add_custom_command(TARGET audacity POST_BUILD
        COMMAND codesign --force --sign "${AU4_MAC_SIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:audacity>"
        VERBATIM
    )
endif (OS_IS_MAC)

if (CC_IS_MINGW)
    if (CMAKE_BUILD_TYPE MATCHES "DEBUG")
        set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-mwindows -mconsole")
    else (CMAKE_BUILD_TYPE MATCHES "DEBUG")
        set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-Wl,-S -mwindows")
    endif (CMAKE_BUILD_TYPE MATCHES "DEBUG")
endif(CC_IS_MINGW)

if (CC_IS_MSVC)
    target_link_options(${EXECUTABLE_NAME} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
        "/ILK:${CMAKE_BINARY_DIR}/${APP_OUTPUT_NAME}-$<CONFIG>.ilk"
    )

    if (AU4_BUILD_MODE STREQUAL "release")
        target_link_options(${EXECUTABLE_NAME} PRIVATE
            /INCREMENTAL:NO
            /OPT:REF
            /OPT:ICF
        )
    endif()
endif(CC_IS_MSVC)

###########################################
# Link
##########################################
target_link_libraries(audacity
        PRIVATE
        ${LINK_LIB}
        ${COVERAGE_LINK_FLAGS}
)

###########################################
# Dependencies
###########################################

if (AU_MODULE_EFFECTS_NYQUIST)
    add_dependencies(audacity nyquist-runtime nyquist-plug-ins)
endif()

add_dependencies(audacity builtin-workspaces)

###########################################
# INSTALL
###########################################

###########################################
# Windows
###########################################
if (OS_IS_WIN)

    install(TARGETS ${EXECUTABLE_NAME} RUNTIME DESTINATION bin )

    include(GetCompilerInfo)

    if (CC_IS_MINGW)
        get_filename_component(COMPILER_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        get_filename_component (MINGW_ROOT ${COMPILER_DIR} DIRECTORY)

        install( FILES
               ${MINGW_ROOT}/bin/libgcc_s_seh-1.dll
               ${MINGW_ROOT}/bin/libstdc++-6.dll
               ${MINGW_ROOT}/bin/libwinpthread-1.dll
               DESTINATION bin)

    endif(CC_IS_MINGW)

    # Install Qt
    qt_generate_deploy_qml_app_script(
        TARGET audacity
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
        EXCLUDE_PLUGIN_TYPES qmltooling sqldrivers styles
        DEPLOY_TOOL_OPTIONS
            --plugindir bin
            --no-system-dxc-compiler
            --qmldir ${PROJECT_SOURCE_DIR}/src
            --qmldir ${PROJECT_SOURCE_DIR}/muse/framework
    )

    add_custom_command(
        TARGET audacity PRE_LINK
        COMMAND ${CMAKE_COMMAND} -DSCRIPT_PATH=${deploy_script} -P ${CMAKE_CURRENT_SOURCE_DIR}/add_NO_OVERWRITE_option.cmake
        COMMENT "Injecting NO_OVERWRITE into Qt deploy script"
    )

    install(SCRIPT ${deploy_script})

    # GlideMix: the music library is SQLite through Qt Sql. The deploy above
    # leaves SQL drivers out; ship this one (prune_qt_runtime keeps it).
    install(FILES $<TARGET_FILE:Qt6::QSQLiteDriverPlugin> DESTINATION bin/sqldrivers)

    install(SCRIPT
        "${PROJECT_SOURCE_DIR}/buildscripts/packaging/prune_qt_runtime.cmake"
    )

    if (WIN_PORTABLE)
        # deploy the files and directory structure needed for the PortableApps.com format
        install(DIRECTORY ${PROJECT_SOURCE_DIR}/build/PortableApps/App DESTINATION ${CMAKE_INSTALL_PREFIX}/../..)
        install(DIRECTORY ${PROJECT_SOURCE_DIR}/build/PortableApps/Other DESTINATION ${CMAKE_INSTALL_PREFIX}/../..)
        install(FILES ${PROJECT_SOURCE_DIR}/build/PortableApps/help.html DESTINATION ${CMAKE_INSTALL_PREFIX}/../..)
        configure_file(${PROJECT_SOURCE_DIR}/build/PortableApps/appinfo.ini.in     ${CMAKE_INSTALL_PREFIX}/../../App/AppInfo/appinfo.ini   @ONLY)
    endif (WIN_PORTABLE)

    if (MSVC)
        set_target_properties(${EXECUTABLE_NAME} PROPERTIES
            VS_DEBUGGER_COMMAND "${CMAKE_INSTALL_PREFIX}/bin/${APP_OUTPUT_NAME}.exe"
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin"
        )
    endif()

###########################################
# Linux
###########################################
elseif(OS_IS_LIN)

    install(TARGETS ${EXECUTABLE_NAME} RUNTIME DESTINATION bin)

elseif(OS_IS_FBSD)
###########################################
# FreeBSD
###########################################


###########################################
# MacOS
###########################################
elseif(OS_IS_MAC)

    install(TARGETS audacity BUNDLE DESTINATION ${CMAKE_INSTALL_PREFIX})

    if(MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT)
        install(PROGRAMS "${MUSE_MODULE_DIAGNOSTICS_CRASHPAD_HANDLER_PATH}"
            DESTINATION GlideMix.app/Contents/MacOS
        )
    endif()

    qt_generate_deploy_qml_app_script(
        TARGET audacity
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        # GlideMix: sqldrivers stay -- the music library is an SQLite database.
        EXCLUDE_PLUGIN_TYPES qmltooling styles
        DEPLOY_TOOL_OPTIONS
            -qmldir=${PROJECT_SOURCE_DIR}/src
            -qmldir=${PROJECT_SOURCE_DIR}/muse/framework
    )
    install(SCRIPT ${deploy_script})

    install(SCRIPT
        "${PROJECT_SOURCE_DIR}/buildscripts/packaging/prune_qt_runtime.cmake"
    )

###########################################
# Wasm
###########################################
elseif(OS_IS_WASM)

else()
    message(FATAL_ERROR "Unsupported Platform: ${CMAKE_HOST_SYSTEM_NAME}")
endif()


#################################################
# Miscellaneous Microsoft Visual Studio settings
#################################################
if (MSVC)

   # Force the "install" and "package" targets not to depend on the "all" target.
   set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)
   set(CMAKE_SKIP_PACKAGE_ALL_DEPENDENCY true)

endif (MSVC)
