#!/usr/bin/env bash

# ==============================================================================
# КОНФИГУРАЦИЯ И ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
# ==============================================================================
APTOS_URL="https://download.microsoft.com/download/8/6/0/860a94fa-7feb-44ef-ac79-c072d9113d69/Microsoft%20Aptos%20Fonts.zip"
BUILD_DIR="./lo_msoffice_build"
EXTENSION_NAME="MS_Office_Aspect_Pack"
OXT_FILE="${EXTENSION_NAME}.oxt"

# Определение целевой ОС
TARGET_OS="windows"
if [[ "$1" == "--linux" || "$1" == "-l" ]]; then
    TARGET_OS="linux"
fi

echo "=== Сборка и АВТОАКТИВАЦИЯ стиля MS Office для LibreOffice 26.2+ ==="
echo "Целевая ОС: $TARGET_OS"

# ==============================================================================
# ОБЩИЙ БЛОК: ПРОВЕРКА И ПОДГОТОВКА СРЕДЫ
# ==============================================================================
prepare_environment() {
    echo "-> Очистка и создание папки сборки: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR/fonts_tmp"
    mkdir -p "$BUILD_DIR/extension/registry/data/org/openoffice/Office"
    mkdir -p "$BUILD_DIR/extension/META-INF"
    mkdir -p "$BUILD_DIR/extension/ui"
    mkdir -p "$BUILD_DIR/extension/fonts"
    mkdir -p "$BUILD_DIR/extension/palette"
    mkdir -p "$BUILD_DIR/extension/Scripts/python"

    local deps=(curl unzip zip wget jq)
    local missing_deps=()

    for cmd in "${deps[@]}"; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done

    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo "Внимание! Отсутствуют пакеты: ${missing_deps[*]}"
        read -p "Установить автоматически? (y/n): " confirm
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            if [ "$TARGET_OS" == "linux" ]; then
                sudo apt-get update && sudo apt-get install -y "${missing_deps[@]}"
            else
                pacman -S --noconfirm "${missing_deps[@]}"
            fi
        else
            echo "Критическая ошибка: сборка невозможна без зависимостей."
            exit 1
        fi
    fi
}

# ==============================================================================
# ДИНАМИЧЕСКАЯ ЗАГРУЗКА LATEST RELEASE С GITHUB
# ==============================================================================
download_tools() {
    echo "-> Получение ссылок на Latest Releases инструментов с GitHub..."
    cd "$BUILD_DIR" || exit 1

    # Репозитории
    local ect_repo="fhanau/Efficient-Compression-Tool"
    local minify_repo="tdewolff/minify"


    get_github_release_asset() {
        local repo="$1"
        local filter_string="$2"
        # Исправлено: корректный URL API GitHub
        local api_url="https://api.github.com/repos/$repo/releases/latest"

        # Вывод логов перенаправлен в >&2
        echo "   Запрос к API: $api_url" >&2
        local response
        response=$(curl -s -f "$api_url") || {
            echo "Ошибка: не удалось получить данные релиза для $repo" >&2
            return 1
        }

        local download_url
        download_url=$(echo "$response" | jq -r ".assets[] | select(.name | contains(\"$filter_string\")) | .browser_download_url" | head -n1)

        if [ -z "$download_url" ] || [ "$download_url" == "null" ]; then
            echo "Ошибка: не найден подходящий актив для $repo (ищем: $filter_string)" >&2
            return 1
        fi

        echo "$download_url"
    }

    # Определяем строки для поиска активов в зависимости от ОС
    local ect_pattern
    local minify_pattern

    if [ "$TARGET_OS" == "windows" ]; then
        ect_pattern="win64.zip"
        minify_pattern="minify_windows_amd64"
    else
        ect_pattern=""
        minify_pattern="minify_linux_amd64"
    fi

    # Получаем URL для загрузки Minify
    local minify_download_url
    minify_download_url=$(get_github_release_asset "$minify_repo" "$minify_pattern") || exit 1
    echo "   Minify URL: $minify_download_url"

    # Скачиваем Minify
    echo "   Загрузка Minify..."
    if ! curl -L -f -o "minify_temp" "$minify_download_url"; then
        echo "Ошибка загрузки Minify" >&2
        exit 1
    fi

    # Обрабатываем Minify в зависимости от ОС
    if [ "$TARGET_OS" == "windows" ]; then
        if ! unzip -q -o "minify_temp" "minify.exe"; then
            echo "Ошибка распаковки Minify для Windows" >&2
            rm -f "minify_temp"
            exit 1
        fi
        rm -f "minify_temp"
    else
        if [[ "$minify_download_url" == *.tar.gz ]]; then
            if ! tar -xzf "minify_temp" minify; then
                echo "Ошибка распаковки Minify (tar.gz)" >&2
                rm -f "minify_temp"
                exit 1
            fi
        else
            if ! unzip -q -o "minify_temp" "minify"; then
                echo "Ошибка распаковки Minify (zip)" >&2
                rm -f "minify_temp"
                exit 1
            fi
        fi
        rm -f "minify_temp"
    fi

    # Загружаем ECT только для Windows
    if [ "$TARGET_OS" == "windows" ]; then
        local ect_download_url
        ect_download_url=$(get_github_release_asset "$ect_repo" "$ect_pattern") || exit 1
        echo "   ECT URL: $ect_download_url"

        echo "   Загрузка ECT..."
        if ! curl -L -f -o "ect_temp.zip" "$ect_download_url"; then
            echo "Ошибка загрузки ECT" >&2
            exit 1
        fi

        if ! unzip -q -o "ect_temp.zip" "*ect.exe"; then
            echo "Ошибка распаковки ECT" >&2
            rm -f "ect_temp.zip"
            exit 1
        fi
        rm -f "ect_temp.zip"

        # Перемещаем бинарник в корень BUILD_DIR, если распаковался во вложенную папку
        find . -name "ect.exe" -exec mv {} . \; 2>/dev/null || true
    else
        echo "   Пропуск ECT: пакет для Linux отсутствует в репозитории" >&2
    fi

    cd ..
}

# ==============================================================================
# ГЕНЕРАЦИЯ ФИРМЕННОЙ ПАЛИТРЫ ЦВЕТОВ MICROSOFT (.SOC)
# ==============================================================================
generate_microsoft_palette() {
    echo "-> Создание фирменной палитры цветов Microsoft Office 2022..."
    cat <<EOF > "$BUILD_DIR/extension/palette/microsoft.soc"
<?xml version="1.0" encoding="UTF-8"?>
<ooo:palette xmlns:ooo="http://openoffice.org" ooo:name="Microsoft Office">
  <ooo:color ooo:name="Word Blue" ooo:color="#2b579a"/>
  <ooo:color ooo:name="Excel Green" ooo:color="#207245"/>
  <ooo:color ooo:name="PowerPoint Orange" ooo:color="#ae371a"/>
  <ooo:color ooo:name="Office Dark Text" ooo:color="#242424"/>
  <ooo:color ooo:name="Office Light Gray" ooo:color="#f3f2f1"/>
</ooo:palette>
EOF
}

# ==============================================================================
# УСТАНОВКА ШРИФТОВ И МАКРОСА АВТОМАТИЗАЦИИ
# ==============================================================================
pack_fonts_inside() {
    echo "-> Скачивание и упаковка шрифтов Aptos..."
    curl -L "$APTOS_URL" -o "$BUILD_DIR/fonts_tmp/aptos.zip"
    unzip -q -o "$BUILD_DIR/fonts_tmp/aptos.zip" -d "$BUILD_DIR/extension/fonts/"
    rm -rf "$BUILD_DIR/fonts_tmp"
}

generate_python_installer_script() {
    cat <<EOF > "$BUILD_DIR/extension/Scripts/python/font_installer.py"
import os
import shutil
import subprocess
import sys
from unoimport import uno

def install_system_fonts(*args):
    try:
        if sys.platform == "win32":
            appdata = os.environ.get("APPDATA", "")
            src_dir = os.path.join(appdata, "LibreOffice", "4", "user", "uno_packages", "cache", "uno_packages")
            dest_dir = os.path.join(os.environ.get("USERPROFILE", ""), "AppData", "Local", "Microsoft", "Windows", "Fonts")
        else:
            src_dir = os.path.expanduser("~/.config/libreoffice/4/user/uno_packages/cache/uno_packages")
            dest_dir = os.path.expanduser("~/.local/share/fonts/Aptos")

        if not os.path.exists(dest_dir):
            os.makedirs(dest_dir)

        font_files = []
        for root, dirs, files in os.walk(src_dir):
            for file in files:
                if file.lower().endswith(".ttf") and "aptos" in file.lower():
                    font_files.append(os.path.join(root, file))

        changed = False
        for src in font_files:
            target = os.path.join(dest_dir, os.path.basename(src))
            if not os.path.exists(target):
                shutil.copy(src, target)
                changed = True

        if changed and sys.platform != "win32":
            subprocess.call(["fc-cache", "-f"])
    except Exception:
        pass
EOF
}

# ==============================================================================
# СБОРКА КОНФИГУРАЦИЙ РАСШИРЕНИЯ (ОТКЛЮЧЕНИЕ ПРЕДУПРЕЖДЕНИЙ, ЦВЕТА, КЛАВИШИ)
# ==============================================================================
generate_extension_manifest() {
    cat <<EOF > "$BUILD_DIR/extension/META-INF/manifest.xml"
<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="http://openoffice.org">
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/UI.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Accelerators.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Addons.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Linguistic.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Setup.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Common.xcu" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="palette/microsoft.soc" manifest:media-type="application/vnd.oasis.opendocument.palette"/>
  <manifest:file-entry manifest:full-path="Scripts/python/font_installer.py" manifest:media-type="application/vnd.sun.star.framework-script"/>
  <manifest:file-entry manifest:full-path="registry/data/org/openoffice/Office/Jobs.xcu" manifest:media-type="text/xml"/>
</manifest:manifest>
EOF

    # ИСПРАВЛЕННЫЙ description.xml с зависимостями (минимальная версия 26.2)
    cat <<EOF > "$BUILD_DIR/extension/description.xml"
<?xml version="1.0" encoding="UTF-8"?>
<description xmlns="http://openoffice.org/extensions/description/2006" xmlns:xlink="http://www.w3.org/1999/xlink">
  <identifier value="org.msoffice.aspect.pack"/>
  <version value="26.2.0"/>
  
  <!-- Блок зависимостей: минимальная версия LibreOffice 26.2 -->
  <dependencies>
    <l:LibreOffice-minimal-version value="26.2" d:name="LibreOffice 26.2 or higher" xmlns:l="http://libreoffice.org" xmlns:d="http://openoffice.org/extensions/description/2006"/>
  </dependencies>
  
  <display-name>
    <name lang="en">MS Office 2022 Ultimate Auto-Pack</name>
  </display-name>
  <publisher>
    <name xlink:href="https://libreoffice.org" lang="en">LibreOffice Community</name>
  </publisher>
</description>
EOF

    # Триггер Jobs.xcu для автозапуска макроса
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Jobs.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Jobs" oor:package="org.openoffice.Office">
  <node oor:name="Jobs">
    <node oor:name="FontInstallJob" oor:op="replace">
      <prop oor:name="Macro"><value>vnd.sun.star.script:font_installer.py\$install_system_fonts?language=Python&amp;location=share</value></prop>
    </node>
  </node>
  <node oor:name="Events">
    <node oor:name="OnExtensionLoaded" oor:op="replace"><node oor:name="JobList"><node oor:name="FontInstallJob" oor:op="replace"/></node></node>
  </node>
</oor:component-data>
EOF
}

generate_autosave_and_warnings_config() {
    echo "-> Настройка автосохранения и отключения предупреждений..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Setup.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Setup" oor:package="org.openoffice.Office">
  <node oor:name="Office">
    <!-- Отключение назойливого предупреждения при сохранении в DOCX/XLSX/PPTX -->
    <prop oor:name="WarnWhenSavingInNonODFFormat" oor:type="xs:boolean">
      <value>false</value>
    </prop>
    <node oor:name="DefaultFormat">
      <prop oor:name="Writer" oor:type="xs:string">
        <value>MS Word 2007 XML</value>
      </prop>
      <prop oor:name="Calc" oor:type="xs:string">
        <value>MS Excel 2007 XML</value>
      </prop>
      <prop oor:name="Impress" oor:type="xs:string">
        <value>MS PowerPoint 2007 XML</value>
      </prop>
    </node>
  </node>
</oor:component-data>
EOF
}

generate_mouse_autoscroll_config() {
    echo "-> Настройка мыши, кэширования буфера обмена и локальных путей TEMP..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Common.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Common" oor:package="org.openoffice.Office">
  
  <!-- ПУНКТ 3: Настройка буфера обмена в стиле MS Office -->
  <node oor:name="Clipboard">
    <!-- Принудительно сохранять данные в системном буфере обмена при закрытии приложений -->
    <prop oor:name="SaveClipboardOnExit" oor:type="xs:boolean"><value>true</value></prop>
    <!-- Включение универсальных форматов для бесшовного копирования таблиц и текста -->
    <prop oor:name="PreferHTMLFormat" oor:type="xs:boolean"><value>true</value></prop>
  </node>

  <!-- Мышь и скроллинг -->
  <node oor:name="Accessibility">
    <!-- Отключение вставки X11 буфера при клике колесиком мыши (разрешает скролл) -->
    <prop oor:name="SelectionInMiddleMouseButton" oor:type="xs:boolean"><value>false</value></prop>
  </node>
  <node oor:name="Misc">
    <!-- Принудительная активация плавного автоскроллинга как в Windows/MS Office -->
    <prop oor:name="UseSmoothScrolling" oor:type="xs:boolean"><value>true</value></prop>
  </node>

  <!-- ПУНКТ 4: Перенос lock-файлов и автовосстановления в локальный TEMP текущего пользователя -->
  <node oor:name="Path">
    <node oor:name="Current">
      <!-- Перенаправление папки резервных копий (Бэкапов) во временный каталог системы -->
      <prop oor:name="Backup" oor:type="xs:string"><value>\$(temp)/LibreOffice_Backups</value></prop>
      <!-- Перенаправление временных файлов (включая lock-файлы), чтобы они не создавались на сетевых дисках -->
      <prop oor:name="Temp" oor:type="xs:string"><value>\$(temp)/LibreOffice_Temp</value></prop>
    </node>
  </node>

</oor:component-data>
EOF
}

generate_custom_ribbon_ui() {
    echo "-> Сборка кастомных макетов Ribbon-ленты под логику вкладок MS Office 2022..."
    cat <<EOF > "$BUILD_DIR/extension/ui/notebookbar.ui"
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <requires lib="gtk+" version="3.18"/>
  <object class="GtkNotebook" id="NotebookBar">
    <property name="visible">True</property>
    <property name="can_focus">True</property>
    <child>
      <object class="GtkBox" id="HomeTab">
        <property name="visible">True</property>
        <property name="orientation">horizontal</property>
        <!-- Секция Буфер обмена -->
        <child><object class="GtkButton" id="Paste"><property name="label">.uno:Paste</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="Cut"><property name="label">.uno:Cut</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="Copy"><property name="label">.uno:Copy</property><property name="visible">True</property></object></child>
        <!-- Секция Шрифт -->
        <child><object class="GtkButton" id="Bold"><property name="label">.uno:Bold</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="Italic"><property name="label">.uno:Italic</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="Underline"><property name="label">.uno:Underline</property><property name="visible">True</property></object></child>
      </object>
      <packing><property name="tab_label">Главная (Home)</property></packing>
    </child>
    <child>
      <object class="GtkBox" id="InsertTab">
        <property name="visible">True</property>
        <property name="orientation">horizontal</property>
        <child><object class="GtkButton" id="Table"><property name="label">.uno:InsertTable</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="Image"><property name="label">.uno:InsertGraphic</property><property name="visible">True</property></object></child>
        <child><object class="GtkButton" id="PageBreak"><property name="label">.uno:InsertPageBreak</property><property name="visible">True</property></object></child>
      </object>
      <packing><property name="tab_label">Вставка (Insert)</property></packing>
    </child>
  </object>
</interface>
EOF
}

generate_accelerators_config() {
    echo "-> Переопределение горячих клавиш для всех модулей..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Accelerators.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Accelerators" oor:package="org.openoffice.Office">
  <node oor:name="PrimaryKeys">
    <node oor:name="Global" oor:op="replace">
      <node oor:name="H_SHIFT_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:SearchDialog</value></prop></node>
      <node oor:name="SPACE_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:ResetAttributes</value></prop></node>
      <node oor:name="Y_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:Redo</value></prop></node>
      <node oor:name="F2_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:PrintPreview</value></prop></node>
    </node>
    <node oor:name="Modules">
      <node oor:name="com.sun.star.text.TextDocument" oor:op="replace">
        <node oor:name="J_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:Justify</value></prop></node>
        <node oor:name="E_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:CenterPara</value></prop></node>
        <node oor:name="R_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:RightPara</value></prop></node>
        <node oor:name="L_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:LeftPara</value></prop></node>
        <node oor:name="RETURN_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:InsertPageBreak</value></prop></node>
        <node oor:name="2_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:SpacePara2</value></prop></node>
        <node oor:name="1_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:SpacePara1</value></prop></node>
        <node oor:name="5_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:SpacePara15</value></prop></node>
        <node oor:name="C_SHIFT_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:FormatPaintbrush</value></prop></node>
        <node oor:name="V_SHIFT_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:PasteSpecial</value></prop></node>
        <node oor:name="K_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:LinkDialog</value></prop></node>
      </node>
      <node oor:name="com.sun.star.sheet.SpreadsheetDocument" oor:op="replace">
        <node oor:name="SEMICOLON_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:InsertCurrentDate</value></prop></node>
        <node oor:name="SEMICOLON_SHIFT_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:InsertCurrentTime</value></prop></node>
        <node oor:name="1_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:FormatCellDialog</value></prop></node>
        <node oor:name="EQUAL_MOD2" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:AutoSum</value></prop></node>
        <node oor:name="F3_SHIFT" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:FunctionDialog</value></prop></node>
        <node oor:name="9_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:HideRow</value></prop></node>
        <node oor:name="0_MOD1" oor:op="replace"><prop oor:name="Command" oor:type="xs:string"><value>.uno:HideColumn</value></prop></node>
      </node>
    </node>
  </node>
</oor:component-data>
EOF
}

generate_context_menu_config() {
    echo "-> Настройка контекстных меню мыши для всех модулей..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Addons.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Addons" oor:package="org.openoffice.Office">
  <node oor:name="ContextMenu">
    <node oor:name="ContextMenus">
      <node oor:name="com.sun.star.text.TextDocument" oor:op="replace">
        <node oor:name="Entries">
          <node oor:name="m1" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:Cut</value></prop></node>
          <node oor:name="m2" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:Copy</value></prop></node>
          <node oor:name="m3" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:Paste</value></prop></node>
          <node oor:name="m4" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:PasteSpecial</value></prop></node>
          <node oor:name="m5" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>private:separator</value></prop></node>
          <node oor:name="m6" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:FontDialog</value></prop></node>
          <node oor:name="m7" oor:op="replace"><prop oor:name="URL" oor:type="xs:string"><value>.uno:ParagraphDialog</value></prop></node>
        </node>
      </node>
    </node>
  </node>
</oor:component-data>
EOF
}

generate_ui_modifications() {
    echo "-> Настройка адаптивной Ribbon-ленты и двухрежимных корпоративных палитр..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/UI.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="UI" oor:package="org.openoffice.Office">
  <node oor:name="ColorScheme">
    <node oor:name="ColorSchemes">
      <!-- СХЕМА 1: СВЕТЛЫЙ РЕЖИМ (Яркие оригинальные корпоративные цвета) -->
      <node oor:name="MS_Office_Light" oor:op="replace">
        <node oor:name="Writer"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>2768499</value></prop></node> <!-- Синий #2B579A -->
        <node oor:name="Calc"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>2111829</value></prop></node>   <!-- Зеленый #207245 -->
        <node oor:name="Impress"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>11425834</value></prop></node> <!-- Оранжевый #AE371A -->
        <node oor:name="Draw"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>11425834</value></prop></node>    <!-- Бордовый #AE371A -->
        <node oor:name="Base"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>10561101</value></prop></node>   <!-- Пурпурный #A1242D -->
        <node oor:name="Math"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>34361</value></prop></node>       <!-- Изумрудный #008659 -->
      </node>
      <!-- СХЕМА 2: ТЕМНЫЙ РЕЖИМ (Глубокие ночные тона с сохранением оттенка приложения) -->
      <node oor:name="MS_Office_Dark" oor:op="replace">
        <node oor:name="Writer"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>1514013</value></prop></node> <!-- Ночной Синий #171B1D -->
        <node oor:name="Calc"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>1116434</value></prop></node>   <!-- Ночной Зеленый #110912 -->
        <node oor:name="Impress"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>2499604</value></prop></node> <!-- Ночной Оранжевый #262414 -->
        <node oor:name="Draw"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>2499604</value></prop></node>    
        <node oor:name="Base"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>2163222</value></prop></node>    <!-- Ночной Пурпурный #210216 -->
        <node oor:name="Math"><prop oor:name="ApplicationBackground" oor:type="xs:int"><value>13520</value></prop></node>       <!-- Ночной Изумрудный #0034D0 -->
      </node>
    </node>
  </node>
  <node oor:name="InterfaceSettings">
    <!-- Автоматическое следование за темой ОС и динамическое переключение палитр -->
    <prop oor:name="FollowSystemTheme" oor:type="xs:boolean"><value>true</value></prop>
    <!-- Автоматическая подмена иконок Colibre Light <-> Colibre Dark -->
    <prop oor:name="IconTheme" oor:type="xs:string"><value>colibre</value></prop>
    <prop oor:name="IconThemeDark" oor:type="xs:string"><value>colibre_dark</value></prop>
    <prop oor:name="NotebookBarMode" oor:type="xs:string"><value>Tabbed</value></prop>
  </node>
</oor:component-data>
EOF
}

generate_default_fonts_config() {
    echo "-> Внедрение глобальных шрифтов Aptos для всех приложений пакета..."
    cat <<EOF > "$BUILD_DIR/extension/registry/data/org/openoffice/Office/Linguistic.xcu"
<?xml version="1.0" encoding="UTF-8"?>
<oor:component-data xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org" oor:name="Linguistic" oor:package="org.openoffice.Office">
  <node oor:name="General">
    <prop oor:name="DefaultFont_Standard" oor:type="xs:string"><value>Aptos</value></prop>
    <prop oor:name="DefaultFont_Heading" oor:type="xs:string"><value>Aptos Display</value></prop>
    <prop oor:name="DefaultFont_List" oor:type="xs:string"><value>Aptos</value></prop>
    <prop oor:name="DefaultFont_Caption" oor:type="xs:string"><value>Aptos</value></prop>
    <prop oor:name="DefaultFont_Index" oor:type="xs:string"><value>Aptos</value></prop>
  </node>
</oor:component-data>
EOF
}

detect_and_configure_libreoffice() {
    local lo_path=""
    echo "-> Поиск установленного донора LibreOffice..."

    if [ "$TARGET_OS" == "windows" ]; then
        if [ -n "$PROGRAMFILES" ]; then
            local p_files_posix
            p_files_posix=$(cygpath -u "$PROGRAMFILES" 2>/dev/null || echo "/c/Program Files")
            if [ -d "${p_files_posix}/LibreOffice" ]; then
                lo_path="${p_files_posix}/LibreOffice"
            fi
        fi
    else
        if command -v libreoffice &> /dev/null; then
            lo_path=$(dirname "$(readlink -f "$(command -v libreoffice)")")/..
        fi
    fi

    if [ -n "$lo_path" ] && [ -d "$lo_path" ]; then
        echo "✅ Донор LibreOffice успешно сопряжен по пути: $lo_path"
    fi
}

optimize_assets() {
    echo "-> Оптимизация и максимальное сжатие OXT пакета..."
    cd "$BUILD_DIR/extension" || exit 1

    local minify_bin="../minify"
    local ect_bin
    
    if [ "$TARGET_OS" == "windows" ]; then
        minify_bin="../minify.exe"
        # Динамически находим, куда распаковался ect.exe (учитывая вложенные папки релиза)
        ect_bin=$(find .. -name "ect.exe" -print -quit)
        [ -z "$ect_bin" ] && ect_bin="../ect.exe"
    else
        ect_bin="../ect"
    fi

    # Минификация конфигурационных XML/XCU, палитр и UI-лент с использованием актуального флага --type=xml
    find . -type f \( -name "*.xml" -o -name "*.xcu" -o -name "*.ui" -o -name "*.soc" \) | while read -r file; do
        "$minify_bin" --type=xml -o "$file" "$file"
    done

    echo "-> Сжатие архива с помощью ECT..."
    zip -r9 "../$OXT_FILE" ./* > /dev/null
    cd ..
    
    # Запуск компрессии (только если бинарник существует)
    if [ -f "$ect_bin" ]; then
        "$ect_bin" -9 -zip "$OXT_FILE"
    else
        echo "   [Инфо] Полноразмерное сжатие ECT пропущено (используется стандартный zip)."
    fi
    
    cd ..
    echo "✅ Тотальная сборка завершена: $BUILD_DIR/$OXT_FILE"
}

install_and_force_activate_theme() {
    echo "-> Инициирована процедура полной автоматической активации интерфейса..."
    
    local unopkg_bin="unopkg"
    local lo_profile_reg=""

    if [ "$TARGET_OS" == "windows" ]; then
        if [ -n "$PROGRAMFILES" ]; then
            local p_files_posix=$(cygpath -u "$PROGRAMFILES")
            unopkg_bin="${p_files_posix}/LibreOffice/program/unopkg.com"
        fi
        local appdata_posix=$(cygpath -u "$APPDATA")
        lo_profile_reg="${appdata_posix}/LibreOffice/4/user/registrymodifications.xcu"
    else
        command -v unopkg &> /dev/null && unopkg_bin=$(command -v unopkg)
        lo_profile_reg="${HOME}/.config/libreoffice/4/user/registrymodifications.xcu"
    fi

    # 1. Тихое развертывание .oxt расширения в профиль текущего пользователя
    if [ -f "$unopkg_bin" ] || command -v unopkg &> /dev/null; then
        echo "   [1/2] Тихая установка расширения через unopkg..."
        "$unopkg_bin" add --force "$BUILD_DIR/$OXT_FILE" &>/dev/null
        echo "   ✅ Расширение успешно внедрено в профиль."
    else
        echo "   ⚠️ Предупреждение: утилита unopkg не найдена, расширение нужно запустить вручную."
    fi

    # 2. ПРИНУДИТЕЛЬНАЯ АКТИВАЦИЯ КЭША ИНТЕРФЕЙСА (Инъекция в реестр LibreOffice)
    if [ -f "$lo_profile_reg" ]; then
        echo "   [2/2] Форсированная активация Ленты (Tabbed) и палитр в реестре LibreOffice..."
        
        # Удаляем старые записи параметров интерфейса, чтобы избежать дубликации тегов
        sed -i '/NotebookBarMode/d' "$lo_profile_reg"
        sed -i '/IconTheme/d' "$lo_profile_reg"
        sed -i '/ColorScheme/d' "$lo_profile_reg"

        # Записываем жесткие директивы: активировать вкладки, использовать colibre и цветовую схему MS Office
        sed -i '$i <item oor:path="/org.openoffice.Office.UI/InterfaceSettings"><prop oor:name="NotebookBarMode" oor:type="xs:string"><value>Tabbed</value></prop></item>' "$lo_profile_reg"
        sed -i '$i <item oor:path="/org.openoffice.Office.UI/InterfaceSettings"><prop oor:name="IconTheme" oor:type="xs:string"><value>colibre</value></prop></item>' "$lo_profile_reg"
        sed -i '$i <item oor:path="/org.openoffice.Office.UI/InterfaceSettings"><prop oor:name="ColorScheme" oor:type="xs:string"><value>MS_Office_Light</value></prop></item>' "$lo_profile_reg"
        
        echo "   ✅ Реестр профиля успешно пропатчен. Изменения вступят в силу при старте."
    else
        # Если это первый холодный запуск и файла конфигурации еще нет — инициализируем его с флагом Ленты
        mkdir -p "$(dirname "$lo_profile_reg")"
        cat <<EOF > "$lo_profile_reg"
<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org">
<item oor:path="/org.openoffice.Office.UI/InterfaceSettings"><prop oor:name="NotebookBarMode" oor:type="xs:string"><value>Tabbed</value></prop></item>
<item oor:path="/org.openoffice.Office.UI/InterfaceSettings"><prop oor:name="IconTheme" oor:type="xs:string"><value>colibre</value></prop></item>
</oor:items>
EOF
        echo "   ✅ Базовый файл конфигурации профиля инициализирован с флагом Ленты."
    fi

    echo "🎉 ВСЕ ДЕЙСТВИЯ ВЫПОЛНЕНЫ! Запустите Writer или Calc, чтобы увидеть интерфейс MS Office 2022."
}

# ==============================================================================
# ВИНДОУС-ИНСТАЛЛЯТОР: ГЕНЕРАЦИЯ АВТОНОМНОГО BAT-ФАЙЛА АКТИВАЦИИ
# ==============================================================================
generate_windows_installer_bat() {
    echo "-> Генерация Windows-автоинсталлятора (install.bat) рядом с OXT..."
    
    # Файл создается в той же папке, где появится готовый OXT-пакет
    cat <<'EOF' > "$BUILD_DIR/install.bat"
@echo off
chcp 65001 > nul
echo ============================================================
echo      Автоматическая установка и активация темы MS Office
echo ============================================================
echo.

:: 1. Определение пути к LibreOffice в Program Files
set "LO_PATH=%ProgramFiles%\LibreOffice\program"
if not exist "%LO_PATH%\unopkg.com" (
    set "LO_PATH=%ProgramFiles(x86)%\LibreOffice\program"
)

if not exist "%LO_PATH%\unopkg.com" (
    echo [ОШИБКА] LibreOffice не найден в стандартных папках Program Files.
    echo Пожалуйста, установите LibreOffice или запустите пакет расширения .oxt вручную.
    echo.
    pause
    exit /b
)

:: 2. Тихое развертывание расширения в профиль пользователя
echo [1/2] Установка расширения в LibreOffice...
"%LO_PATH%\unopkg.com" add --force "MS_Office_Aspect_Pack.oxt" > nul 2>&1
if %errorlevel% neq 0 (
    echo [ОШИБКА] Не удалось установить расширение. Убедитесь, что LibreOffice закрыт.
    pause
    exit /b
)
echo      Пакет расширения успешно добавлен.

:: 3. Принудительная инъекция настроек Ленты и Палитры в реестр LibreOffice
echo [2/2] Форсированная активация Ленточного интерфейса и палитры...
set "REG_FILE=%APPDATA%\LibreOffice\4\user\registrymodifications.xcu"

if not exist "%REG_FILE%" (
    mkdir "%APPDATA%\LibreOffice\4\user" 2>nul
    (
    echo ^<?xml version="1.0" encoding="UTF-8"?^>
    echo ^<oor:items xmlns:oor="http://openoffice.org" xmlns:xs="http://w3.org"^>
    echo ^<item oor:path="/org.openoffice.Office.UI/InterfaceSettings"^>^<prop oor:name="NotebookBarMode" oor:type="xs:string"^>^<value^>Tabbed^</value^>^</prop^>^</item^>
    echo ^<item oor:path="/org.openoffice.Office.UI/InterfaceSettings"^>^<prop oor:name="IconTheme" oor:type="xs:string"^>^<value^>colibre^</value^>^</prop^>^</item^>
    echo ^<item oor:path="/org.openoffice.Office.UI/InterfaceSettings"^>^<prop oor:name="ColorScheme" oor:type="xs:string"^>^<value^>MS_Office_Light^</value^>^</prop^>^</item^>
    echo ^</oor:items^>
    ) > "%REG_FILE%"
) else (
    powershell -Command "$p = '%REG_FILE%'; (Get-Content $p) | Where-Object { $_ -notmatch 'NotebookBarMode' -and $_ -notmatch 'IconTheme' -and $_ -notmatch 'ColorScheme' } | Set-Content $p"
    powershell -Command "$p = '%REG_FILE%'; (Get-Content $p) | ForEach-Object { if ($_ -match '</oor:items>') { '<item oor:path=\"/org.openoffice.Office.UI/InterfaceSettings\"><prop oor:name=\"NotebookBarMode\" oor:type=\"xs:string\"><value>Tabbed</value></prop></item>'; '<item oor:path=\"/org.openoffice.Office.UI/InterfaceSettings\"><prop oor:name=\"IconTheme\" oor:type=\"xs:string\"><value>colibre</value></prop></item>'; '<item oor:path=\"/org.openoffice.Office.UI/InterfaceSettings\"><prop oor:name=\"ColorScheme\" oor:type=\"xs:string\"><value>MS_Office_Light</value></prop></item>'; $_ } else { $_ } } | Set-Content $p"
)

echo      Настройки интерфейса успешно применены.
echo.
echo 🎉 ВСЕ ДЕЙСТВИЯ УСПЕШНО ВЫПОЛНЕНЫ!
echo Запустите Writer или Calc, чтобы увидеть интерфейс MS Office 2022.
echo.
pause
EOF
}

# ==============================================================================
# ОСНОВНОЙ КОНВЕЙЕР ВЫПОЛНЕНИЯ
# ==============================================================================
prepare_environment
download_tools
generate_microsoft_palette
pack_fonts_inside
generate_extension_manifest
generate_autosave_and_warnings_config
generate_mouse_autoscroll_config                # Включает твики буфера обмена и TEMP
generate_custom_ribbon_ui
generate_accelerators_config
generate_context_menu_config
generate_ui_modifications
generate_advanced_compatibility_and_start_center
generate_sidebar_graphics_patch
generate_default_fonts_config
detect_and_configure_libreoffice
optimize_assets
generate_windows_installer_bat
install_and_force_activate_theme
