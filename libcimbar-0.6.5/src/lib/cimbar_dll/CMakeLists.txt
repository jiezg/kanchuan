cmake_minimum_required(VERSION 3.10)
project(cimbar_dll)

set(SOURCES
	cimbar_dll.h
	cimbar_dll.cpp
)

add_library(cimbar_dll SHARED ${SOURCES})

target_compile_definitions(cimbar_dll PRIVATE CIMBAR_DLL_EXPORTS)

if(OPENCV_STATIC)
	target_link_libraries(cimbar_dll
		cimb_translator
		extractor
		correct_static
		wirehair
		zstd
		${OPENCV_LIBS}
		-lws2_32 -liphlpapi -lsetupapi -lole32 -luuid -lcomctl32
		-lmfplat -lmf -lmfreadwrite -lmfuuid -loleaut32
	)
else()
	target_link_libraries(cimbar_dll
		cimb_translator
		extractor
		correct_static
		wirehair
		zstd
		${OPENCV_LIBS}
		-lmfplat -lmf -lmfreadwrite -lmfuuid -lole32 -loleaut32
	)
endif()
