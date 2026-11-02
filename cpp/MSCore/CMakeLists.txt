# MSCore native DLL (UE4SS C++ mod). Built inside the RE-UE4SS tree: build.ps1 copies this folder to cppmods\MSCore.
set(TARGET MSCore)

add_library(${TARGET} SHARED
    dllmain.cpp
)

target_include_directories(${TARGET} PRIVATE .)
target_link_libraries(${TARGET} PUBLIC UE4SS)
