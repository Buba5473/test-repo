#!/usr/bin/env bash
set -e

# ============================================================================
# 1. ОПРЕДЕЛЕНИЕ ПЛАТФОРМЫ И ПЕРЕМЕННЫХ СБОРКИ
# ============================================================================
TARGET="windows"
CLEAN_FIRST=false

for arg in "$@"; do
    if [[ "$arg" == "--clean" ]]; then CLEAN_FIRST=true; fi
done
if [[ "$1" == "--target" && -n "$2" ]]; then TARGET="$2"; fi

WORKSPACE_DIR="pdfmrc_workspace"
MUPDF_DIR="$WORKSPACE_DIR/mupdf"
CACHE_FILE="$WORKSPACE_DIR/.mupdf_tags_cache"

# Блок тотальной очистки рабочего пространства
if [ "$CLEAN_FIRST" = true ]; then
    echo "======================================================================"
    echo "==> [pdfmrc] Запрошена полная деинсталляция/очистка среды сборки..."
    echo "======================================================================"
    rm -rf "$WORKSPACE_DIR"
    rm -f pdfmrc.exe pdfmrc cert.pfx cert.key cert.crt
    echo "==> Временные папки, кэши и готовые бинарники успешно удалены."
    exit 0
fi

echo "======================================================================"
echo " pdfmrc — Автономный конвейер сборки через Curl (Target: $TARGET)"
echo "======================================================================"

# ============================================================================
# 2. ИНТЕРАКТИВНАЯ ПРОВЕРКА И УСТАНОВКА ЗАВИСИМОСТЕЙ (БЕЗ ARIA2 / БЕЗ GIT)
# ============================================================================
REQUIRED_PKGS=("make" "curl" "jq" "g++" "openssl" "tar")
MISSING_PKGS=()

for cmd in "${REQUIRED_PKGS[@]}"; do
    if ! command -v "$cmd" &> /dev/null; then
        if [[ "$cmd" == "g++" ]]; then MISSING_PKGS+=("mingw-w64-ucrt-x86_64-gcc")
        elif [[ "$cmd" == "make" ]]; then MISSING_PKGS+=("mingw-w64-ucrt-x86_64-make")
        elif [[ "$cmd" == "tar" ]]; then MISSING_PKGS+=("tar")
        else MISSING_PKGS+=("$cmd"); fi
    fi
done

if [ ${#MISSING_PKGS[@]} -ne 0 ]; then
    echo "⚠️ Обнаружены отсутствующие пакеты: ${MISSING_PKGS[*]}"
    read -p "Хотите автоматически установить их прямо сейчас? (y/n): " confirm
    if [[ "$confirm" == [yY] || "$confirm" == [yY][eE][sS] ]]; then
        if command -v pacman &> /dev/null; then
            echo "==> Установка пакетов через pacman..."
            pacman -S --noconfirm "${MISSING_PKGS[@]}"
        elif command -v apt-get &> /dev/null; then
            echo "==> Установка пакетов через apt-get..."
            sudo apt-get update && sudo apt-get install -y build-essential curl jq openssl libssl-dev tar
        else
            echo "Ошибка: Менеджер пакетов не определен. Установите вручную: ${MISSING_PKGS[*]}"
            exit 1
        fi
    else
        echo "Сборка прервана пользователем из-за отсутствия компонентов."
        exit 1
    fi
fi

if [ ! -f "icon.ico" ]; then touch icon.ico icon.png; fi

# ============================================================================
# 3. ИНИЦИАЛИЗАЦИЯ РАБОЧЕГО ПРОСТРАНСТВА И ВАЛИДАЦИЯ КЭША ПЛАТФОРМ
# ============================================================================
mkdir -p "$WORKSPACE_DIR"
if [ -f "main.cpp" ]; then cp "main.cpp" "$WORKSPACE_DIR/main.cpp"; fi

CACHE_MARKER="$WORKSPACE_DIR/.last_target_marker"
if [ -f "$CACHE_MARKER" ] && [ "$(cat "$CACHE_MARKER")" != "$TARGET" ]; then
    echo "==> 🔄 Смена целевой платформы! Очистка старых объектных файлов во избежание конфликтов..."
    if [ -d "$MUPDF_DIR" ]; then
        cd "$MUPDF_DIR" && make clean || true && cd ../..
    fi
fi
echo "$TARGET" > "$CACHE_MARKER"

# ============================================================================
# 4. СКАЧИВАНИЕ ОФИЦИАЛЬНОГО СТАБИЛЬНОГО СТРОГО ЦИФРОВОГО РЕЛИЗА ИЗ АРХИВА ARTIFEX
# ============================================================================
if [ ! -d "$MUPDF_DIR" ]; then
    echo "==> Запрос информации об актуальном стабильном цифровом релизе..."
    
    LATEST_TAG=$(curl -s "https://api.github.com/repos/ArtifexSoftware/mupdf/tags" | \
                 jq -r '.[].tag_name' 2>/dev/null | \
                 grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | \
                 sort -V | \
                 tail -1)
                 
    if [ -z "$LATEST_TAG" ] || [ "$LATEST_TAG" == "null" ]; then 
        LATEST_TAG="1.27.2" 
    fi
    
    echo "$LATEST_TAG" > "$CACHE_FILE"
    echo "==> Фильтр пройден. Выбран последний цифровой релиз: $LATEST_TAG"
    echo "==> Загрузка Source Tarball напрямую с mupdf.com через Curl..."
    
    # ИСПРАВЛЕНИЕ: Жесткое кавыкание путей во избежание сбоев парсера curl и ворнингов терминала
    curl -fL -# "https://www.mupdf.com/downloads/archive/mupdf-${LATEST_TAG}-source.tar.gz" -o "${WORKSPACE_DIR}/mupdf.tar.gz"
    
    echo "==> Отказоустойчивая распаковка исходного кода (игнорирование симлинков NTFS)..."
    # Заключение команды в скобки с оператором "; true" гарантирует, что 
    # некритичные ошибки текстовых симлинков документации не сломают конвейер bash
    (tar -xzf "${WORKSPACE_DIR}/mupdf.tar.gz" -C "${WORKSPACE_DIR}" --warning=no-unknown-keyword || true)
    
    # Синхронизация папок с явным указанием суффикса -source
    if [ -d "${WORKSPACE_DIR}/mupdf-${LATEST_TAG}-source" ]; then
        mv "${WORKSPACE_DIR}/mupdf-${LATEST_TAG}-source" "$MUPDF_DIR"
    elif [ -d "${WORKSPACE_DIR}/mupdf-${LATEST_TAG}" ]; then
        mv "${WORKSPACE_DIR}/mupdf-${LATEST_TAG}" "$MUPDF_DIR"
    else
        echo "Критическая ошибка: Распакованная директория MuPDF не найдена."
        exit 1
    fi
    
    rm -f "${WORKSPACE_DIR}/mupdf.tar.gz"
    
    # "Потрошение" — удаление ненужной документации и азиатских шрифтов
    echo "==> Глубокая оптимизация: удаление нецелевых компонентов (docs, шрифты CJK)..."
    rm -rf "$MUPDF_DIR/docs" "$MUPDF_DIR/resources/fonts/cjk"
fi

# ============================================================================
# 5. ДИФФЕРЕНЦИАЦИЯ СИСТЕМНЫХ ФЛАГОВ И ЗАВИСИМОСТЕЙ ПОД ОС
# ============================================================================
# Добавлен макрос -D__USE_MINGW_ANSI_STDIO=1 для исправления предупреждений формата %zu под MinGW/MSYS2
COMMON_OPTIMIZATIONS="-O3 -march=x86-64-v3 -mtune=generic -flto -ffunction-sections -fdata-sections -DNDEBUG -D__USE_MINGW_ANSI_STDIO=1"

if [ "$TARGET" == "windows" ]; then
    export CFLAGS="$COMMON_OPTIMIZATIONS -D_WIN32_WINNT=0x0A00"
    export CXXFLAGS="$COMMON_OPTIMIZATIONS -D_WIN32_WINNT=0x0A00"
    
    windres resources.rc -O coff -o resources.res
    EXTRA_ASSETS="resources.res"
    SYSTEM_LIBS="-lgdi32 -lcomctl32 -lole32 -luuid -lshlwapi"
    LINK_FLAGS="-static -static-libgcc -static-libstdc++"
    BIN_NAME="pdfmrc_unsigned.exe"
    MAKE_CMD="make OS=MINGW"
else
    export CFLAGS="$COMMON_OPTIMIZATIONS"
    export CXXFLAGS="$COMMON_OPTIMIZATIONS"
    
    ld -r -b binary -o icon.o icon.png 2>/dev/null || touch icon.o
    EXTRA_ASSETS="icon.o"
    SYSTEM_LIBS="-lpthread -ldl -lm"
    LINK_FLAGS="-static-libgcc -static-libstdc++"
    BIN_NAME="pdfmrc"
    MAKE_CMD="make"
fi

# ============================================================================
# 6. КОМПИЛЯЦИЯ СТАТИЧЕСКИХ БИБЛИОТЕК ОФИЦИАЛЬНЫМ GNU MAKE
# ============================================================================
echo "==> Сборка официальных статических модулей ядра MuPDF $(cat $CACHE_FILE) через GNU Make..."
cd "$MUPDF_DIR"

# Отключаем сборку сопутствующих утилит и тяжелых форматов (XPS, EPUB, FB2)
$MAKE_CMD libs -j$(nthread 2>/dev/null || nproc 2>/dev/null || echo 4) \
          HAVE_XPS=no HAVE_EPUB=no HAVE_GLUT=no HAVE_X11=no

cd ../..

# ============================================================================
# 7. ФИНАЛЬНАЯ МОНОЛИТНАЯ ЛИНКОВКА И СТРИППИНГ
# ============================================================================
echo "==> Линковка и сквозная Link-Time оптимизация автономного бинарника..."
g++ -std=c++17 $LINK_FLAGS $CFLAGS \
    main.cpp $EXTRA_ASSETS \
    -I"$MUPDF_DIR/include" \
    -L"$MUPDF_DIR/build/release" -lmupdf -lmupdf-third \
    $SYSTEM_LIBS \
    -o "../$BIN_NAME"

cd ..
strip "$BIN_NAME"
rm -f "$WORKSPACE_DIR/resources.res" "$WORKSPACE_DIR/icon.o"

# ============================================================================
# 8. ГЕНЕРАЦИЯ ЦИФРОВОЙ ПОДПИСИ AUTHENTICODE (ТОЛЬКО WINDOWS)
# ============================================================================
if [ "$TARGET" == "windows" ]; then
    echo "==> Выпуск самоподписанного сертификата и наложение подписи..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout cert.key -out cert.crt -subj "/CN=PDFMRC Trusted Code Signer/O=Fake Code Sign LLC/C=RU" 2>/dev/null
    openssl pkcs12 -export -out cert.pfx -inkey cert.key -in cert.crt -password pass:1234 2>/dev/null

    if command -v osslsigncode &> /dev/null; then
        osslsigncode sign -pkcs12 cert.pfx -pass 1234 -in pdfmrc_unsigned.exe -out pdfmrc.exe 2>/dev/null
        rm pdfmrc_unsigned.exe
    else
        mv pdfmrc_unsigned.exe pdfmrc.exe
    fi
    rm -f cert.pfx cert.key cert.crt
fi

echo "======================================================================"
echo "==> [pdfmrc] Сборка успешно завершена! Исполняемый файл готов: ./$BIN_NAME"
echo "======================================================================"
