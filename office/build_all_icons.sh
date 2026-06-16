#!/bin/bash


# Скрипт сборки и кросс‑развертывания адаптивной темы MS Office Fluent для LibreOffice
# Поддерживает среду MSYS2 (UCRT64), Linux и автоматическую установку пакетов.


set -e  # Прерывать выполнение при любой ошибке


# Переменные окружения по умолчанию
BUILD_DIR="build_theme"
OXT_NAME="msoffice_theme.oxt"
TARGET_OS="windows" # По умолчанию компилируем под Windows
LOG_FILE="build_$(date +%Y%m%d_%H%M%S).log"


# Функция вывода справки
show_help() {
    echo "Использование: \$0 [КЛЮЧ]"
    echo "Ключи сборки и кросс-развертывания:"
    echo "  -w, --windows    Сборка и развертывание под Windows (через unopkg.exe) [По умолчанию]"
    echo "  -l, --linux      Сборка и развертывание под Linux (через unopkg)"
    echo "  -h, --help       Показать эту справку"
    exit 0
}

# Парсинг аргументов командной строки
while [ $# -gt 0 ]; do
    case "$1" in
        -w|--windows)   TARGET_OS="windows"; shift ;;
        -l|--linux)     TARGET_OS="linux"; shift ;;
        -h|--help)      show_help ;;
        *) echo "❌ Неизвестный ключ: $1"; show_help ;;
    esac
done

echo "��️ Выбранная целевая ОС для развертывания: $TARGET_OS"

# ==========================================
# БЛОК ОБЩИХ ФУНКЦИЙ
# ==========================================

# Проверка версии LibreOffice
check_libreoffice_version() {
    echo "[0/7] Проверка версии LibreOffice..."
    if command -v soffice &> /dev/null; then
        version=$(soffice --version | grep -oE '[0-9]+\.[0-9]+' | head -1)
        if [[ -n "$version" ]]; then
            # Сравниваем версии (упрощённо)
            if [[ $(echo "$version < 26.2" | bc -l 2>/dev/null || echo "1") -eq 1 ]]; then
                echo "❌ LibreOffice версии $version не поддерживается. Требуется 26.2+"
                exit 1
            else
                echo "✅ Обнаружена совместимая версия LibreOffice: $version"
            fi
        else
            echo "⚠️ Не удалось определить версию LibreOffice"
        fi
    else
        echo "⚠️ LibreOffice не установлен"
    fi
}

# Проверка зависимостей и предложение установки
check_and_install_dependencies() {
    echo "[1/7] Проверка необходимых системных пакетов..."
    local missing_packages=()

    # Проверка zip
    if ! command -v zip &> /dev/null; then
        missing_packages+=("zip")
    fi

    # Проверка python3
    if ! command -v python3 &> /dev/null; then
        missing_packages+=("python3")
    fi

    # Если мы в MSYS2, проверяем p7zip для бесшовной работы с zip-архивами
    if [[ "$OSTYPE" == "msys" ]] && ! command -v 7z &> /dev/null; then
        missing_packages+=("p7zip")
    fi

    # Если обнаружены недостающие пакеты
    if [ ${#missing_packages[@]} -ne 0 ]; then
        echo "⚠️ В системе отсутствуют необходимые компоненты: ${missing_packages[*]}"
        read -p "Хотите установить их автоматически сейчас? (y/n): " confirm
        if [[ "$confirm" =~ ^[YyИи] ]]; then
            if [[ "$OSTYPE" == "msys" ]]; then
                echo "Используем менеджер пакетов pacman (MSYS2)..."
                # В MSYS2 UCRT64 предпочтительно ставить нативный python
                for pkg in "${missing_packages[@]}"; do
                    if [ "$pkg" == "python3" ]; then
                pacman -S --noconfirm mingw-w64-ucrt-x86_64-python
            else
                pacman -S --noconfirm "$pkg"
            fi
                done
            else
                echo "Используем менеджер пакетов apt (Linux)..."
                sudo apt update && sudo apt install -y "${missing_packages[@]}"
            fi
        else
            echo "❌ Отмена операции. Сборка невозможна без необходимых утилит."
            exit 1
        fi
    else
        echo "✅ Все необходимые системные пакеты присутствуют."
    fi
}

# Генерация структуры и конфигураций
generate_oxt_structure() {
    echo "[2/7] Создание структуры директорий..."
    mkdir -p META-INF
    mkdir -p registry/data/org/openoffice/Office
    mkdir -p icons/images_office2022/cmd
    mkdir -p themes


    echo "[3/7] Генерация манифестов и описания XML..."
    cat > description.xml << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<description xmlns="http://openoffice.org"
             xmlns:xlink="http://w3.org">
  <identifier value="com.microsoft.officefluent.adaptive.theme"/>
  <version value="1.0.2"/>
  <display-name>
    <name lang="en">MS Office Fluent Adaptive Theme</name>
    <name lang="ru">Тема MS Office Fluent (Динамические цвета)</name>
  </display-name>
  <platform>all</platform>
  <dependencies>
    <OpenOffice.org-minimal-version value="26.2" dtd-version="2.0"/>
  </dependencies>
</description>
EOF

    cat > META-INF/manifest.xml << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="http://openoffice.org">
  <manifest:file-entry manifest:media-type="application/vnd.sun.star.configuration-data"
                   manifest:full-path="registry/data/org/openoffice/Office/View.xcu"/>
  <manifest:file-entry manifest:media-type="application/vnd.sun.star.extension-bundle"
                   manifest:full-path="description.xml"/>
  <manifest:file-entry manifest:media-type="image/svg+xml"
                   manifest:full-path="icons/images_office2022.zip"/>
</manifest:manifest>
EOF

    echo "[4/7] Генерация View.xcu (Автоматический Dark/Light режим и палитры приложений)..."
    cat > registry/data/org/openoffice/Office/View.xcu << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org"
                     xmlns:xsi="http://w3.org"
             oor:name="View" oor:package="org.openoffice.Office">
  <node oor:name="IconTheme">
    <prop oor:name="Current" oor:op="fuse"><value>office2022</value></prop>
    <node oor:name="office2022" oor:op="fuse">
      <prop oor:name="Path"><value>images_office2022</value></prop>
    </node>
  </node>
  <node oor:name="Appearance" oor:op="fuse">
    <prop oor:name="Theme" oor:op="fuse"><value>Automatic</value></prop>
  </node>
  <node oor:name="WindowColors" oor:op="fuse">
    <node oor:name="TextDocument" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>1079140</value></prop></node>
    <node oor:name="Spreadsheet" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>1081409</value></prop></node>
    <node oor:name="Presentation" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>14049842</value></prop></node>
    <node oor:name="Drawing" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>11422740</value></prop></node>
    <node oor:name="Formula" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>7818396</value></prop></node>
    <node oor:name="Bibliography" oor:op="fuse"><prop oor:name="ApplicationBackground" oor:op="fuse"><value>6817812</value></prop></node>
  </node>
  <node oor:name="Layout" oor:op="fuse">
    <node oor:name="TextDocument" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
    <node oor:name="Spreadsheet" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
    <node oor:name="Presentation" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
    <node oor:name="Drawing" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
    <node oor:name="Formula" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
    <node oor:name="Bibliography" oor:op="fuse"><prop oor:name="UserInterfaceLayout" oor:op="fuse"><value>Tabbed</value></prop></node>
  </node>
</oor:component-data>
EOF
}

# Python‑скрипт скачивания иконок Fluent UI
download_icons_payload() {
    echo "[5/7] Создание и запуск Python‑скрипта массовой загрузки значков..."
    cat > generate_icons.py << 'PYTHON_SCRIPT'
import os
import urllib.request
import json
import sys

# Корректные URL для доступа к иконкам
API_URL = "https://api.github.com/repos/LibreOffice/core/contents/icon-themes/colibre"
RAW_BASE_URL = "https://raw.githubusercontent.com/LibreOffice/core/refs/heads/master/icon-themes/colibre/"

os.makedirs("icons/images_office2022/cmd", exist_ok=True)

try:
    req = urllib.request.Request(API_URL, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as response:
        if response.getcode() != 200:
            print(f"❌ Ошибка HTTP: {response.getcode()}")
            sys.exit(1)
        files_data = json.loads(response.read().decode())

    success_count = 0
    total_files = len([f for f in files_data if f['name'].endswith('.svg')])
    print(f"   Обнаружено {total_files} иконок. Скачивание...")

    for file_info in files_data:
        filename = file_info['name']
        if filename.endswith('.svg'):
            file_url = RAW_BASE_URL + filename
            target_path = f"icons/images_office2022/cmd/{filename}"
            try:
                urllib.request.urlretrieve(file_url, target_path)
                # Проверяем размер файла (чтобы исключить пустые SVG)
                if os.path.getsize(target_path) > 0:
                    success_count += 1
            if success_count % 50 == 0:  # Обновляем прогресс чаще
                print(f"   Загружено {success_count}/{total_files} файлов...")
            else:
                print(f"   Предупреждение: пустой файл {filename}, пропускаем...")
                os.remove(target_path)
            except Exception as e:
                print(f"   Предупреждение: не удалось загрузить {filename}: {e}")
                continue

    print(f"   ✨ Успешно импортировано {success_count} иконок!")
    if success_count == 0:
        print("   ❌ Критично: не загружено ни одной иконки. Сборка прервана.")
        sys.exit(1)

except Exception as e:
    print(f"   ❌ Критическая ошибка загрузки иконок: {e}")
    sys.exit(1)
PYTHON_SCRIPT

    if ! python3 generate_icons.py; then
        echo "❌ Ошибка выполнения скрипта загрузки иконок. Сборка прервана."
        rm -f generate_icons.py
        exit 1
    fi
    rm generate_icons.py
}

# Сборка и архивация пакета
pack_oxt_package() {
    echo "[6/7] Упаковка ресурсов в архив и создание OXT..."
    cd icons
    zip -q -r images_office2022.zip images_office2022
    rm -rf images_office2022
    cd ..
    zip -q -r "../$OXT_NAME" META-INF registry icons description.xml
    cd ..
    rm -rf "$BUILD_DIR"
    echo "📦 Сборка пакета $OXT_NAME успешно завершена."
}

# Автоматизация кросс‑развёртывания расширения
deploy_oxt_package() {
    echo "[7/7] Автоматизация кросс‑развертывания расширения..."

    if [ "$TARGET_OS" == "windows" ]; then
        echo "Инициализация деплоя под Windows (Среда MSYS2/UCRT64)..."

        # Проверка прав администратора в MSYS2
        net session > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "❌ Требуется запуск с правами администратора для установки расширения"
            exit 1
        fi

        # Закрываем Windows‑процессы LibreOffice
        if tasklist | grep -i "soffice" &> /dev/null; then
            echo "   ⚠️ Обнаружен запущенный LibreOffice под Windows. Закрываем процессы..."
            taskkill /F /IM soffice.bin /T || true
            sleep 1
        fi

        # Поиск путей unopkg.exe в Windows (стандартные папки установки)
        local unopkg_path=""
        if [ -f "/c/Program Files/LibreOffice/program/unopkg.exe" ]; then
            unopkg_path="/c/Program Files/LibreOffice/program/unopkg.exe"
        elif [ -f "/c/Program Files (x86_64)/LibreOffice/program/unopkg.exe" ]; then
            unopkg_path="/c/Program Files (x86_64)/LibreOffice/program/unopkg.exe"
        elif [ -f "/c/Program Files (x86)/LibreOffice/program/unopkg.exe" ]; then
            unopkg_path="/c/Program Files (x86)/LibreOffice/program/unopkg.exe"
        fi

        if [ -n "$unopkg_path" ]; then
            echo "   ⚙️ Выполняется тихая интеграция расширения через: $unopkg_path"
            # Вызов unopkg.exe из окружения MSYS2
            "$unopkg_path" add --suppress-license-acceptance --force "$OXT_NAME"
            if [ $? -eq 0 ]; then
                echo "   ✨ Расширение успешно зарегистрировано в реестре Windows LibreOffice!"
            else
                echo "   ❌ Ошибка при установке расширения через unopkg"
                exit 1
            fi
        else
            echo "   ❌ Ошибка: unopkg.exe не найден в стандартных путях Program Files. Установите OXT файл вручную."
            exit 1
        fi

    elif [ "$TARGET_OS" == "linux" ]; then
        echo "Инициализация деплоя под Linux..."

        # Закрываем Linux‑процессы LibreOffice
        if pgrep -x "soffice.bin" > /dev/null; then
            echo "   ⚠️ Обнаружен запущенный LibreOffice под Linux. Закрываем процессы..."
            killall -w soffice.bin || true
            sleep 1
        fi

        # Проверяем утилиту unopkg в Linux
        if command -v unopkg &> /dev/null; then
            echo "   ⚙️ Выполняется тихая интеграция расширения через Linux unopkg..."
            unopkg add --suppress-license-acceptance --force "$OXT_NAME"
            if [ $? -eq 0 ]; then
                echo "   ✨ Расширение успешно зарегистрировано в Linux!"
            else
                echo "   ❌ Ошибка при установке расширения через unopkg"
                exit 1
            fi
        else
            # Пробуем найти unopkg через стандартные пути установки LibreOffice
            local unopkg_path=""
            if [ -f "/usr/lib/libreoffice/program/unopkg" ]; then
                unopkg_path="/usr/lib/libreoffice/program/unopkg"
            elif [ -f "/opt/libreoffice*/program/unopkg" ]; then
                # Используем find для поиска в случае вариативных версий в /opt
                unopkg_path=$(find /opt/libreoffice* -name unopkg 2>/dev/null | head -n1)
            fi

            if [ -n "$unopkg_path" ]; then
                echo "   ⚙️ Выполняется интеграция через найденный unopkg: $unopkg_path"
                "$unopkg_path" add --suppress-license-acceptance --force "$OXT_NAME"
                if [ $? -eq 0 ]; then
                    echo "   ✨ Расширение успешно зарегистрировано через найденный unopkg!"
                else
                    echo "   ❌ Ошибка при установке через найденный unopkg"
                    exit 1
                fi
            else
                echo "   ❌ Ошибка: Консольная утилита 'unopkg' не найдена в стандартных путях Linux."
                echo "      Проверьте установку LibreOffice или установите OXT файл вручную."
                exit 1
            fi
        fi
    fi
}

# ==========================================
# ТОЧКА ВХОДА И ПОРЯДОК ВЫПОЛНЕНИЯ СКРИПТА
# ==========================================

# Перенаправление вывода в лог‑файл
exec > >(tee -a "$LOG_FILE") 2>&1
echo "Запуск сборки темы MS Office Fluent для LibreOffice: $(date)"

# 1. Проверяем окружение и ставим зависимости
check_and_install_dependencies

# 2. Проверяем версию LibreOffice
check_libreoffice_version


# 3. Очищаем старые следы сборки, если они были
rm -f "$OXT_NAME"
rm -rf "$BUILD_DIR"

# 4. Переходим в чистую директорию сборки
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"


# 5. Вызовы общих изолированных функций сборки
generate_oxt_structure
download_icons_payload
pack_oxt_package

# 6. Вызов кросс‑платформенного деплоя
deploy_oxt_package

echo "🎉 Работа скрипта полностью завершена!"
echo "📄 Лог сборки сохранён в: $LOG_FILE"
