cmake_minimum_required(VERSION 3.20)
project(WinFspLinuxMegaDriver LANGUAGES C CXX RC)

# ==============================================================================
#                 СТРОГАЯ ПРОВЕРКА ЦЕЛЕВОЙ АРХИТЕКТУРЫ
# ==============================================================================
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Этот драйвер глубоко оптимизирован исключительно под архитектуру amd64 (x64)!")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
#       ГЛУБОКАЯ ОПТИМИЗАЦИЯ AMD64 И ПОЛНАЯ СТАТИЧЕСКАЯ ЛИНКОВКА (UCRT64)
# ==============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(
        -O3                  # Максимальный уровень оптимизации скорости выполнения
        -march=x86-64-v3     # Включает инструкции AVX, AVX2, BMI1, BMI2, FMA, SSE4.2 (обязательно для CRC32c)
        -ffast-math          # Агрессивное аппаратное ускорение математических вычислений
        -flto                # Link Time Optimization для монолитного инлайнинга функций внутри main.cpp
    )
    
    add_link_options(
        -static              # Исключает зависимости от любых динамических библиотек среды MSYS2
        -static-libgcc       # Статическая линковка рантайма GCC
        -static-libstdc++    # Статическая стандартная библиотека C++
        -flto                # Включение LTO на этапе связывания бинарника
    )
endif()

# ==============================================================================
#                     ПОИСК И ПОДКЛЮЧЕНИЕ WINFSP SDK
# ==============================================================================
# По умолчанию WinFSP устанавливается в Program Files (x86) на Windows x64
set(WINFSP_DIR "C:/Program Files (x86)/WinFsp")

if(NOT EXISTS "${WINFSP_DIR}")
    message(FATAL_ERROR "WinFSP SDK не найден по пути ${WINFSP_DIR}! Убедитесь, что SDK установлен.")
endif()

include_directories(
    "${WINFSP_DIR}/inc"
    "${WINFSP_DIR}/inc/fuse"
)

# Передаем прямой путь к x64-библиотеке WinFSP для корректной связки API вызовов в Clang/MinGW
set(WINFSP_LIB "${WINFSP_DIR}/lib/winfsp-x64.lib")

# ==============================================================================
#         ПОДКЛЮЧЕНИЕ СКАЧАННЫХ БИБЛИОТЕК СЖАТИЯ (LATEST RELEASES)
# ==============================================================================
# Исходные коды автоматически разворачиваются скриптом build_and_sign.sh в third_party/
# Флаг EXCLUDE_FROM_ALL исключает сборку лишних утилит и тестов вендоров из нашего Ninja-файла

# 1. ZSTD (Основной стандарт сжатия Btrfs / ZFS в Linux 6+)
add_subdirectory(third_party/zstd/build/cmake EXCLUDE_FROM_ALL)

# 2. LZ4 (Высокоскоростной стандарт ZFS / F2FS)
add_subdirectory(third_party/lz4/contrib/cmake_unofficial EXCLUDE_FROM_ALL)

# 3. ZLIB (Классический deflate/gzip)
add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)

# 4. LZO (Классический lzo1x для старых томов Btrfs)
add_subdirectory(third_party/lzo EXCLUDE_FROM_ALL)

# ==============================================================================
#               СБОРКА МОНОЛИТНОГО ИСПОЛНЯЕМОГО ФАЙЛА СЛУЖБЫ
# ==============================================================================
set(SOURCE_FILES
    src/main.cpp                 # Монолитная C++ реализация (FSD, WinFSP, Драйверы, Логгеры, Планировщик)
    src/resources/resources.rc   # Ресурсы Windows: Иконка, Манифест UAC Администратора, Версия, STRINGTABLE логов
)

add_executable(WinFspLinuxReaderService ${SOURCE_FILES})

# Жесткое связывание статических библиотек компрессии и системного сокета
target_link_libraries(WinFspLinuxReaderService
    PRIVATE
    ${WINFSP_LIB}                # Нативный SDK WinFSP
    libzstd_static              # Статическая ZSTD (с поддержкой AVX2)
    lz4_static                  # Статическая LZ4
    zlibstatic                  # Статическая Zlib
    lzo_static                  # Статическая LZO
    ws2_32                       # Системная библиотека Windows Sockets (необходима для ntohll в модуле ZFS)
)
