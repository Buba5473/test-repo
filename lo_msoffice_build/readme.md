# 🇷🇺 Набор автоматической трансформации LibreOffice в Microsoft Office 2022+

Этот проект содержит универсальный кроссплатформенный Bash-скрипт (`transform.sh`), предназначенный для глубокой автоматической модификации интерфейса и поведения офисного пакета **LibreOffice 26.2+** с целью приведения его к стандарту **Microsoft Office 2022 / 365**.

Скрипт полностью автономен, автоматически скачивает необходимые инструменты и собирает ультра-сжатый пакет расширения (`.oxt`), после чего принудительно активирует его в системе.

## 🚀 Ключевые возможности

| Функция | Описание |
| :--- | :--- |
| **Тотальное брендирование** | Фон рабочей области каждого приложения перекрашивается в его фирменный цвет MS Office (синий для Word, зеленый для Excel, оранжевый для PowerPoint и т.д.). |
| **Адаптивная тема** | Автоматическое переключение между Светлым (`MS_Office_Light`) и Темным (`MS_Office_Dark`) режимами вслед за настройками ОС. |
| **Шрифты Aptos** | Автоматическое скачивание гарнитуры Aptos, интеграция внутрь `.oxt` и написание Python-макроса, который устанавливает шрифты прямо в ОС без прав администратора. |
| **Интеграция Ribbon (Ленты)** | Автоматическая генерация и принудительное включение вкладчатого (ленточного) интерфейса. |
| **Горячие клавиши (Shortcuts)** | Полное переопределение сочетаний клавиш во всех модулях (например, `Ctrl+Enter` для разрыва страницы, `Alt+=` для автосуммы, `Ctrl+M` для нового слайда). |
| **Контекстные меню** | Очистка и перестройка меню правой кнопки мыши (буфер обмена сверху, затем параметры шрифтов и ячеек). |
| **Форматы по умолчанию** | Новые документы автоматически и молча сохраняются в форматы `.docx`, `.xlsx` и `.pptx`. |
| **Тихое отключение окон** | Полное отключение раздражающих предупреждений LibreOffice о сохранении в сторонние форматы OXML. |
| **Умный буфер обмена** | Данные принудительно кэшируются в форматах HTML/RTF и сохраняются при закрытии приложений, исключая пропажу таблиц при копировании. |
| **Защита сетевых дисков** | Перенос lock-файлов и резервных копий во временный локальный каталог `TEMP` пользователя, что предотвращает зависание сетевых документов. |
| **Твики мыши** | Отключение UNIX-вставки текста по клику колесиком. Взамен включается плавный автоскроллинг. |
| **Фирменная палитра** | Генерация и интеграция файла `microsoft.soc` со стандартными HEX-цветами MS Office для текста и заливки. |
| **Совместимость с VBA** | Автоматическая принудительная активация прозрачного режима выполнения макросов Excel VBA внутри Calc. |
| **Патч Sidebar (Боковой панели)** | Интеграция кастомных векторных SVG-иконок для вкладок свойств, стилей и галереи в дизайне Microsoft Office. |
| **Брендирование Start Center** | Окрашивание стартового экрана LibreOffice в глубокий синий цвет с белым текстом в гайдлайнах Office 365. |
| **Максимальное сжатие** | Все XML, XCU, UI, SOC и SVG ресурсы проходят глубокую минификацию через `tdewolff/minify` с использованием актуального флага `--type=xml`. Финальный пакет сжимается через `Efficient-Compression-Tool -9` (ECT). |
| **100% Автоактивация** | Скрипт сам устанавливает расширение через `unopkg` и патчит файл `registrymodifications.xcu`, чтобы лента и цвета включились мгновенно без ручной настройки. |
| **Генерация Windows-инсталлятора** | Скрипт автоматически создает автономный файл `install.bat` рядом с `.oxt` для быстрой установки на компьютерах обычных пользователей в один клик. |

## 🛠️ Зависимости и требования

Скрипт проверяет наличие зависимостей автоматически при старте и предлагает установить их.

* **Windows:** Окружение **MSYS2 (UCRT64)**. Необходимы пакеты `curl`, `unzip`, `zip`, `wget`, `jq` (устанавливаются через `pacman`). Права Администратора **не требуются**.
* **Linux:** Терминал Bash. Необходимы пакеты `curl`, `unzip`, `zip`, `wget`, `jq` (устанавливаются через `apt`). Права root/sudo требуются только в том случае, если в системе не установлены базовые утилиты.

## 📦 Инструкция по запуску

Для работы нужен только один файл — `transform.sh`. Поместите его в любую рабочую директорию. Перед началом сборки папка `./lo_msoffice_build` зачищается "под ноль".

### Запуск в Windows (MSYS2 UCRT64)
Сборка под Windows выполняется по умолчанию при запуске без аргументов командной строки:
```bash
chmod +x transform.sh
./transform.sh
```

### Запуск в Linux
Сборка под Linux запускается с отдельным аргументом `-l` или `--linux`:
```bash
chmod +x transform.sh
./transform.sh --linux
```

## 📂 Структура генерируемого проекта

В процессе работы скрипт создает изолированную структуру, которая затем компилируется в OXT-архив:

```text
📂 lo_msoffice_build/
├── 📄 ect.exe (или ect)           # Бинарник компрессора (скачивается с GitHub Latest Release)
├── 📄 minify.exe (или minify)     # Бинарник минификатора (скачивается с GitHub Latest Release)
├── 📄 install.bat                 # Автономный установщик темы в один клик для конечных пользователей Windows
├── 📄 MS_Office_Aspect_Pack.oxt   # Итоговое ультра-сжатое расширение готовое к переносу
└── 📂 extension/                  # Исходная структура расширения перед сжатием
    ├── 📄 description.xml         # Метаданные расширения
    ├── 📂 META-INF/
    │   └── 📄 manifest.xml        # Манифест регистрации файлов и Python-скрипта
    ├── 📂 ui/
    │   └── 📄 notebookbar.ui      # Минифицированная структура Ribbon-ленты
    ├── 📂 palette/
    │   └── 📄 microsoft.soc       # Фирменная палитра цветов Microsoft
    ├── 📂 fonts/
    │   └── 📄 [Aptos Fonts...]    # Оригинальные TTF-шрифты
    ├── 📂 Scripts/python/
    │   └── 📄 font_installer.py   # UNO-макрос автоматической установки шрифтов в ОС
    ├── 📂 res/cmd/                # Векторный патч кастомных SVG-иконок боковой панели Sidebar
    └── 📂 registry/data/org/openoffice/Office/
        ├── 📄 Accelerators.xcu    # Конфигурация горячих клавиш
        ├── 📄 Addons.xcu          # Переопределение контекстных меню мыши
        ├── 📄 Calc.xcu            # Твики совместимости с Excel VBA макросами
        ├── 📄 Common.xcu          # Твики автоскролла, HTML-буфера обмена и локальных путей TEMP
        ├── 📄 Jobs.xcu            # Триггер для автозапуска Python-макроса
        ├── 📄 Linguistic.xcu      # Назначение Aptos шрифтом по умолчанию
        ├── 📄 Setup.xcu           # Параметры автосохранения DOCX/XLSX и скрытия предупреждений
        └── 📄 UI.xcu              # Двухрежимная палитра окон приложений и стили Start Center
```

***

# 🇬🇧 LibreOffice to Microsoft Office 2022+ Ultimate Transformation Suite

This project contains a universal, cross-platform Bash script (`transform.sh`) designed for deep automated modification of the **LibreOffice 26.2+** interface and behavior to match the **Microsoft Office 2022 / 365** standard.

The script is fully autonomous: it automatically fetches required optimization tools and fonts from remote repositories, compiles an ultra-compressed extension package (`.oxt`), and instantly activates it on the target system.

## 🚀 Key Features

| Feature | Description |
| :--- | :--- |
| **Total Branding** | The application workspace background is recolored to its corporate MS Office brand color (Blue for Word, Green for Excel, Orange for PowerPoint, etc.). |
| **Adaptive Theme** | Seamlessly toggles between Light (`MS_Office_Light`) and Dark (`MS_Office_Dark`) application backgrounds following the OS system settings. |
| **Aptos Fonts Inside** | Downloads the Aptos font family, packs it directly into the `.oxt`, and uses an embedded Python UNO macro to install fonts into the OS without Admin/root privileges. |
| **Ribbon Integration** | Automatically generates and forces the tabbed (Ribbon-style) layout configuration. |
| **Shortcut Remapping** | Complete key remapping across all modules (e.g., `Ctrl+Enter` for page break, `Alt+=` for AutoSum, `Ctrl+M` for a new slide). |
| **Context Menus** | Cleans up and restructures the right-click mouse menus (Clipboard actions on top, followed by Font/Cell properties). |
| **Default OpenXML Formats** | Forces new documents to save directly into `.docx`, `.xlsx`, and `.pptx` by default. |
| **Silent Saving** | Permanently disables the annoying LibreOffice prompt warning about saving in non-ODF formats. |
| **Advanced Clipboard** | Forces clipboard data persistence on app exit and optimizes HTML/RTF caching for seamless table copying. |
| **Network Drive Protection** | Redirects backup and file-lock generation directly to the local user `TEMP` directory, preventing shared network document hangs. |
| **Mouse Enhancements** | Disables UNIX middle-click clipboard insertion. Smooth auto-scrolling via the mouse wheel is enabled instead. |
| **Corporate Palette** | Generates and integrates a `microsoft.soc` color file providing standard MS Office HEX colors for font and fill tasks. |
| **VBA Macro Support** | Forces hidden compatibility execution flags enabling Calc to transparently run native Excel VBA macros. |
| **Sidebar Graphic Patch** | Integrates customized vector SVG icon alternatives for properties, styles, and gallery sidebar tabs following MS Office design. |
| **Start Center Customization** | Recolors the default LibreOffice start splash hub background into an elegant dark-blue corporate layout matching Office 365. |
| **Extreme Optimization** | All XML, XCU, UI, SOC, and SVG assets are deeply minified using `tdewolff/minify` with the modern `--type=xml` flag syntax. The final bundle is compressed via `Efficient-Compression-Tool -9` (ECT). |
| **100% Auto-Activation** | Quietly deploys the extension using `unopkg` and patches the user's `registrymodifications.xcu` file so the Ribbon and colors activate instantly without manual setup. |
| **One-Click Windows Installer** | Automatically builds a portable standalone `install.bat` next to the `.oxt` package for rapid corporate user workstation deployments. |

## 🛠️ Dependencies & Requirements

The script checks for missing utilities upon execution and prompts for automated installation.

* **Windows:** **MSYS2 (UCRT64)** environment. Requires `curl`, `unzip`, `zip`, `wget`, `jq` (installed via `pacman`). Administrator privileges are **NOT** required.
* **Linux:** Bash terminal. Requires `curl`, `unzip`, `zip`, `wget`, `jq` (installed via `apt`). Root/sudo access is only needed if basic system utilities are missing.

## 📦 Deployment & Execution

You only need a single file to build the project — `transform.sh`. Place it in any directory. The build folder `./lo_msoffice_build` is automatically wiped out before each run.

### Running on Windows (MSYS2 UCRT64)
Building for Windows is the default behavior when running the script without arguments:
```bash
chmod +x transform.sh
./transform.sh
```

### Running on Linux
Building for Linux requires passing the `-l` or `--linux` command-line argument:
```bash
chmod +x transform.sh
./transform.sh --linux
```

## 📂 Project Architecture

During execution, the script populates an isolated environment before compiling it into the final OXT bundle:

```text
📂 lo_msoffice_build/
├── 📄 ect.exe (or ect)           # Compressor binary (downloaded from GitHub Latest Release)
├── 📄 minify.exe (or minify)     # Minifier binary (downloaded from GitHub Latest Release)
├── 📄 install.bat                 # Autonomous one-click user theme deployment batch script for Windows
├── 📄 MS_Office_Aspect_Pack.oxt   # Ultra-compressed extension package ready for deployment
└── 📂 extension/                  # Source structure of the extension before building
    ├── 📄 description.xml         # Extension metadata
    ├── 📂 META-INF/
    │   └── 📄 manifest.xml        # File registry and Python macro manifest
    ├── 📂 ui/
    │   └── 📄 notebookbar.ui      # Minified Ribbon layout structure
    ├── 📂 palette/
    │   └── 📄 microsoft.soc       # Microsoft corporate color palette file
    ├── 📂 fonts/
    │   └── 📄 [Aptos Fonts...]    # Original TrueType font files
    ├── 📂 Scripts/python/
    │   └── 📄 font_installer.py   # UNO macro for non-admin system font deployment
    ├── 📂 res/cmd/                # Vector SVG sidebar custom command action icon overlays
    └── 📂 registry/data/org/openoffice/Office/
        ├── 📄 Accelerators.xcu    # Hotkey remapping configuration
        ├── 📄 Addons.xcu          # Mouse right-click context menu overrides
        ├── 📄 Calc.xcu            # Embedded Excel VBA engine macro triggers
        ├── 📄 Common.xcu          # Mouse tweaks, clipboard persistence, and local TEMP path configuration
        ├── 📄 Jobs.xcu            # Python macro execution lifecycle event triggers
        ├── 📄 Linguistic.xcu      # Global Aptos default font overrides
        ├── 📄 Setup.xcu           # Default OpenXML save formats and silent warnings config
        └── 📄 UI.xcu              # Adaptive light/dark application window background color themes and Start Center styles
```
