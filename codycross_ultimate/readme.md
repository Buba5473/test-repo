# CodyCross Ultimate Modder (Android 14 Optimized) 🚀

[Русский](#русский) | [English](#english)

---

## Русский

Автоматизированный скрипт для комплексного реверс-инжиниринга, очистки от рекламы, телеметрии, заморозки обновлений, активации чит-опций, взлома подписок и глубокой оптимизации игры **CodyCross** под требования **Android 14 (API 34/35)**. Скрипт разработан для выполнения в изолированном нативном окружении **MSYS2 UCRT64** на Windows.

### ✨ Основные возможности

*   **Автономная подготовка окружения**: Скрипт автоматически проверяет наличие необходимых системных утилит в MSYS2 и предлагает установить недостающие одной командой.
*   **Ультра-легковесный запуск инструментов**: Вместо скачивания тяжелого Android SDK (Build-Tools), скрипт использует `curl` для точечной загрузки изолированных файлов `zipalign.exe` и `apksigner.jar` из проверенных источников (размер снижен с ~60 МБ до ~4 МБ). Последние версии `Apktool` и `Il2CppDumper` скачиваются напрямую через GitHub API.
*   **Полная блокировка обновлений (Новое!)**: Интегрирован механизм **Update Blocker**. На уровне `AndroidManifest.xml` вырезаются интенты апдейтов Google Play, а на уровне кода Unity Python-патчер находит методы проверки версий (`CheckForUpdates`, `IsUpdateRequired`) и глушит их инъекцией ARM64-инструкции `MOV W0, #0`. Игра считает себя самой актуальной и никогда не потребует обновиться.
*   **Интерактивный Premium-взлом**: При сборке скрипт предлагает опционально активировать VIP-статус и разблокировать платные подписки. Встроенный Python-патчер находит методы проверки покупок (`IsPremium`, `IsSubscribed`) и делает инъекцию ARM64-инструкции `MOV W0, #1`, открывая все закрытые паки кроссвордов.
*   **Интерактивный чит-мод**: Предлагает опционально активировать режим бесконечной валюты и помощников. Скрипт находит методы баланса (`get_Tokens`, `get_Coins`) и делает инъекцию ARM64-инструкции `MOV W0, #999999`, обеспечивая неиссякаемый запас подсказок.
*   **Двухэтапная очистка (Pre/Post Build Clean)**: 
    *   *Перед сборкой:* Полностью удаляет следы прошлых сессий, исключая попадание старого кода или кэша в новый билд.
    *   *После сборки:* Мгновенно стирает гигабайты временного мусора (папки декомпиляции, дампы, промежуточные APK), оставляя на диске только чистый готовый APK.
*   **Поддержка локальных APK и XAPK**: Скрипт принимает как обычные файлы `.apk`, так и контейнеры `.xapk`. При обнаружении XAPK он автоматически распакует его, извлечет 64-битное ядро (`base.apk`) и удалит промежуточные сплит-пакеты.
*   **Удаление рекламы и телеметрии**: Запускает `Il2CppDumper`, сканирует структуру методов движка Unity (`script.json`) и патчит байты функций рекламы (AdMob, AppLovin) и аналитики (Firebase), заменяя их на мгновенный возврат `RET` (`D65F03C0` для ARM64).
*   **Языковая оптимизация**: Вырезает все лишние локализации, оставляя строго **Русский (RU)** и **Английский (EN)** языки.
*   **Очистка архитектур**: Удаляет устаревшие 32-битные библиотеки (`armeabi-v7a`, `x86`), оставляя только чистый **arm64-v8a** (стандарт для Android 14+).
*   **Опциональное сжатие графики**: Интерактивно предлагает провести глубокую оптимизацию текстур (`.png`, `.jpg`) с помощью утилит `optipng` и `jpegoptim` без потери качества.
*   **Двухпроходное ультра-сжатие (7-Zip)**: Сжимает код `dex` и ресурсы разметки на максимальных настройках архивации (`-mx9`), но оставляет библиотеки `.so` и данные Unity без сжатия (`-mx0/Store`), предотвращая ошибку `INSTALL_FAILED_INVALID_APK` на Android 14.

### 📦 Структура папок

Скрипт изолирован и работает внутри одной директории:
```text
📂 codycross_mod_env/
├── 📂 tools/                 # Скачанный софт (Apktool, zipalign, apksigner, Keystore)
│   └── 📂 Il2CppDumper/      # Распакованный дампер кода Unity
├── 📄 codycross_base.apk     # ИСХОДНЫЙ ФАЙЛ ИГРЫ (СЮДА КЛАСТЬ APK)
│   ── 📄 codycross_base.xapk    # ИЛИ СЮДА КЛАСТЬ XAPK АРХИВ
└── 📄 codycross_patched.apk  # ИТОГОВЫЙ ОПТИМИЗИРОВАННЫЙ APK С ЧИТАМИ И БЕЗ РЕКЛАМЫ
```

### 🛠️ Системные требования и подготовка

1. Установите **MSYS2** (официальный сайт: [msys2.org](https://msys2.org)).
2. Запустите терминал **MSYS2 UCRT64** (используйте именно эту среду).

### 🚀 Инструкция по использованию

#### Шаг 1: Создание скрипта
Создайте файл скрипта в терминале MSYS2 и выдайте ему права:
```bash
touch codycross_ultimate.sh
nano codycross_ultimate.sh  # Вставьте код скрипта и сохраните (Ctrl+O, Enter, Ctrl+X)
chmod +x codycross_ultimate.sh
```

#### Шаг 2: Первый запуск (Инициализация)
Запустите скрипт: `./codycross_ultimate.sh`. При запросе на установку пакетов (`pacman`) нажмите **`y`**. Скрипт создаст рабочую папку `codycross_mod_env` и завершится с сообщением, что файл игры не найден.

#### Шаг 3: Добавление исходного файла игры
Положите скачанный вами оригинальный файл CodyCross (APK или XAPK) в папку `codycross_mod_env`. Переименуйте его строго в **`codycross_base.apk`** или **`codycross_base.xapk`**.

#### Шаг 4: Конфигурация и Сборка
Запустите скрипт повторно: `./codycross_ultimate.sh`.
1. Скрипт спросит про сжатие картинок: `Хотите запустить глубокое сжатие PNG/JPG текстур...? (y/n)`.
2. Скрипт спросит про читы: `Включить чит на бесконечных помощников/токены? (y/n)`. Выберите **`y`** для активации бесконечных подсказок.
3. Скрипт спросит про премиум: `Разблокировать Premium-контент/подписки? (y/n)`. Выберите **`y`**, чтобы получить VIP-статус и открыть все платные наборы кроссвордов без реальных покупок.
Скрипт выполнит всю рутину автоматически, параллельно применив «заморозку» обновлений, и выдаст готовый файл **`codycross_patched.apk`**.

### ⚠️ Тонкости реверс-инжиниринга для Разработчика

*   **Принцип работы Update Blocker**: Механизм блокировки обновлений выполняется безусловно (в отличие от читов) и нацелен на обеспечение полной автономности билда. Это исключает ситуации, когда через полгода игра внезапно перестанет работать, требуя скачать новую версию с серверов Fanatee.
*   **Изоляция читов и премиума**: Поиск игровых методов ведется по ключевым словам из массивов `CHEAT_KEYWORDS` и `PREMIUM_KEYWORDS`. Чтобы не задеть системные компоненты самого движка Unity, скрипт принудительно проверяет принадлежность функции к пространствам имен `codycross` или `fanatee`.
*   **Динамические смещения**: Поиск рекламы завязан на список `KEYWORDS` в Python-блоке. Если разработчики игры в будущем изменят имена внутренних классов рекламы, просто добавьте новые ключевые слова в массив `KEYWORDS` внутри скрипта.

---

## English

An automated tool script for end-to-end reverse engineering, ad/telemetry removal, mandatory update freezing, premium content unlock, cheat injection, and deep optimization of **CodyCross** specifically tailored to meet **Android 14 (API 34/35)** requirements. Designed to run within an isolated native **MSYS2 UCRT64** environment on Windows.

### ✨ Key Features

*   **Automated Environment Setup**: Checks for required system packages within MSYS2 and automatically installs missing ones via a single `pacman` prompt.
*   **Ultra-Lightweight Tool Provisioning**: Instead of downloading the heavy official Android SDK (Build-Tools), the script uses `curl` to fetch isolated, standalone `zipalign.exe` and `apksigner.jar` binaries (slashing down tool size from ~60 MB to ~4 MB). The latest releases of `Apktool` and `Il2CppDumper` are retrieved dynamically via the GitHub API.
*   **Mandatory Update Blocker**: Integrates an aggressive update freeze pipeline. It strips Google Play broadcast update intents from `AndroidManifest.xml`, while the built-in Python framework locates core runtime verification properties (`CheckForUpdates`, `IsUpdateRequired`) and patches them using an ARM64 `MOV W0, #0` instruction opcode. The local app permanently reports itself as up-to-date and never triggers forced update prompts.
*   **Interactive Premium Unlock**: Prompts during execution to optionally bypass purchase screens and unlock premium content. The built-in Python patcher scans the game structure for validation properties (`IsPremium`, `IsSubscribed`, `HasVIP`) inside the app's native namespace and injects an ARM64 `MOV W0, #1` instruction, giving you full VIP access to locked crosswords.
*   **Interactive Cheat Injection**: Prompts during execution to optionally activate an infinite currency and helpers mode. It target-patches the binary offsets of balance properties (`get_Tokens`, `get_Coins`) inside the app's native namespace and injects an ARM64 `MOV W0, #999999` instruction, giving you an inexhaustible supply of hints.
*   **Two-Stage Clean (Pre/Post Build Clean)**:
    *   *Pre-Build:* Completely purges remnants of any previous compilation sessions to eliminate data corruption or stale cache issues.
    *   *Post-Build:* Instantly wipes gigabytes of temporary deployment directories (decompiled workspaces, dumper json files, raw unaligned APKs), leaving only the clean, modified APK on your drive.
*   **Local APK & XAPK Container Support**: Accepts standard `.apk` files or `.xapk` bundles. If an XAPK is detected, it automatically unzips it, extracts the core 64-bit binary (`base.apk`), and discards any temporary split-language assets.
*   **Ad & Telemetry Disabling**: Executes `Il2CppDumper`, parses the resulting Unity code structure (`script.json`), and target-patches the binary offsets of ad frameworks (AdMob, AppLovin, Facebook) and analytics (Firebase) with an explicit function `RET` opcode (`D65F03C0` for ARM64).
*   **Language Asset Pruning**: Discards all secondary localized Android values, keeping strictly **Russian (RU)** and **English (EN)** locales.
*   **Architecture Pruning**: Purges legacy 32-bit compilation binaries (`armeabi-v7a`, `x86`), reserving exclusively native **arm64-v8a** libraries (mandatory standard for modern Android 14+ devices).
*   **Optional Image Optimization**: Interactively prompts for lossless optimization of graphics (`.png`, `.jpg`) utilizing `optipng` and `jpegoptim` binaries.
*   **Two-Pass Ultra Compression (7-Zip)**: Compresses core byte-code (`dex`) and layout XML assets at maximum archiving configurations (`-mx9`), while explicitly leaves `.so` libraries and Unity binaries uncompressed (`-mx0/Store`) to seamlessly pass Android 14 memory-mapping (`mmap`) stability checks and avoid `INSTALL_FAILED_INVALID_APK` faults.

### 📦 Directory Layout

The workspace is fully self-contained and operates within a single folder:
```text
📂 codycross_mod_env/
├── 📂 tools/                 # Downloaded binary assets (Apktool, zipalign, apksigner, Keystore)
│   └── 📂 Il2CppDumper/      # Extracted Unity metadata engine dumper
├── 📄 codycross_base.apk     # BASE TARGET FILE (PLACE YOUR LOCAL APK HERE)
│   ── 📄 codycross_base.xapk    # OR PLACE YOUR LOCAL XAPK BUNDLE HERE
└── 📄 codycross_patched.apk  # THE FINAL MODIFIED AD-FREE APK WITH CHEATS & PREMIUM UNLOCKED
```

### 🛠️ Prerequisites & Setup

1. Download **MSYS2** (Official site: [msys2.org](https://msys2.org)).
2. Launch the **MSYS2 UCRT64** terminal instance (ensure you are using UCRT64 specifically).

### 🚀 Usage Guide

#### Step 1: Initialize the Script File
Generate the execution script inside your MSYS2 console shell and apply proper user permissions:
```bash
touch codycross_ultimate.sh
nano codycross_ultimate.sh  # Paste the script code here, save and exit (Ctrl+O, Enter, Ctrl+X)
chmod +x codycross_ultimate.sh
```

#### Step 2: First-Time Initialization Run
Execute the binary: `./codycross_ultimate.sh`. When prompted to install missing dependencies via `pacman`, hit **`y`**. The routine will generate the target `codycross_mod_env` folder and safely exit, warning you that the game source is missing.

#### Step 3: Provide the Source File
Acquire the original CodyCross game archive (APK or XAPK) and place it inside the `codycross_mod_env` directory. Rename it exactly to **`codycross_base.apk`** or **`codycross_base.xapk`**.

#### Step 4: Run the Modder pipeline
Fire up the script again: `./codycross_ultimate.sh`.
1. Decide on texture optimization: `Хотите запустить глубокое сжатие PNG/JPG текстур...? (y/n)`.
2. Decide on cheat injection: `Включить чит на бесконечных помощников/токены? (y/n)`. Type **`y`** to unleash infinite hints.
3. Decide on premium unlock: `Разблокировать Premium-контент/подписки? (y/n)`. Type **`y`** to gain complete VIP privileges and open paid packs.
The architecture will automatically run through the reverse-engineering steps, suppress forced runtime updates, and compile your ready-to-install **`codycross_patched.apk`**.

### ⚠️ Technical Notes for the Reverse Engineer

*   **Update Suppression Mechanics**: The update blocker routine executes unconditionally (unlike cheats or premium patches) to guarantee absolute application longevity. This ensures that the compiled app will remain fully operational down the road without nagging the user for future store or package upgrades.
*   **Cheat & Premium Engine Isolation**: Target game methods are filtered via strings inside the `CHEAT_KEYWORDS` and `PREMIUM_KEYWORDS` arrays. To bypass matching any core engine frameworks or third-party plugins with identical naming, the script strictly validates that the signature belongs to either the `codycross` or `fanatee` namespace.
*   **Dynamic Class Offsets**: The binary patch routine evaluates names according to the `KEYWORDS` array defined inside the Python execution code. If a future iteration of the game renames ad modules, simply append those new signatures to the internal script `KEYWORDS` array.
