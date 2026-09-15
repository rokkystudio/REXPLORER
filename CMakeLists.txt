cmake_minimum_required(VERSION 3.20)
project(REXPLORER LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GUI-приложение (без консоли) + ресурс с иконкой
add_executable(REXPLORER WIN32
        main.cpp
        rexplorer.rc
)

# Юникод по умолчанию
target_compile_definitions(REXPLORER PRIVATE UNICODE _UNICODE)

if (MSVC)
    # ===== MSVC: один .exe без VC redist =====
    # /MT (Release) и /MTd (Debug) — статический рантайм
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(REXPLORER PRIVATE /W4 /utf-8)
    # Релиз поменьше
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(REXPLORER PRIVATE /OPT:REF /OPT:ICF)
    endif()

elseif (MINGW)
    # ===== MinGW: попытка полной статики =====
    target_compile_options(REXPLORER PRIVATE -Wall -Wextra -Wpedantic)

    # Включаем статическую линковку стандартных либ
    # (если статических .a нет в твоём MinGW, линкер возьмёт DLL — это ограничение тулчейна)
    target_link_options(REXPLORER PRIVATE
            -static                 # стараться линковать статически всё, что возможно
            -static-libstdc++       # статически stdc++
            -static-libgcc          # статически libgcc
            -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic  # статический winpthread, если доступен
            -Wl,--gc-sections
            -s                      # вырезать символы
    )

    # (Не обязательно) можно добавить -municode, если уберёшь WinMain-обёртку
    # target_link_options(REXPLORER PRIVATE -municode)

else()
    message(WARNING "Неподдержанный тулчейн: используйте MSVC или MinGW.")
endif()
