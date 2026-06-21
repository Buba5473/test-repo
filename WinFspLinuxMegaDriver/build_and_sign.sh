#!/bin/bash
set -e # Немедленно прерывать выполнение при любой ошибке

# ==============================================================================
#                 ВЫДЕЛЕННЫЙ БЛОК: ОБЩИЙ КОД, НАСТРОЙКИ И ЗАВИСИМОСТИ
# ==============================================================================
REQUIRED_PACKAGES=(
    "mingw-w64-ucrt-x86_64-clang"
    "mingw-w64-ucrt-x86_64-ninja"
    "mingw-w64-ucrt-x86_64-cmake"
    "mingw-w64-ucrt-x86_64-binutils"
    "mingw-w64-ucrt-x86_64-osslsigncode"
    "curl"
    "tar"
)

# Ассоциативный массив репозиториев GitHub (Имя локальной папки -> Путь репозитория вендора)
declare -A GITHUB_REPOS=(
    ["zstd"]="facebook/zstd"
    ["lz4"]="lz4/lz4"
    ["zlib"]="madler/zlib"
    ["lzo"]="nemequ/lzo"
)

BUILD_DIR="build_artifacts"
TARGET_NAME="WinFspLinuxReaderService"
CERT_FILE="fake_cert.pfx"
CERT_CER="fake_cert.cer"
CERT_PASS="1234"

# ==============================================================================
#                      ФУНКЦИИ КОНВЕЙЕРА АВТОМАТИЗАЦИИ
# ==============================================================================

# 1. Интерактивная проверка и установка необходимых системных пакетов MSYS2
check_sys_dependencies() {
    echo "=== 1. Проверка системных пакетов в MSYS2 UCRT64 ==="
    MISSING_PACKAGES=()

    for pkg in "${REQUIRED_PACKAGES[@]}"; do
        if ! pacman -Q "$pkg" &>/dev/null; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
        echo "[-] Обнаружены недостающие компоненты для сборки проекта:"
        for pkg in "${MISSING_PACKAGES[@]}"; do
            echo "    * $pkg"
        done
        echo ""
        read -p "Хотите автоматически скачать и установить пакеты через pacman? (y/n): " choice
        case "$choice" in 
            y|Y ) 
                echo "[+] Скачивание и установка системных пакетов..."
                pacman -S --noconfirm "${MISSING_PACKAGES[@]}"
                ;;
            * ) 
                echo "[ОШИБКА] Сборка прервана. Невозможно скомпилировать проект без системных утилит."
                exit 1
                ;;
        esac
    else
        echo "[+] Все необходимые системные пакеты pacman уже установлены в системе."
    fi
}

# 2. Автоматический опрос GitHub API и скачивание Latest Release (стабильного исходного кода)
download_latest_releases() {
    echo "=== 2. Проверка и автоматическое скачивание Latest Releases с GitHub ==="
    mkdir -p third_party

    for folder in "${!GITHUB_REPOS[@]}"; do
        target_path="third_party/$folder"
        
        # Проверяем, пуста ли директория зависимости
        if [ ! -d "$target_path" ] || [ -z "$(ls -A "$target_path")" ]; then
            echo "[-] Исходный код декомпрессора '$folder' отсутствует."
            repo_path="${GITHUB_REPOS[$folder]}"
            api_url="https://github.com{repo_path}/releases/latest"
            
            echo "[+] Запрос к GitHub REST API для получения тега последнего релиза ${repo_path}..."
            
            # Извлекаем URL-адрес архива последнего стабильного релиза (tarball)
            tarball_url=$(curl -s "$api_url" | grep '"tarball_url":' | head -n 1 | sed -E 's/.*"tarball_url": "([^"]+)".*/\1/')
            
            # Резервный вариант, если у проекта нет формальных релизов, а только теги
            if [ -z "$tarball_url" ] || [ "$tarball_url" == "null" ]; then
                echo "[!] Последний стабильный релиз не найден. Скачиваем архив ветки по умолчанию..."
                tarball_url="https://github.com{repo_path}/tarball"
            fi

            echo "[+] Скачивание стабильного архива: $tarball_url"
            mkdir -p "$target_path"
            
            # Скачиваем поток данных и распаковываем на лету, отсекая корневое имя папки релиза
            curl -sL "$tarball_url" | tar -xzf - -C "$target_path" --strip-components=1
            echo "[+] Библиотека '$folder' успешно развернута в каталоге: $target_path"
        else
            echo "[+] Библиотека '$folder' уже присутствует в проекте актуальной версии."
        fi
    done
}

# 3. Генерация фейковой пары ключей (Закрытый PFX для подписи и открытый CER для certutil)
generate_fake_certificate() {
    echo "=== 3. Проверка и генерация криптографических сертификатов ==="
    if [ ! -f "$CERT_FILE" ] || [ ! -f "$CERT_CER" ]; then
        echo "[+] Создание закрытого RSA-ключа и открытого сертификата..."
        openssl genrsa -out fake_key.key 4096 2>/dev/null
        
        # Создаем публичный сертификат с метаданными
        openssl req -new -x509 -days 3650 -key fake_key.key -out fake_cert.crt \
            -subj "/C=RU/ST=Siberia/L=Novosibirsk/O=MegaLinuxDriver Inc/OU=Development/CN=WinFSP Driver SelfSigned" 2>/dev/null
            
        # Экспортируем закрытый .pfx контейнер для утилиты osslsigncode
        openssl pkcs12 -export -out "$CERT_FILE" -inkey fake_key.key -in fake_cert.crt -passout pass:"$CERT_PASS"
        
        # Экспортируем открытый .cer ключ (формат DER) для Windows certutil.exe
        openssl x509 -in fake_cert.crt -outform DER -out "$CERT_CER"
        
        # Удаляем промежуточные конфиденциальные файлы
        rm fake_key.key fake_cert.crt
        echo "[+] Пара сертификатов ($CERT_FILE и $CERT_CER) успешно сгенерирована."
    else
        echo "[+] Необходимые сертификаты подписи уже существуют."
    fi
}

# 4. Компиляция проекта через CMake и Ninja в строго изолированной директории
compile_project() {
    echo "=== 4. Подготовка изолированной папки сборки: /$BUILD_DIR ==="
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo "=== Генерация конфигурации CMake через Ninja ==="
    cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..

    echo "=== Запуск компиляции проекта ==="
    ninja
    cd ..
}

# 5. Внедрение цифровой подписи в готовый исполняемый файл
sign_binary() {
    echo "=== 5. Наложение цифровой подписи на бинарный файл ==="
    unsigned_exe="$BUILD_DIR/$TARGET_NAME.exe"
    signed_exe="$BUILD_DIR/${TARGET_NAME}_Signed.exe"

    if [ -f "$unsigned_exe" ]; then
        # Копируем открытый ключ сертификата в папку с бинарником, чтобы инсталлятор нашел его при запуске
        cp "$CERT_CER" "$BUILD_DIR/$CERT_CER"

        # Подписываем исполняемый файл службы
        osslsigncode sign -pkcs12 "$CERT_FILE" -pass "$CERT_PASS" \
            -n "WinFSP Universal Linux Driver Service" \
            -i "https://github.com" \
            -in "$unsigned_exe" \
            -out "$signed_exe"
        
        echo "=========================================================================="
        echo "[УСПЕХ] Монолитная сборка, оптимизация amd64 (AVX2) и подпись завершены!"
        echo "[ИНФО]  Финальный бинарник службы: $signed_exe"
        echo "[ИНФО]  Файл открытого сертификата: $BUILD_DIR/$CERT_CER"
        echo "=========================================================================="
    else
        echo "[ОШИБКА] Компилируемый файл $unsigned_exe не найден! Сборка завершилась сбоем."
        exit 1
    fi
}

# ==============================================================================
#                          ОСНОВНОЙ КОНВЕЙЕР ЗАПУСКА
# ==============================================================================
check_sys_dependencies
download_latest_releases
generate_fake_certificate
compile_project
sign_binary
