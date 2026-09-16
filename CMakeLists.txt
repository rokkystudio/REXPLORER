cmake_minimum_required(VERSION 3.20)
project(REXPLORER LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(REXPLORER WIN32
        main.cpp
        rexplorer.rc
)

target_compile_definitions(REXPLORER PRIVATE UNICODE _UNICODE)

if (MSVC)
    set_property(TARGET REXPLORER PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(REXPLORER PRIVATE /W4 /utf-8)

    target_link_options(REXPLORER PRIVATE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
    )

elseif (MINGW)
    target_compile_options(REXPLORER PRIVATE -Wall -Wextra -Wpedantic)

    target_link_options(REXPLORER PRIVATE
            -static
            -static-libstdc++
            -static-libgcc
            -Wl,--gc-sections
            -s
    )

else()
    message(WARNING "Неподдержанный тулчейн: используйте MSVC или MinGW.")
endif()
