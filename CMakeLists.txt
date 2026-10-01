cmake_minimum_required(VERSION 3.21)
project(smcr_editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(smcr_ogg STATIC
	thirdparty/libogg/src/framing.c
	thirdparty/libogg/src/bitwise.c
)
target_include_directories(smcr_ogg PUBLIC thirdparty/libogg/include)

add_library(smcr_vorbis STATIC
	thirdparty/libvorbis/lib/mdct.c
	thirdparty/libvorbis/lib/smallft.c
	thirdparty/libvorbis/lib/block.c
	thirdparty/libvorbis/lib/envelope.c
	thirdparty/libvorbis/lib/window.c
	thirdparty/libvorbis/lib/lsp.c
	thirdparty/libvorbis/lib/lpc.c
	thirdparty/libvorbis/lib/analysis.c
	thirdparty/libvorbis/lib/synthesis.c
	thirdparty/libvorbis/lib/psy.c
	thirdparty/libvorbis/lib/info.c
	thirdparty/libvorbis/lib/floor1.c
	thirdparty/libvorbis/lib/floor0.c
	thirdparty/libvorbis/lib/res0.c
	thirdparty/libvorbis/lib/mapping0.c
	thirdparty/libvorbis/lib/registry.c
	thirdparty/libvorbis/lib/codebook.c
	thirdparty/libvorbis/lib/sharedbook.c
	thirdparty/libvorbis/lib/lookup.c
	thirdparty/libvorbis/lib/bitrate.c
	thirdparty/libvorbis/lib/vorbisenc.c
)
target_include_directories(smcr_vorbis PUBLIC thirdparty/libvorbis/include)
target_include_directories(smcr_vorbis PRIVATE thirdparty/libvorbis/lib)
target_link_libraries(smcr_vorbis PUBLIC smcr_ogg)

add_executable(smcr_editor WIN32
	main.cpp
	mainwindow.cpp mainwindow.h
	modfile.cpp modfile.h
	ogg.cpp ogg.h
	convert.cpp convert.h
)

target_include_directories(smcr_editor PRIVATE thirdparty/minimp3)
target_link_libraries(smcr_editor PRIVATE Qt6::Widgets smcr_vorbis)

if(MINGW)
	target_link_options(smcr_editor PRIVATE -static-libgcc -static-libstdc++)
endif()

set_target_properties(smcr_editor PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}
)
