# Тема MS Office 2022 для OpenOffice 26.2+
# MS Office 2022 Theme for OpenOffice 26.2+

---

## [RU] РУССКИЙ ЯЗЫК

Пакет расширения (.OXT) для глубокой визуальной и функциональной оптимизации офисного пакета OpenOffice 26.2+ в стилистику Microsoft Office 2022 / 365. 

### Основные возможности
* **Ленточный интерфейс**: Автоматическое переключение на вкладчатый дизайн (ModernRibbon) со скрытием классического текстового меню.
* **Брендовая раскраска**: Принудительное окрашивание рабочих зон под фирменные цвета приложений Microsoft (Writer, Calc, Impress, Draw, Math, Base).
* **Адаптивность (Dark/Light)**: Автоматическая синхронизация со светлой или темной темой ОС при старте, а также возможность ручной фиксации режима через меню `Сервис -> Тема MS Office...`.
* **Полная замена иконок**: Интеграция графического пула Colibre (Светлый/Темный) из локального дистрибутива LibreOffice.
* **Шрифтовая экосистема**: Поддержка полного семейства шрифтов Microsoft Aptos. Интерфейс переводится на `Aptos Display`, формулы и макросы — на `Aptos Mono`, тексты с засечками — на `Aptos Serif`.
* **Горячие клавиши**: Переназначение ключевых комбинаций (Ctrl+F, Ctrl+H, Ctrl+K, F5) под привычные стандарты MS Office.
* **Экстремальное Rust/C++ сжатие**: Графика параллельно обрабатывается многопоточным оптимизатором `oxipng` (на Rust), а финальный OXT-контейнер прессуется утилитой `ect` (Efficient Compression Tool), что гарантирует минимально возможный размер файла.

### Системные требования и зависимости
Сборочный скрипт полностью автономен. Для работы требуются:
* **Windows**: Среда **MSYS2 (UCRT64)**. Путь к LibreOffice определяется автоматически через опрос реестра Windows.
* **Linux**: Стандартный терминал (поддержка `apt`, `dnf` или `pacman`).
* Установленный в системе **LibreOffice** (используется как донор графики).

### Инструкция по сборке

1. Откройте терминал (в Windows запустите **MSYS2 UCRT64**).
2. Перейдите в корневую папку проекта и выполните команду:
   ```bash
   make
   ```
   *Примечание: Скрипт проверит окружение. В Linux он установит утилиты через менеджер пакетов, а в Windows автоматически скачает последние стабильные релизы `minify`, `oxipng` и `ect` с GitHub прямо в папку сборки.*
3. Готовый оптимизированный пакет будет сохранен по пути: `build_output/msoffice_theme_2022.oxt`.

### Установка
1. **Системные шрифты**: Чтобы установить шрифты Aptos, Mono и Serif глобально для всей ОС, выполните:
   * В Windows: Запустите MSYS2 от имени Администратора и введите `make install-fonts`.
   * В Linux: Выполните `sudo make install-fonts`.
2. **Расширение**: Откройте OpenOffice, перейдите в `Сервис -> Управление расширениями -> Добавить` и выберите созданный файл `.oxt`. Перезапустите офис.

---

## [EN] ENGLISH

An extension package (.OXT) for deep visual and functional optimization of OpenOffice 26.2+ to match the Microsoft Office 2022 / 365 style.

### Key Features
* **Ribbon Interface**: Automatic activation of the modern tabbed layout (ModernRibbon) with the classic menu bar hidden.
* **Branded Coloring**: Enforced workspace background synchronization based on native application identities (Writer, Calc, Impress, Draw, Math, Base).
* **Adaptive Theme (Dark/Light)**: Automatic real-time synchronization with the OS color scheme on startup, featuring an option to lock the theme via `Tools -> MS Office Theme...`.
* **Full Icon Replacement**: Seamless integration of the Colibre icon sets (Light & Dark) extracted from the local LibreOffice installation.
* **Font Ecosystem**: Full Microsoft Aptos family deployment. Overrides the UI font with `Aptos Display`, macros/formulas with `Aptos Mono`, and serif texts with `Aptos Serif`.
* **Hotkey Mapping**: Remapping vital shortcuts (Ctrl+F, Ctrl+H, Ctrl+K, F5) to strictly follow MS Office standards.
* **Extreme Rust/C++ Compression**: Imagery is processed in parallel using the multi-threaded `oxipng` (Rust), while the final OXT bundle is squeezed via `ect` (Efficient Compression Tool), ensuring a breakthrough minimal file size.

### System Requirements & Dependencies
The build pipeline is completely autonomous. Ensure you have:
* **Windows**: **MSYS2 (UCRT64)** environment. The LibreOffice path is discovered automatically via Windows Registry polling.
* **Linux**: Standard terminal shell (`apt`, `dnf`, or `pacman` package managers supported).
* A locally installed **LibreOffice** suite (acting as the donor source for core icons).

### Build Instructions

1. Open your terminal (run **MSYS2 UCRT64** if you are on Windows).
2. Navigate to the project root directory and execute:
   ```bash
   make
   ```
   *Note: The script automatically audits your environment. On Linux, it installs missing packages via native managers; on Windows, it fetches the latest stable binary releases of `minify`, `oxipng`, and `ect` directly from GitHub into the build folder.*
3. The ultra-compressed extension bundle will be generated at: `build_output/msoffice_theme_2022.oxt`.

### Installation
1. **System Fonts**: To install Aptos, Mono, and Serif fonts globally across the operating system, execute:
   * On Windows: Open MSYS2 as an Administrator and run `make install-fonts`.
   * On Linux: Run `sudo make install-fonts`.
2. **Extension**: Open OpenOffice, navigate to `Tools -> Extension Manager -> Add...`, and pick the generated `.oxt` file. Restart the office suite to apply changes.
