
include_directories(${CMAKE_SOURCE_DIR}/include/common
                    ${CMAKE_SOURCE_DIR}/include/lang
                    ${CMAKE_SOURCE_DIR}/include/plugin_interface
                    ${CMAKE_SOURCE_DIR}/include/server
                    ${CMAKE_SOURCE_DIR}/common

                    ${CMAKE_SOURCE_DIR}/external_libraries/boost_sync/include

                    LangSource
                    LangPrimSource

                    ${CMAKE_SOURCE_DIR}/external_libraries/nova-tt
                    LangSource/Bison

                    ${YAMLCPP_INCLUDE_DIR}
)


if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	find_package(ALSA)
endif()

find_package(Readline 5.0)

find_package(Sndfile)

if (SC_QT OR SC_IDE)
	set(LOCALSOCK_UTIL_FILE ${CMAKE_SOURCE_DIR}/editors/sc-ide/primitives/localsocket_utils.cpp)
else ()
	set(LOCALSOCK_UTIL_FILE)
endif ()

set(sclang_sources
    LangPrimSource/SC_LinkClock.cpp
    LangPrimSource/SC_AudioDevicePrim.cpp
	LangPrimSource/PyrSignalPrim.cpp
	LangPrimSource/PyrSched.cpp
	LangPrimSource/PyrPrimitive.cpp
	LangPrimSource/PyrMathPrim.cpp
	LangPrimSource/SC_ComPort.cpp
	LangPrimSource/OSCData.cpp
	LangPrimSource/PyrArchiver.cpp
	LangPrimSource/PyrArrayPrimitives.cpp
	LangPrimSource/PyrBitPrim.cpp
	LangPrimSource/PyrCharPrim.cpp
	LangPrimSource/PyrFilePrim.cpp
	LangPrimSource/PyrListPrim.cpp
	LangPrimSource/PyrPlatformPrim.cpp
	LangPrimSource/PyrStringPrim.cpp
	LangPrimSource/PyrSymbolPrim.cpp
	LangPrimSource/PyrUnixPrim.cpp
	LangPrimSource/PyrSerialPrim.cpp
	LangSource/AdvancingAllocPool.cpp
	LangSource/ByteCodeArray.cpp
	LangSource/DumpParseNode.cpp
	LangSource/GC.cpp
	LangSource/InitAlloc.cpp
	LangSource/PyrInterpreter3.cpp
	LangSource/PyrLexer.cpp
	LangSource/PyrMathOps.cpp
	LangSource/PyrMathSupport.cpp
	LangSource/PyrMessage.cpp
	LangSource/PyrObject.cpp
	LangSource/PyrParseNode.cpp
	LangSource/PyrSignal.cpp
	LangSource/PyrSymbolTable.cpp
    LangSource/SC_CLIOptions.cpp
	LangSource/SC_LanguageClient.cpp
	LangSource/SC_LanguageConfig.cpp
	LangSource/SC_TerminalClient.cpp
	LangSource/SimpleStack.cpp
	LangSource/VMGlobals.cpp
	LangSource/dumpByteCodes.cpp

	${CMAKE_SOURCE_DIR}/common/SC_Filesystem_macos.cpp
	${CMAKE_SOURCE_DIR}/common/SC_Filesystem_win.cpp
	${CMAKE_SOURCE_DIR}/common/SC_Filesystem_unix.cpp
	${CMAKE_SOURCE_DIR}/common/SC_Filesystem_iphone.cpp

	${CMAKE_SOURCE_DIR}/common/fftlib.c
	${CMAKE_SOURCE_DIR}/common/Samp.cpp
	${CMAKE_SOURCE_DIR}/common/SC_AllocPool.cpp
	${CMAKE_SOURCE_DIR}/common/SC_Reply.cpp
	${CMAKE_SOURCE_DIR}/common/SC_StringBuffer.cpp
	${CMAKE_SOURCE_DIR}/common/SC_StringParser.cpp
	${CMAKE_SOURCE_DIR}/common/SC_TextUtils.cpp
	${LOCALSOCK_UTIL_FILE}

	${CMAKE_SOURCE_DIR}/common/sc_popen.cpp
)

if(APPLE)
	set_property(SOURCE ${CMAKE_SOURCE_DIR}/common/SC_Filesystem_macos.cpp PROPERTY COMPILE_FLAGS -xobjective-c++)

	list(APPEND sclang_sources ${CMAKE_SOURCE_DIR}/common/SC_Apple.mm)
	set_source_files_properties(${CMAKE_SOURCE_DIR}/common/SC_Apple.mm PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-exceptions")
endif()

file(GLOB_RECURSE headers ../include/*.h* )
file(GLOB_RECURSE external_headers ../external_libraries/*.h* )
list(APPEND sclang_sources ${headers} ${external_headers}) # make qt creator happy

set(sclang_parser_source LangSource/Bison/lang11d_tab.cpp)

# audio API for listing devices, selected the same way as in the server
if(AUDIOAPI STREQUAL "default")
    if(APPLE)
        set(AUDIOAPI coreaudio)
    elseif(WIN32)
        set(AUDIOAPI portaudio)
    else()
        set(AUDIOAPI jack)
    endif(APPLE)
endif()

if(NOT AUDIOAPI MATCHES "^(jack|coreaudio|portaudio|bela)$")
    message(FATAL_ERROR "Unrecognised audio API: ${AUDIOAPI}")
endif()

if (AUDIOAPI STREQUAL coreaudio)
    add_definitions("-DSC_AUDIO_API_COREAUDIO")
elseif (AUDIOAPI STREQUAL portaudio)
    if (SYSTEM_PORTAUDIO)
        find_package(Portaudio)
        if(NOT PORTAUDIO_FOUND)
            message(FATAL_ERROR "Portaudio selected as audio API, but development files not found")
        endif()
    endif()
    add_definitions("-DSC_AUDIO_API_PORTAUDIO")
    list(APPEND sclang_sources ${CMAKE_SOURCE_DIR}/common/SC_PaUtils.cpp)
    include_directories(${PORTAUDIO_INCLUDE_DIRS})
elseif(AUDIOAPI STREQUAL bela)
    add_definitions("-DSC_AUDIO_API_BELA")
endif()

if(UNIX)
	if(APPLE)
		list(APPEND sclang_sources
			LangPrimSource/SC_CoreMIDI.cpp
			)
	else(APPLE)
		if(ALSA_FOUND)
			list(APPEND sclang_sources LangPrimSource/SC_AlsaMIDI.cpp)
		endif()
		if(LINUX)
			list(APPEND sclang_sources LangPrimSource/SC_LID.cpp)
			add_definitions(-DHAVE_LID)
		endif(LINUX)
	endif(APPLE)
endif(UNIX)

if(WIN32)
        list(APPEND sclang_sources ${CMAKE_SOURCE_DIR}/common/SC_Win32Utils.cpp)
	include_directories (../platform/windows/compat_stuff)
        if(NOT MINGW)
                # mingw-w64 provides getopt
                list(APPEND sclang_sources ../platform/windows/compat_stuff/getopt/getopt.c)
                include_directories(../platform/windows/compat_stuff/getopt)
        endif()

        list(APPEND sclang_sources LangPrimSource/SC_PortMidi.cpp)
endif()

# This sets up the exe icon for windows.
if(WIN32)
 set(RES_FILES ${CMAKE_SOURCE_DIR}/platform/windows/Resources/sclang.rc)
 if(MINGW OR MSYS)
 set(CMAKE_RC_COMPILER_INIT windres)
 ENABLE_LANGUAGE(RC)
 SET(CMAKE_RC_COMPILE_OBJECT
 "<CMAKE_RC_COMPILER> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
 endif(MINGW OR MSYS)
endif(WIN32)

if(SC_HIDAPI)
    list(APPEND sclang_sources LangPrimSource/SC_HID_api.cpp)
    include_directories(
        ${CMAKE_SOURCE_DIR}/external_libraries/hidapi/hidapi
        ${CMAKE_SOURCE_DIR}/external_libraries/hidapi/hidapi_parser
    )
    add_definitions(-DSC_HIDAPI)
endif(SC_HIDAPI)

if(SC_QT OR SC_IDE)
	set(QT_COLLIDER_LANG_CLIENT ON)
	include(../QtCollider/CMakeLists.txt)
	list(APPEND sclang_sources ${QT_COLLIDER_SRCS})
endif()

if(SC_IDE)
	add_definitions(-DSC_IDE -DQT_NO_KEYWORDS)

	qt_wrap_cpp( SCLANG_MOC_SRCS ../editors/sc-ide/primitives/sc_ipc_client.hpp )
	list(APPEND sclang_sources ../editors/sc-ide/primitives/sc_ipc_client.cpp)
	list(APPEND sclang_sources ${SCLANG_MOC_SRCS})
endif()

include(../SCDoc/CMakeLists.txt)
list(APPEND sclang_sources ${SCDOC_SRCS})

if(0 AND FINAL_BUILD) # sclang final-builds are broken
	CREATE_FINAL_FILE(libsclang_final.cpp ${sclang_sources})
	add_library(libsclang STATIC libsclang_final.cpp ${sclang_parser_source})
else()
	add_library(libsclang STATIC ${sclang_sources} ${sclang_parser_source})
endif()

target_compile_definitions(libsclang PRIVATE YYSTACK_USE_ALLOC)
target_link_libraries(libsclang tlsf ${PTHREADS_LIBRARIES})

if(SC_QT OR SC_IDE)
    get_target_property(QtCore_location Qt${QT_VERSION_MAJOR}::Core LOCATION)
    message(STATUS "Found Qt ${QT_VERSION}: " ${QtCore_location} )
    get_filename_component(QT_BIN_PATH ${QtCore_location} DIRECTORY CACHE)

    if(SC_USE_QTWEBENGINE)
        message(STATUS "sclang: Building with QtWebEngine")
        target_compile_definitions(libsclang PUBLIC SC_USE_QTWEBENGINE)
    endif()
    target_link_libraries(libsclang ${QT_COLLIDER_LIBS})

    # This makes sclang/scide work with a Qt installation at a fixed location.
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        set_property(TARGET libsclang PROPERTY INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
    endif()
    set_property(TARGET libsclang PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

if(FALSE) # libsclang is a shared library
  target_compile_definitions(libsclang PRIVATE   BUILDING_SCLANG)
  target_compile_definitions(libsclang INTERFACE BUILDING_SCLANG)
endif()

if(SC_HIDAPI)
  target_compile_definitions(libsclang PRIVATE HAVE_HIDAPI)
  target_link_libraries( libsclang hidapi hidapi_parser )

  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND HID_HIDRAW)
    target_link_libraries( libsclang ${UDEV_LIBRARIES})
  endif()
  if(HID_LIBUSB)
    target_link_libraries( libsclang ${LIBUSB_1_LIBRARIES})
  endif()
endif()

target_link_libraries(libsclang boost_thread_lib boost_system_lib boost_regex_lib boost_program_options_lib)
target_include_directories(libsclang PUBLIC ${boost_include_dirs})

if (SCLANG_SERVER)
	target_link_libraries(libsclang libscsynth)
else()
	target_compile_definitions(libsclang PUBLIC NO_INTERNAL_SERVER)
endif()

if (NOT WIN32)
	set_property(TARGET libsclang
		PROPERTY OUTPUT_NAME sclang)
endif()

if (SC_ABLETON_LINK)
	message(STATUS "Compiling with Ableton Link support")
	if(SYSTEM_ABLETON_LINK)
		find_package(AbletonLink NAMES AbletonLink ableton-link link REQUIRED)
	else()
		include(../external_libraries/link/AbletonLinkConfig.cmake)
	endif()

	target_link_libraries(libsclang Ableton::Link)

	add_definitions(-DSC_ABLETON_LINK)
endif()

## external libraries
if(READLINE_FOUND)
    message(STATUS "Compiling with Readline support")
    # Some Linuxes require linking against ncurses as well as libreadline, because libtermcap provides
    # the same API and so could be used instead. Since ncurses is widely available and usually installed
    # already, we simply require it on all Linuxes. See #4899.
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(CURSES_NEED_NCURSES 1)
        find_package(Curses REQUIRED)
        target_link_libraries(libsclang ${CURSES_LIBRARIES})
    endif()
    target_compile_definitions(libsclang PUBLIC HAVE_READLINE)
    target_include_directories(libsclang PUBLIC ${READLINE_INCLUDE_DIR})
    target_link_libraries(libsclang ${READLINE_LIBRARY})
endif(READLINE_FOUND)
mark_as_advanced(READLINE_INCLUDE_DIR READLINE_LIBRARY)

if (APPLE)
	target_link_libraries(libsclang "-framework Carbon")
	target_link_libraries(libsclang "-framework CoreAudio")
	target_link_libraries(libsclang "-framework CoreMIDI")
	target_link_libraries(libsclang "-framework CoreServices")
	target_link_libraries(libsclang "-framework IOKit")
	target_link_libraries(libsclang "-framework CoreFoundation")
endif()

if(ALSA_FOUND)
	message(STATUS "Compiling with ALSA midi support")
	target_compile_definitions(libsclang PUBLIC HAVE_ALSA=1)
	target_link_libraries(libsclang ${ALSA_LIBRARY})
endif(ALSA_FOUND)

if (AUDIOAPI STREQUAL portaudio)
    target_link_libraries(libsclang ${PORTAUDIO_LIBRARIES})
endif()

if(SNDFILE_FOUND)
	if(SNDFILE_HAS_VORBIS)
		target_compile_definitions(libsclang PUBLIC SNDFILE_HAS_VORBIS)
	endif()
	if(SNDFILE_HAS_OPUS)
		target_compile_definitions(libsclang PUBLIC SNDFILE_HAS_OPUS)
	endif()
	if(SNDFILE_HAS_MPEG)
		target_compile_definitions(libsclang PUBLIC SNDFILE_HAS_MPEG)
	endif()
	target_include_directories(libsclang PUBLIC ${SNDFILE_INCLUDE_DIR})
	target_link_libraries(libsclang ${SNDFILE_LIBRARIES})
elseif(NOT NO_LIBSNDFILE)
	message(SEND_ERROR "Cannot find libsndfile")
endif(SNDFILE_FOUND)

if (FFTW3F_FOUND)
	target_include_directories (libsclang ${FFTW3F_INCLUDE_DIR})
	target_link_libraries(libsclang ${FFTW3F_LIBRARY})
endif()

if (WIN32 AND SC_HIDAPI)
	target_link_libraries(libsclang wsock32 ws2_32 portmidi iphlpapi hid)
elseif(WIN32)
	target_link_libraries(libsclang wsock32 ws2_32 portmidi iphlpapi)
endif()

if (GC_SANITYCHECK)
	target_compile_definitions(libsclang PUBLIC GC_SANITYCHECK)
endif()

if(MSVC)
    target_compile_definitions(libsclang PUBLIC HAVE_ISXDIGIT) # needed for MSVC 2022
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	target_link_libraries(libsclang rt)
endif()

target_link_libraries(libsclang ${YAMLCPP_LIBRARY})

add_executable(sclang LangSource/cmdLineFuncs.cpp ${RES_FILES})
target_link_libraries(sclang libsclang)
target_link_libraries(sclang ${ICU_LIBRARIES})

target_compile_definitions(sclang PUBLIC USE_SC_TERMINAL_CLIENT)

if(LTO)
  set_property(TARGET sclang libsclang
        APPEND PROPERTY COMPILE_FLAGS "-flto -flto-report")

  set_property(TARGET sclang
        APPEND PROPERTY LINK_FLAGS "-flto -flto-report -fwhole-program")

  set_property(TARGET libsclang
        APPEND PROPERTY LINK_FLAGS "-flto -flto-report")
endif()

if(APPLE)
    if(scappbindir)
        add_custom_command(TARGET sclang POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/${scappbindir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:sclang> ${CMAKE_INSTALL_PREFIX}/${scappbindir}
            )
    endif()
elseif(WIN32)
    if(NOT MSVC)
        set_target_properties(sclang PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<CONFIG>")
    endif(NOT MSVC)

    target_compile_definitions(sclang PUBLIC UNICODE _UNICODE)

    set(SC_WIN_DLL_DIRS)
    if(SNDFILE_LIBRARY_DIR)
      list(APPEND SC_WIN_DLL_DIRS "${SNDFILE_LIBRARY_DIR}")
    endif(SNDFILE_LIBRARY_DIR)
    if(FFTW3F_LIBRARY_DIR)
      list(APPEND SC_WIN_DLL_DIRS "${FFTW3F_LIBRARY_DIR}")
    endif(FFTW3F_LIBRARY_DIR)
    if(READLINE_LIBRARY_DIR)
      file(GLOB READLINE_DLL "${READLINE_LIBRARY_DIR}/*readline*.dll")
      list(APPEND SC_WIN_DLL_DIRS "${READLINE_LIBRARY_DIR}")
      add_custom_command(TARGET sclang
          POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${READLINE_DLL}" $<TARGET_FILE_DIR:sclang>
      )
    endif(READLINE_LIBRARY_DIR)
    if(QT_BIN_PATH)
      list(APPEND SC_WIN_DLL_DIRS "${QT_BIN_PATH}")
    endif(QT_BIN_PATH)

    if(SC_IDE)
      if(MSYS)
        add_custom_command(TARGET sclang
          POST_BUILD
          COMMAND "${CMAKE_SOURCE_DIR}/platform/windows/junctions.sh" "remove" "$<TARGET_FILE_DIR:sclang>"
          COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_FILE_DIR:sclang> $<TARGET_FILE_DIR:SuperCollider>
          COMMAND "${CMAKE_SOURCE_DIR}/platform/windows/junctions.sh" "create" "${CMAKE_SOURCE_DIR}" "$<TARGET_FILE_DIR:sclang>"
          COMMENT "Copying files in target sclang to target SuperCollider (scide) and creating links to SCClassLibrary e.a."
        )
      else()
        add_custom_command(TARGET sclang
          POST_BUILD
          COMMAND cmd /C "\"\"${CMAKE_SOURCE_DIR}/platform/windows/junctions.bat\" remove \"$<TARGET_FILE_DIR:sclang>\"\""
          COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_FILE_DIR:sclang> $<TARGET_FILE_DIR:SuperCollider>
          COMMAND cmd /C "\"\"${CMAKE_SOURCE_DIR}/platform/windows/junctions.bat\" create \"$<TARGET_FILE_DIR:sclang>\" \"${CMAKE_SOURCE_DIR}\"\""
          COMMENT "Copying files in target sclang to target SuperCollider (scide) and creating links to SCClassLibrary e.a."
        )
      endif()
    endif(SC_IDE)

    install(TARGETS sclang
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION "${SC_WIN_BUNDLE_NAME}"
    )

    if(SC_QT)
        SET(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_PREFIX}/${SC_WIN_BUNDLE_NAME}")
        include(InstallRequiredSystemLibraries)
        if(MSYS)
            SET(QT_PLUGINS_DIR "${QT_BIN_PATH}/../share/Qt${QT_VERSION_MAJOR}/plugins" CACHE PATH "Location of qt plugins for windows as provided by MSYS2") # note: not tested with Qt6
        else()
            SET(QT_PLUGINS_DIR "${QT_BIN_PATH}/../plugins" CACHE PATH "Location of qt plugins for windows")
        endif()

        foreach(plugin ${Qt${QT_VERSION_MAJOR}Network_PLUGINS} ${Qt${QT_VERSION_MAJOR}Gui_PLUGINS} ${Qt${QT_VERSION_MAJOR}Sql_PLUGINS} ${Qt${QT_VERSION_MAJOR}PrintSupport_PLUGINS})
            get_target_property(_loc ${plugin} LOCATION)
            get_filename_component(_parent_dir ${_loc} DIRECTORY)
            get_filename_component(_name_we ${_loc} NAME_WE)
            get_filename_component(_abs ${QT_PLUGINS_DIR} ABSOLUTE)
            string(REPLACE "${_abs}/" "" _dest_dir ${_parent_dir})
            install(FILES "${_parent_dir}/${_name_we}$<$<CONFIG:DEBUG>:d>.dll"
                DESTINATION "${CMAKE_INSTALL_PREFIX}/${SC_WIN_BUNDLE_NAME}/${_dest_dir}"
            )
        endforeach()
    endif(SC_QT)

    if(NOT SC_IDE)
        install(CODE "
            include(BundleUtilities)
            fixup_bundle(
                \"${CMAKE_INSTALL_PREFIX}/${SC_WIN_BUNDLE_NAME}/sclang.exe\"
                \"\"
                \"${SC_WIN_DLL_DIRS}\" )
            "
            COMMENT "Looking for libraries..."
            VERBATIM
        )
    endif(NOT SC_IDE)

else()
    install(TARGETS sclang
        RUNTIME DESTINATION "bin"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
