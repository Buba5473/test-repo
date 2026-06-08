# `pdfmrc` — Smart Mixed Raster Content (MRC) PDF Compressor

## 🇷🇺 RU | Русский язык

### Описание проекта
`pdfmrc` — это высокоэффективная, кроссплатформенная и полностью автономная консольная утилита (написанная на C++17 с использованием официального движка MuPDF 1.27.2), предназначенная для интеллектуального сжатия сканированных PDF-документов по технологии **MRC (Mixed Raster Content)**. 

При обработке документа утилита разделяет каждую страницу на три независимых слоя: высокоточную маску текста (контуры символов), передний план (цвет текста) и задний план (фоновая подложка). Текст бинаризуется и сжимается алгоритмами без потерь качества, а фоновые слои подвергаются экстремальной компрессии. Это позволяет уменьшать размер сканированных документов, рукописей и книг в несколько раз, полностью сохраняя бритвенную резкость букв и читаемость.

### Ключевые особенности и алгоритмы
* **Адаптивная сегментация Сауволы**: Заменяет классический метод Оцу. Порог бинаризации рассчитывается для каждого пикселя индивидуально на основе локального среднего и стандартного отклонения в скользящем окне. Алгоритм устойчив к неравномерному освещению, градиентам, замятиям бумаги и теням на страницах.
* **Динамический расчет окна под DPI**: Геометрический размер скользящего окна Сауволы и радиус морфологического фильтра шума рассчитываются «на лету» в зависимости от реального физического разрешения (DPI) обрабатываемой страницы. Защищает мелкие шрифты от «выпотрошенных» букв и размытия штрихов.
* **Многопоточное ядро (ThreadPool)**: Страницы PDF обрабатываются в параллельных потоках независимо, утилизируя всю доступную мощность процессора. Реализован флаг `--threads` для гибкого ограничения количества используемых ядер.
* **Низкоуровневая инжекция XObject (API 1.27.2)**: Полная интеграция слоев MRC в структуру MuPDF через нативный C API. Программа генерирует объекты `XObject`, пакует графику по алгоритмам Flate/DCT и аппаратно модифицирует Content Stream страницы.
* **Веб-линеаризация**: При сохранении готового PDF-файла утилита применяет Fast Web View, оптимизируя структуру документа для мгновенного открытия страниц в веб-браузерах по сети.
* **Двуязычный интерфейс и UTF-8**: Полностью автоматическое переключение языков (RU/EN) на основе системной локали ОС. Проблема с отображением кириллицы в консоли Windows полностью решена за счет сквозного UTF-8.
* **Нативная интеграция с Windows 10**: 
  * Приложение использует современный **Windows 10 Task Dialog API** для интерактивного выбора параметров сжатия.
  * Флаг `--install-sendto` создает системный ярлык в папке Проводника Windows "Отправить" (`CSIDL_SENDTO`), позволяя сжимать файлы в один клик мыши.
  * Вшит системный манифест, иконка, метаданные версии и фейковая цифровая подпись Authenticode.
* **Интеграция в окружения Linux**:
  * Автоматическая детекция и установка расширений контекстного меню для **GNOME (Nautilus Scripts), KDE Plasma (Dolphin Service Menus), XFCE (Thunar Actions) и Cinnamon (Nemo Actions)**.
* **Нулевой рантайм-депенденси**: Сборка производится со статической линковкой CRT-библиотек (`-static`). Итоговый бинарник не требует внешних `.dll`, пакетов MSVC или библиотек выполнения MSYS2.

### Структура проекта
```text
pdfmrc/
├── main.cpp                        # Монолитный исходный код утилиты (C++17)
├── build.sh                        # Очищенный автономный конвейер сборки через GNU Make
├── resources.rc                    # Файл ресурсов Windows (Иконка, Версия, Манифест)
├── manifest.xml                    # Манифест совместимости для Windows 10/11 (Task Dialog API)
├── icon.ico                        # Иконка приложения для Windows (256x256)
├── icon.png                        # Иконка приложения для Linux (256x256 PNG)
└── README.md                       # Данная двуязычная документация проекта (RU/EN)
```

### Сборка проекта
Для сборки под Windows требуется среда **MSYS2 UCRT64**.

1. Откройте консоль MSYS2 UCRT64.
2. Перейдите в папку с проектом и запустите скрипт сборки:
   ```bash
   ./build.sh
   ```
   *Скрипт автоматически проверит наличие необходимых пакетов (`make`, `curl`, `jq`, `g++`, `openssl`, `tar`), предложит установить недостающие, создаст изолированную папку `pdfmrc_workspace`, скачает официальный стабильный Source Tarball релиза MuPDF 1.27.2 напрямую с официального сайта mupdf.com (минуя проблемы Git-сабмодулей и NTFS-симлинков), удалит мусорные шрифты CJK и документацию, после чего скомпилирует полностью автономный и подписанный файл `./pdfmrc.exe` под AMD64 / Windows 10.*

3. Для сборки под Linux (или кросс-компиляции):
   ```bash
   ./build.sh --target linux
   ```
   *Скрипт автоматически обнаружит смену платформы, полностью сбросит кэш и старые объектные файлы компилятора во избежание конфликтов линковки архитектур и соберет бинарник `./pdfmrc`.*

### Использование
Запустите программу без аргументов (или с флагом `-h` / `--help`) для вывода интерактивной справки:
```bash
./pdfmrc.exe
```

**Примеры команд:**
* Стандартное сжатие (по умолчанию 150 DPI, уровень сжатия 3):
  ```bash
  ./pdfmrc.exe document.pdf
  ```
* Ручная настройка параметров компрессии, разрешения и потоков:
  ```bash
  ./pdfmrc.exe document.pdf --dpi 300 --level 5 --threads 4
  ```
* Установка интеграции в Проводник / Контекстные меню Linux:
  ```bash
  ./pdfmrc.exe --install-sendto
  ```
* Полная деинсталляция и очистка системных следов (ярлыков, ключей реестра):
  ```bash
  ./pdfmrc.exe --uninstall
  ```
* Тотальная зачистка окружения сборщика, удаление кэшей и временных папок компиляции:
  ```bash
  ./build.sh --clean
  ```

---

## 🇺🇸 EN | English

### Project Description
`pdfmrc` is a highly efficient, cross-platform, and fully standalone command-line utility (written in C++17 utilizing the official MuPDF 1.27.2 engine) designed for intelligent scanned PDF document compression using **MRC (Mixed Raster Content)** technology.

The utility splits each PDF page into three independent layers: a high-precision text mask (character contours), a foreground (text color), and a background (underlying page texture). The text is binarized and compressed using lossless algorithms, while the background layers undergo extreme compression. This allows for reducing the size of scanned documents, manuscripts, and books multiple times over without sacrificing text sharpness and readability.

### Key Features and Algorithms
* **Sauvola Adaptive Segmentation**: Replaces the classic Otsu method. The binarization threshold is calculated individually for each pixel based on the local mean and standard deviation within a sliding window. The algorithm is highly resilient to uneven illumination, gradients, paper folds, and page shadows.
* **Dynamic Window Calculation via DPI**: The geometric size of Sauvola's sliding window and the radius of the morphological noise filter are calculated on-the-fly based on the actual physical resolution (DPI) of the processed page. This protects small fonts from "hollowed-out" characters and line blurriness.
* **Multi-threaded Core (ThreadPool)**: PDF pages are processed concurrently in independent threads, maximizing the utilization of all available CPU power. Includes a `--threads` flag for customizable CPU core allocation limits.
* **Low-level XObject Injection (API 1.27.2)**: Built-in custom integration of MRC layers into the MuPDF structure via native C API. The program generates low-level `XObject` primitives, packs graphics using Flate/DCT algorithms, and directly modifies the page Content Stream.
* **Web Linearization**: When saving the final PDF, the utility applies Fast Web View, optimizing the document structure for instantaneous page opening inside web browsers over the network.
* **Bilingual Interface & UTF-8**: Seamless, fully automatic language switching (RU/EN) based on the OS system locale. The Cyrillic character encoding issues in the Windows console are fully resolved by using end-to-end UTF-8.
* **Native Windows 10 Integration**:
  * The application utilizes the modern **Windows 10 Task Dialog API** for an interactive compression parameters configuration dialogue.
  * The `--install-sendto` flag generates a system shortcut within the Windows Explorer "Send To" directory (`CSIDL_SENDTO`), enabling one-click file compression.
  * Built-in embedded icon, side-by-side manifest, version metadata, and fake Authenticode digital signature.
* **Linux Environment Integration**:
  * Automatic detection and deployment of desktop context menu entry extensions for **GNOME (Nautilus Scripts), KDE Plasma (Dolphin Service Menus), XFCE (Thunar Actions), and Cinnamon (Nemo Actions)**.
* **Zero Runtime Dependencies**: Compilation is carried out with static linking of CRT libraries (`-static`). The resulting binary requires no external `.dll` files, MSVC redistributable packages, or MSYS2 runtime dependencies.

### Project Structure
```text
pdfmrc/
├── main.cpp                        # Monolithic source code of the utility (C++17)
├── build.sh                        # Clean standalone build pipeline utilizing GNU Make
├── resources.rc                    # Windows resource script (Icon, Product Version, Manifest)
├── manifest.xml                    # Side-by-side compatibility manifest for Windows 10/11
├── icon.ico                        # Application icon for Windows (256x256)
├── icon.png                        # Application icon for Linux (256x256 PNG)
└── README.md                       # This bilingual project documentation (RU/EN)
```

### Building the Project
For building on Windows, the **MSYS2 UCRT64** environment is required.

1. Open the MSYS2 UCRT64 console.
2. Navigate to the project directory and run the build script:
   ```bash
   ./build.sh
   ```
   *The script will automatically check for required packages (`make`, `curl`, `jq`, `g++`, `openssl`, `tar`), prompt you to install any missing ones, create an isolated `pdfmrc_workspace` folder, download the official stable MuPDF 1.27.2 Source Tarball directly from mupdf.com (bypassing Git submodule blocks and NTFS symlink restrictions), strip CJK fonts and documentation bloat, and compile a fully standalone, signed `./pdfmrc.exe` binary optimized for AMD64 / Windows 10.*

3. To build for Linux (or cross-compile):
   ```bash
   ./build.sh --target linux
   ```
   *The script will automatically detect the platform switch, fully wipe the CMake/Make cache to prevent object file conflicts between different architectures, and compile the native `./pdfmrc` binary.*

### Usage
Run the application without any arguments (or with the `-h` / `--help` flags) to display the interactive help screen:
```bash
./pdfmrc.exe
```

**Command Examples:**
* Standard compression (Default: 150 DPI, Compression Level 3):
  ```bash
  ./pdfmrc.exe document.pdf
  ```
* Manual configuration of compression parameters, resolution, and threads:
  ```bash
  ./pdfmrc.exe document.pdf --dpi 300 --level 5 --threads 4
  ```
* Installing Explorer context / Linux DE context menu integration:
  ```bash
  ./pdfmrc.exe --install-sendto
  ```
* Full uninstallation and registry/shortcut footprint cleanup:
  ```bash
  ./pdfmrc.exe --uninstall
  ```
* Complete cleanup of the builder workspace, removing objects, caches, and source trees:
  ```bash
  ./build.sh --clean
  ```
