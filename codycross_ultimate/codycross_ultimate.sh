#!/bin/bash

# Настройка изолированного окружения
BASE_DIR="$(pwd)/codycross_mod_env"
TOOLS_DIR="$BASE_DIR/tools"
WORK_DIR="$BASE_DIR/workspace"
DUMP_DIR="$BASE_DIR/dump_output"

INPUT_APK_NAME="codycross_base.apk"
INPUT_XAPK_NAME="codycross_base.xapk"
INPUT_APK="$BASE_DIR/$INPUT_APK_NAME"
INPUT_XAPK="$BASE_DIR/$INPUT_XAPK_NAME"

OUTPUT_APK="$BASE_DIR/codycross_patched.apk"
KEYSTORE_NAME="$TOOLS_DIR/my-release-key.keystore"

# --- БЛОК ОБЩИХ ФУНКЦИЙ (УТИЛИТЫ) ---

log_info() { echo -e "\e[34m[*]\e[0m $1"; }
log_success() { echo -e "\e[32m[+]\e[0m $1"; }
log_warn() { echo -e "\e[33m[!]\e[0m $1"; }
log_error() { echo -e "\e[31m[-]\e[0m $1"; exit 1; }

# ИСПРАВЛЕННАЯ ФУНКЦИЯ: Убрано регулярное выражение, используется endswith для надежности
download_latest_github_release() {
    local repo="$1" pattern="$2" output="$3"
    
    if [ -z "$repo" ] || [ -z "$pattern" ] || [ -z "$output" ]; then
        log_error "Неверные аргументы для download_latest_github_release"
    fi

    log_info "Загрузка с GitHub: $repo (pattern: $pattern)..."
    
    local api_url="https://api.github.com/repos/$repo/releases/latest"
    local headers=()
    [ -n "${GITHUB_TOKEN:-}" ] && headers+=("-H" "Authorization: token $GITHUB_TOKEN")
    
    local json
    # Добавляем -w для отладки или оставляем -s для тишины. Важно: проверяем код ответа curl
    json=$(curl -s -f -L "${headers[@]}" "$api_url") || log_error "Ошибка сети при запросе к GitHub API (проверьте URL: $api_url)"
    
    if echo "$json" | grep -q "API rate limit"; then
        log_error "Достигнут лимит запросов GitHub. Добавьте GITHUB_TOKEN или подождите."
    fi
    
    # Проверка на ошибку API (например, репозиторий не найден)
    if echo "$json" | grep -q '"message"'; then
        log_error "GitHub API вернул ошибку: $(echo "$json" | grep 'message' | head -n 1)"
    fi

    local url
    if command -v jq &> /dev/null; then
        # ИСПРАВЛЕНИЕ ЗДЕСЬ:
        # Вместо test("\.jar") используем endswith(".jar")
        # Если pattern передан как ".jar", мы ищем файлы, заканчивающиеся на это.
        # Для универсальности: если pattern содержит спецсимволы regex, jq их не любит.
        # Мы предполагаем, что pattern - это простая подстрока или расширение.
        
        if [ "$pattern" = "\.jar" ]; then
            # Специальный случай для apktool: ищем файлы, оканчивающиеся на .jar
            url=$(echo "$json" | jq -r '.assets[] | select(.name | endswith(".jar")) | .browser_download_url' | head -n 1)
        elif [ "$pattern" = "\.zip" ]; then
            url=$(echo "$json" | jq -r '.assets[] | select(.name | endswith(".zip")) | .browser_download_url' | head -n 1)
        else
            # Универсальный fallback: ищем точное совпадение подстроки в имени (без regex)
            # Это менее точно, но безопасно для jq
            url=$(echo "$json" | jq -r --arg p "$pattern" '.assets[] | select(.name | contains($p)) | .browser_download_url' | head -n 1)
        fi
    else
        # Fallback без jq (менее надежно, но работает)
        url=$(echo "$json" | grep '"browser_download_url"' | awk -F'"' '{print $4}' | grep "$pattern" | head -n 1)
    fi

    if [ -z "$url" ]; then
        log_error "Файл по шаблону '$pattern' не найден в релизе $repo. Возможно, структура релиза изменилась или имя файла не совпадает."
    fi

    curl -L -s -# "$url" -o "$output" || log_error "Скачивание не удалось: $url"
    log_success "Загружено: $output"
}

# Функция извлечения базового APK из локального XAPK архива
extract_base_from_local_xapk() {
    log_info "Обнаружен локальный файл XAPK. Начинаем извлечение ядра игры..."
    mkdir -p "$BASE_DIR/xapk_extracted"
    unzip -q "$INPUT_XAPK" -d "$BASE_DIR/xapk_extracted"
    
    if [ -f "$BASE_DIR/xapk_extracted/com.fanatee.cody.apk" ]; then
        mv "$BASE_DIR/xapk_extracted/com.fanatee.cody.apk" "$INPUT_APK"
    elif [ -f "$BASE_DIR/xapk_extracted/base.apk" ]; then
        mv "$BASE_DIR/xapk_extracted/base.apk" "$INPUT_APK"
    else
        rm -rf "$BASE_DIR/xapk_extracted"
        log_error "Внутри вашего XAPK не найден базовый APK-файл игры."
    fi
    
    rm -rf "$BASE_DIR/xapk_extracted"
    log_success "Базовый APK успешно извлечен из XAPK!"
}

# --- ШАГ 0: ПРОВЕРКА И ПРЕДЛОЖЕНИЕ УСТАНОВКИ ЗАВИСИМОСТЕЙ ---

echo "=== Проверка системных компонентов MSYS2 ==="
MISSING_PACKAGES=()

for cmd in curl wget unzip zip python3 java; do
    if ! command -v $cmd &> /dev/null; then
        if [ "$cmd" == "java" ]; then MISSING_PACKAGES+=("mingw-w64-ucrt-x86_64-openjdk")
        else MISSING_PACKAGES+=("$cmd"); fi
    fi
done

if ! command -v optipng &> /dev/null; then MISSING_PACKAGES+=("mingw-w64-ucrt-x86_64-optipng"); fi
if ! command -v jpegoptim &> /dev/null; then MISSING_PACKAGES+=("mingw-w64-ucrt-x86_64-jpegoptim"); fi
if ! command -v 7z &> /dev/null; then MISSING_PACKAGES+=("mingw-w64-ucrt-x86_64-p7zip"); fi

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    log_warn "В системе отсутствуют компоненты: ${MISSING_PACKAGES[*]}"
    read -p "Хотите автоматически установить их через pacman сейчас? (y/n): " install_choice
    if [[ "$install_choice" =~ ^[Yy]$ ]]; then
        pacman -S --needed --noconfirm "${MISSING_PACKAGES[@]}"
        if [ $? -ne 0 ]; then log_error "Не удалось установить пакеты. Запустите MSYS2 от имени Администратора."; fi
    else log_error "Скрипт не может продолжить работу без утилит."; fi
else log_success "Все системные компоненты MSYS2 установлены."; fi

# --- ЭТАП 1 ИЗ 2: ОЧИСТКА ПЕРЕД НАЧАЛОМ СБОРКИ (PRE-BUILD CLEAN) ---
log_info "Выполнение предварительной очистки рабочего окружения..."
rm -rf "$WORK_DIR" "$DUMP_DIR" "$BASE_DIR/zip_out"
rm -f "$BASE_DIR/temp_structure.apk" "$BASE_DIR/unsigned.apk" "$BASE_DIR/aligned.apk"

mkdir -p "$TOOLS_DIR" "$WORK_DIR" "$DUMP_DIR"

# --- ШАГ 1: ПРОВЕРКА И ПОДГОТОВКА ИСХОДНОГО ФАЙЛА ПРИЛОЖЕНИЯ ---

if [ ! -f "$INPUT_APK" ] && [ -f "$INPUT_XAPK" ]; then
    extract_base_from_local_xapk
fi

if [ ! -f "$INPUT_APK" ]; then
    echo "----------------------------------------------------------------------"
    log_warn "Файл игры не найден."
    echo "Пожалуйста, положите исходный файл игры в папку по одному из путей:"
    echo "-> Обычный APK: $INPUT_APK"
    echo "-> Либо XAPK архив: $INPUT_XAPK"
    log_error "Добавьте файл и перезапустите скрипт."
fi

# --- ШАГ 2: СКАЧИВАНИЕ ИНСТРУМЕНТОВ РАЗРАБОТКИ (УЛЬТРА-ЛЕГКИЙ ВАРИАНТ) ---
log_info "Проверка внутренних утилит модификации..."

if [ ! -f "$TOOLS_DIR/apktool.jar" ]; then
    download_latest_github_release "iBotPeaches/Apktool" "\.jar" "$TOOLS_DIR/apktool.jar"
    # Используем curl и для apktool.bat тоже, для единообразия
    if [ ! -f "$TOOLS_DIR/apktool.bat" ]; then
        curl -s -L "https://raw.githubusercontent.com/iBotPeaches/Apktool/master/scripts/windows/apktool.bat" -o "$TOOLS_DIR/apktool.bat"
        if [ $? -ne 0 ]; then log_error "Не удалось скачать apktool.bat"; fi
    fi
fi

# ОПТИМИЗИРОВАННЫЙ БЛОК: Прямая загрузка apksigner и zipalign через curl
if [ ! -f "$TOOLS_DIR/zipalign.exe" ] || [ ! -f "$TOOLS_DIR/apksigner.jar" ]; then
    log_info "Загрузка zipalign.exe и apksigner.jar через curl..."
    
    # Загрузка zipalign.exe
    if [ ! -f "$TOOLS_DIR/zipalign.exe" ]; then
        log_info "Скачивание zipalign.exe..."
        # -L: следовать редиректам (GitHub часто редиректит raw ссылки)
        # -#: показать прогресс (работает в новых версиях curl)
        # -o: сохранить в файл
        curl -L -# "https://github.com/AndnixSH/APKToolGUI/raw/refs/heads/master/Tools/zipalign.exe" -o "$TOOLS_DIR/zipalign.exe"
        if [ $? -ne 0 ]; then log_error "Не удалось скачать zipalign.exe. Проверьте интернет или ссылку."; fi
        log_success "zipalign.exe загружен."
    fi
    
    # Загрузка apksigner.jar
    if [ ! -f "$TOOLS_DIR/apksigner.jar" ]; then
        log_info "Скачивание apksigner.jar..."
        curl -L -# "https://github.com/AndnixSH/APKToolGUI/raw/refs/heads/master/Tools/apksigner.jar" -o "$TOOLS_DIR/apksigner.jar"
        if [ $? -ne 0 ]; then log_error "Не удалось скачать apksigner.jar. Проверьте интернет или ссылку."; fi
        log_success "apksigner.jar загружен."
    fi

    # Создание бат-файла-обертки для apksigner только если jar существует
    if [ -f "$TOOLS_DIR/apksigner.jar" ] && [ ! -f "$TOOLS_DIR/apksigner.bat" ]; then
        echo -e "@echo off\njava -jar \"%~dp0apksigner.jar\" %*" > "$TOOLS_DIR/apksigner.bat"
        log_success "Создана обертка apksigner.bat"
    fi
fi

if [ ! -f "$TOOLS_DIR/Il2CppDumper/Il2CppDumper.exe" ]; then
    download_latest_github_release "Perfare/Il2CppDumper" "\.zip" "$TOOLS_DIR/il2cppdumper.zip"
    mkdir -p "$TOOLS_DIR/Il2CppDumper"
    unzip -q "$TOOLS_DIR/il2cppdumper.zip" -d "$TOOLS_DIR/Il2CppDumper/"
    rm "$TOOLS_DIR/il2cppdumper.zip"
fi

export PATH="$TOOLS_DIR:$PATH"

# --- ШАГ 3: БЛОК ОСНОВНОЙ МОДИФИКАЦИИ И ИНТЕРАКТИВНЫХ ОПЦИЙ ---

COMPRESS_GRAPHICS=false
read -p "Хотите запустить глубокое сжатие PNG/JPG текстур приложения? Это займет время. (y/n): " compress_choice
if [[ "$compress_choice" =~ ^[Yy]$ ]]; then COMPRESS_GRAPHICS=true; fi

ACTIVATE_CHEATS=false
read -p "Включить чит на бесконечных помощников/токены? (y/n): " cheat_choice
if [[ "$cheat_choice" =~ ^[Yy]$ ]]; then ACTIVATE_CHEATS=true; fi

ACTIVATE_PREMIUM=false
read -p "Разблокировать Premium-контент/подписки? (y/n): " premium_choice
if [[ "$premium_choice" =~ ^[Yy]$ ]]; then ACTIVATE_PREMIUM=true; fi

log_info "Декомпиляция APK..."
apktool.bat d "$INPUT_APK" -o "$WORK_DIR" --no-res -f

log_info "Оптимизация Manifest под Android 14 (API 34)..."
MANIFEST="$WORK_DIR/AndroidManifest.xml"
sed -i 's/targetSdkVersion="[0-9]*"/targetSdkVersion="34"/' "$MANIFEST"
sed -i '/com.google.android.gms.permission.AD_ID/d' "$MANIFEST"
sed -i '/com.google.android.gms.version/d' "$MANIFEST"
sed -i '/com.google.android.gms.games.APP_ID/d' "$MANIFEST"

log_info "Очистка архитектур и удаление лишних локализаций..."
if [ -d "$WORK_DIR/lib" ]; then find "$WORK_DIR/lib/" -mindepth 1 -maxdepth 1 ! -name 'arm64-v8a' -exec rm -rf {} +; fi
if [ -d "$WORK_DIR/res" ]; then find "$WORK_DIR/res/" -maxdepth 1 -type d -name "values-*" ! -name "values-ru*" ! -name "values-en*" -exec rm -rf {} +; fi

if [ "$COMPRESS_GRAPHICS" = true ]; then
    log_info "Запуск оптимизации графических ресурсов (PNG и JPG)..."
    if [ -d "$WORK_DIR/res" ]; then
        log_info "Сжатие PNG файлов..."
        find "$WORK_DIR/res/" -type f -name "*.png" -exec optipng -o2 -strip all {} + 2>/dev/null
        log_info "Сжатие JPG файлов..."
        find "$WORK_DIR/res/" -type f -name "*.jpg" -o -name "*.jpeg" -exec jpegoptim --strip-all {} + 2>/dev/null
    fi
fi

log_info "Вызов Il2CppDumper и патчинг бинарного кода движка..."
SO_FILE="$WORK_DIR/lib/arm64-v8a/libil2cpp.so"
METADATA_FILE="$WORK_DIR/assets/bin/data/managed/Metadata/global-metadata.dat"

if [ -f "$SO_FILE" ] && [ -f "$METADATA_FILE" ]; then
    cd "$TOOLS_DIR/Il2CppDumper"
    ./Il2CppDumper.exe "$SO_FILE" "$METADATA_FILE" "$DUMP_DIR" > /dev/null
    cd "$BASE_DIR/.."

    if [ -f "$DUMP_DIR/script.json" ]; then
        log_info "Запуск Python-модификатора бинарного кода..."
        export PYTHON_ACTIVATE_CHEATS="$ACTIVATE_CHEATS"
        export PYTHON_ACTIVATE_PREMIUM="$ACTIVATE_PREMIUM"

        python3 -c "
import json
import os

so_path = '$SO_FILE'
script_json_path = '$DUMP_DIR/script.json'
activate_cheats = os.environ.get('PYTHON_ACTIVATE_CHEATS') == 'true'
activate_premium = os.environ.get('PYTHON_ACTIVATE_PREMIUM') == 'true'

AD_KEYWORDS = [
    'ShowAd', 'ShowInterstitial', 'ShowRewardedVideo', 'LoadAd', 
    'InitAnalytics', 'TrackEvent', 'FirebaseAnalytics', 'AppLovin', 
    'FacebookAds', 'AdMob', 'CodyCross.Ads'
]
RET_ARM64 = bytes.fromhex('D65F03C0')

CHEAT_KEYWORDS = [
    'get_Tokens', 'get_Coins', 'GetTokenCount', 'HasEnoughTokens', 'get_Amount'
]
MOV_MAX_AND_RET = bytes.fromhex('3F478E52D65F03C0')

PREMIUM_KEYWORDS = [
    'IsPremium', 'IsSubscribed', 'HasPremium', 'HasVIP', 'IsVip', 
    'PurchasedPack', 'CheckSubscription', 'IsSubscriptionActive'
]
MOV_TRUE_AND_RET = bytes.fromhex('20008052D65F03C0')

UPDATE_KEYWORDS = [
    'CheckForUpdates', 'IsUpdateRequired', 'get_IsNewVersionAvailable', 
    'ForcedUpdate', 'ShowUpdateDialog', 'AppUpdateManager', 'VersionCheck'
]
MOV_FALSE_AND_RET = bytes.fromhex('00008052D65F03C0')

with open(script_json_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

ad_patched = 0
cheat_patched = 0
premium_patched = 0
update_patched = 0

with open(so_path, 'r+b') as so_file:
    for item in data:
        method_name = item.get('Name', '')
        offset = item.get('Address', 0)
        if offset <= 0:
            continue
            
        if any(kw.lower() in method_name.lower() for kw in AD_KEYWORDS):
            so_file.seek(offset)
            so_file.write(RET_ARM64)
            ad_patched += 1
            
        elif activate_cheats and any(kw.lower() in method_name.lower() for kw in CHEAT_KEYWORDS):
            if 'codycross' in method_name.lower() or 'fanatee' in method_name.lower():
                so_file.seek(offset)
                so_file.write(MOV_MAX_AND_RET)
                cheat_patched += 1

        elif activate_premium and any(kw.lower() in method_name.lower() for kw in PREMIUM_KEYWORDS):
            if 'codycross' in method_name.lower() or 'fanatee' in method_name.lower():
                so_file.seek(offset)
                so_file.write(MOV_TRUE_AND_RET)
                premium_patched += 1

        elif any(kw.lower() in method_name.lower() for kw in UPDATE_KEYWORDS):
            if 'codycross' in method_name.lower() or 'fanatee' in method_name.lower() or 'update' in method_name.lower():
                so_file.seek(offset)
                so_file.write(MOV_FALSE_AND_RET)
                update_patched += 1

print(f'[+] Успешно обезврежено Unity-методов рекламы: {ad_patched}')
print(f'[+] Нейтрализовано механизмов проверки обновлений: {update_patched}')
if activate_cheats:
    print(f'[+] Успешно пропатчено методов на бесконечные токены/помощники: {cheat_patched}')
if activate_premium:
    print(f'[+] Успешно разблокировано Premium-методов/подписок: {premium_patched}')
"
    else log_warn "Не удалось получить script.json для авто-патча."; fi
fi

log_info "Подготовка структуры APK через Apktool..."
apktool.bat b "$WORK_DIR" -o "$BASE_DIR/temp_structure.apk"

log_info "Запуск ультра-сжатия архива через 7-Zip..."
mkdir -p "$BASE_DIR/zip_out"
unzip -q "$BASE_DIR/temp_structure.apk" -d "$BASE_DIR/zip_out"
cd "$BASE_DIR/zip_out"

7z a -tzip -mx9 -m0=Deflate "$BASE_DIR/unsigned.apk" * -xr!*.so -xr!*.dat -xr!*.assets -xr!*.bundle 2>/dev/null
7z a -tzip -mx0 "$BASE_DIR/unsigned.apk" lib/ assets/ 2>/dev/null

cd "$BASE_DIR"

# --- ЭТАП 2 ИЗ 2: ОЧИСТКА ПОСЛЕ СБОРКИ (POST-BUILD CLEAN) ---
log_info "Выполнение финальной очистки временных рабочих папок..."
rm -rf "$WORK_DIR" "$DUMP_DIR" "$BASE_DIR/zip_out" "$BASE_DIR/temp_structure.apk"

log_info "Оптимизация выравнивания данных (Zipalign)..."
zipalign.exe -v -p 4 "$BASE_DIR/unsigned.apk" "$BASE_DIR/aligned.apk"
rm -f "$BASE_DIR/unsigned.apk"

log_info "Финальная криптографическая подпись APK..."
if [ ! -f "$KEYSTORE_NAME" ]; then
    log_info "Генерация нового цифрового ключа подписи..."
    keytool -genkey -v -keystore "$KEYSTORE_NAME" -alias cody_alias -keyalg RSA -keysize 2048 -validity 10000 -storepass password123 -keypass password123 -dname "CN=Mod, O=MSYS2, C=US"
fi

apksigner.bat sign --ks "$KEYSTORE_NAME" --ks-pass pass:password123 --key-pass pass:password123 --out "$OUTPUT_APK" "$BASE_DIR/aligned.apk"
rm -f "$BASE_DIR/aligned.apk"

log_success "ПРОЦЕСС УСПЕШНО ЗАВЕРШЕН!"
log_success "Ваш ультра-сжатый и модифицированный APK: $OUTPUT_APK"
