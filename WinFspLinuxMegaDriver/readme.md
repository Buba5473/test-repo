# WinFSP Universal Linux File System Driver (Mega Linux Reader)

[English version below](#english-version)

Высокопроизводительный асинхронный драйвер файловых систем Linux для Windows 10+ (архитектура `amd64`), работающий в пользовательском режиме (User-Mode FSD) на базе WinFSP SDK. Проект глубоко оптимизирован под современные многоядерные процессоры и NVMe-накопители, обеспечивая безопасный доступ на чтение к 13 популярным файловым системам Linux.

---

## 🚀 Ключевые особенности и архитектурные оптимизации

### 1. Асинхронный Zero-Copy ввод-вывод (I/O)
* **Интеграция с WinFSP IOCP**: Драйвер задействует порты завершения ввода-вывода (I/O Completion Ports). Потоки WinFSP обрабатывают запросы Windows полностью асинхронно.
* **Прямой DMA-перенос (Zero-Copy)**: Чтение секторов с физического диска выполняется с использованием системных флагов `FILE_FLAG_NO_BUFFERING` и `FILE_FLAG_OVERLAPPED`. Данные поступают из контроллера накопителя напрямую в буфер запроса ОС Windows, минуя промежуточное копирование в памяти приложения.

### 2. Векторное ускорение amd64 (AVX2)
* **Аппаратный расчет CRC32c**: Для верификации метаданных современных ФС (Ext4 `metadata_csum`, XFS v5, Btrfs) используются процессорные инструкции `_mm_crc32_u64`, что полностью исключает задержки (Stalls) CPU при обработке метаданных.
* **SIMD-декомпрессия**: Логика распаковки блоков данных ZSTD автоматически задействует векторные регистры AVX2/BMI2 архитектуры `amd64`.

### 3. Оптимизация под Intel Thread Director (Гибридная архитектура P/E ядер)
* **Динамическая топология**: При старте драйвер сканирует CPU через `GetLogicalProcessorInformationEx` и разделяет ядра на Performance (P) и Efficient (E).
* **Приоритезация задач**: Тяжелые вычислительные операции (декомпрессия ZSTD/LZ4, сборка B+деревьев экстентов) принудительно переносятся на P-ядра с приоритетом `THREAD_PRIORITY_HIGHEST` и отключением троттлинга. Легкие операции (листинг каталогов Проводником Windows) изолируются на E-ядрах в режиме `EcoQoS`.

### 4. Поддержка 13 файловых систем и стандартов Linux 6+
* **Ext2 / Ext3 / Ext4**: Поддержка 64-битных адресов блоков, дерева экстентов и инлайн-данных.
* **XFS (v4 и v5)**: Полная обратная совместимость. Автоматическое переключение структур заголовков B+деревьев экстентов со сдвигом на 48 байт для v4.
* **Btrfs**: Разбор Chunk/Root/FS B-деревьев, поддержка подтомов (Subvolumes) и прозрачного сжатия.
* **F2FS**: Оптимизированный разбор Flash-сегментов, обработка инлайн-каталогов `f2fs_inline_dentry` в ОЗУ.
* **ZFS**: Потоковый XDR-декодер для разбора конфигураций пулов (ZPOOL) и датасетов, упакованных в Big-Endian формат `nvlist`.
* **Дополнительно**: Встроенные Read-Only парсеры для ReiserFS, Reiser4, HFS, HFS+, Apple APFS, UFS и UFS2.

### 5. Полный спектр поддерживаемых алгоритмов сжатия
В конвейер чтения интегрирован диспетчер `CompressionManager`, поддерживающий нативную распаковку дисковых блоков «на лету»:
* **ZSTD** (Основной стандарт Btrfs / ZFS в Linux 6+)
* **LZ4** (Высокоскоростной стандарт ZFS / F2FS)
* **LZO1X** (Классический стандарт сжатия Btrfs)
* **Zlib / Deflate** (Универсальный стандарт Gzip)

---

## 🛠️ Требования и окружение для сборки

Сборка проекта полностью изолирована и не зависит от динамических библиотек среды разработки.

* **ОС**: Windows 10 / Windows 11 (64-bit, amd64)
* **Окружение**: [MSYS2](https://msys2.org) (Строго терминал **UCRT64**)
* **SDK**: [WinFSP SDK](https://github.com) (Установлен по стандартному пути `C:\Program Files (x86)\WinFsp`)

---

## 📦 Быстрый запуск сборки в один клик

Проект использует монолитную архитектуру (`src/main.cpp`). Все зависимости и утилиты разворачиваются автоматически.

1. Откройте консоль **MSYS2 UCRT64**.
2. Перейдите в каталог с проектом и запустите скрипт автоматизации:
```bash
chmod +x build_and_sign.sh
./build_and_sign.sh
```

### Что делает скрипт автоматически:
1. **Проверяет пакеты `pacman`**: Если в системе нет `clang`, `ninja`, `cmake`, `curl`, `tar` или `osslsigncode`, скрипт предложит установить их одной командой.
2. **Скачивает Latest Releases**: Скрипт обращается к GitHub REST API, находит последние стабильные релизы исходных кодов **ZSTD, LZ4, Zlib и LZO**, скачивает архивы `tarball` (без тяжелой истории коммитов) и распаковывает их в `third_party/`.
3. **Генерирует сертификаты**: Создает пару ключей `fake_cert.pfx` и `fake_cert.cer` с метаданными «от балды».
4. **Компилирует проект**: Запускает CMake + Ninja в изолированной папке `build_artifacts/`, применяя агрессивные флаги статической линковки (`-static -static-libstdc++`) и оптимизации процессора (`-O3 -march=x86-64-v3`).
5. **Подписывает бинарник**: Накладывает цифровую подпись на готовый файл службы.

Финальные бинарники сохраняются в папке `build_artifacts/`.

---

## ⚙️ Установка, удаление и системная интеграция

Скомпилированное приложение `WinFspLinuxReaderService_Signed.exe` является полноценным инсталлятором и системной службой Windows.

### Установка и запуск:
Запустите командную строку Windows от имени **Администратора** и выполните:
```cmd
WinFspLinuxReaderService_Signed.exe --install
```
**Что происходит на системном уровне:**
1. Приложение вызывает `certutil.exe -addstore -f "Root" "fake_cert.cer"`. Созданный фейковый сертификат заносится в хранилище доверенных корневых центров сертификации Windows. Вкладка «Цифровые подписи» в свойствах файла становится полностью валидной (зеленой).
2. Приложение регистрирует в Windows Service Control Manager (SCM) постоянную службу `WinFspLinuxReaderService` с типом запуска "Автоматически" и подробным описанием в оснастке `services.msc`.
3. Служба регистрирует себя в качестве официального провайдера журналов событий. Все ключевые уведомления (успешное монтирование, тип обнаруженной ФС, ошибки доступа к диску) пишутся асинхронно в стандартный **Просмотр событий Windows (Журнал Application)**.

### Удаление:
Для полной очистки операционной системы от драйвера выполните:
```cmd
WinFspLinuxReaderService_Signed.exe --uninstall
```
Служба корректно завершит рабочие потоки, размонтирует виртуальный диск, удалит запись о себе из базы данных SCM Windows и вызовет `certutil` для удаления сертификата из корневого доверенного хранилища ОС, не оставляя в системе никакого мусора.

---

## 📂 Структура каталогов проекта

```text
WinFspLinuxMegaDriver/
├── CMakeLists.txt                      # Параметры компилятора Clang, флаги AVX2/LTO и пути к WinFSP
├── build_and_sign.sh                   # Автономный bash-скрипт развертывания "в один клик"
└── src/                                # Исходный код проекта
    ├── main.cpp                        # МОНОЛИТНАЯ реализация (Ядро FSD, Системные API, Драйверы ФС)
    └── resources/                      # Системные ресурсы Windows
        ├── app.manifest                # Манифест приложения: жесткий запрос прав Администратора (UAC)
        └── resources.rc                # Свойства версии, копирайты и STRINGTABLE для Event Log
```

---
---

<a name="english-version"></a>

# WinFSP Universal Linux File System Driver (Mega Linux Reader)

A high-performance, asynchronous, user-mode Linux File System Driver (User-Mode FSD) for Windows 10+ (`amd64` architecture) built on top of the WinFSP SDK. Deeply optimized for modern multi-core processors and high-speed NVMe storage, providing secure read-only access to 13 popular Linux filesystems.

---

## 🚀 Key Features & Architectural Optimizations

### 1. Asynchronous Zero-Copy I/O
* **WinFSP IOCP Integration**: The driver natively hooks into I/O Completion Ports. WinFSP worker threads process OS requests fully asynchronously.
* **Direct DMA Transfer (Zero-Copy)**: Disk sector reads leverage `FILE_FLAG_NO_BUFFERING` and `FILE_FLAG_OVERLAPPED` system flags. Data streams from the physical storage controller via DMA straight into the Windows target request buffer, entirely bypassing intermediate application-level memory copying.

### 2. Vectorized amd64 Acceleration (AVX2)
* **Hardware-Accelerated CRC32c**: Metadata verification for modern file systems (Ext4 `metadata_csum`, XFS v5, Btrfs) uses native `_mm_crc32_u64` processor intrinsics, completely preventing CPU pipeline stalls during heavy metadata processing.
* **SIMD-Driven Decompression**: ZSTD data block expansion automatically utilizes AVX2/BMI2 vector registers of the `amd64` architecture.

### 3. Intel Thread Director & Hybrid Core Optimization
* **Dynamic Topology Scanning**: Upon startup, the driver queries the CPU via `GetLogicalProcessorInformationEx` to explicitly map Performance (P) and Efficient (E) cores.
* **Task Prioritization**: Compute-heavy workloads (ZSTD/LZ4 decompression, XFS extent B+tree traversal) are pinned to P-cores with `THREAD_PRIORITY_HIGHEST` and execution speed throttling disabled. Lightweight tasks (directory listings by Windows Explorer) are restricted to E-cores utilizing `EcoQoS`.

### 4. Comprehensive Linux 6+ Filesystem Standards
* **Ext2 / Ext3 / Ext4**: Supports 64-bit block addressing, extent trees, and `inline_data` properties.
* **XFS (v4 & v5)**: Full backward compatibility. Dynamically switches between block-extent B+tree headers, applying a 48-byte reduction shift for older v4 structures.
* **Btrfs**: Comprehensive parsing of Chunk, Root, and FS B-trees, with full support for Subvolumes and transparent block compression.
* **F2FS**: Optimized Flash-segment scanning and in-RAM processing of `f2fs_inline_dentry` layouts.
* **ZFS**: Asynchronous streaming XDR decoder to unpack ZPOOL layouts and dataset properties serialized in Big-Endian `nvlist` structures.
* **Legacy & Cross-Platform Support**: Embedded fallback read-only parsers for ReiserFS, Reiser4, HFS, HFS+, Apple APFS, UFS, and UFS2.

### 5. Multi-Algorithm Compression Layer
An integrated thread-safe `CompressionManager` pipeline handles transparent on-the-fly decompression for blocks:
* **ZSTD** (Standard for Btrfs / ZFS in modern Linux 6+ kernels)
* **LZ4** (Ultra-fast standard for ZFS / F2FS)
* **LZO1X** (Legacy compression layout for Btrfs)
* **Zlib / Deflate** (Classic cross-platform Gzip compression)

---

## 🛠️ Build Requirements & Prerequisites

The build chain is fully isolated and compiled statically to ensure zero dependencies on third-party MSYS2 runtime DLLs.

* **OS**: Windows 10 / Windows 11 (64-bit, amd64)
* **Environment**: [MSYS2](https://msys2.org) (Strictly the **UCRT64** terminal flavor)
* **SDK**: [WinFSP SDK](https://github.com) (Installed at the default location: `C:\Program Files (x86)\WinFsp`)

---

## 📦 One-Click Deployment and Compilation

The project uses a highly consolidated layout (`src/main.cpp`). Dependencies are resolved autonomously.

1. Open the **MSYS2 UCRT64** terminal prompt.
2. Navigate to the project root and initiate the deployment sequence:
```bash
chmod +x build_and_sign.sh
./build_and_sign.sh
```

### What the automation script executes:
1. **Validates Toolchain Packages**: Scans for `clang`, `ninja`, `cmake`, `curl`, `tar`, and `osslsigncode`. If anything is missing, it interactively prompts the user for automated installation via `pacman`.
2. **Fetches Latest Releases**: Queries the GitHub REST API to locate the latest stable tags for **ZSTD, LZ4, Zlib, and LZO**. It downloads compact source code `tarball` archives, stripping bulky git commit history, and extracts them into `third_party/`.
3. **Generates Crypto Certificates**: Builds a matching cryptographic key pair (`fake_cert.pfx` and `fake_cert.cer`) using randomized placeholder certificate authority parameters.
4. **Compiles the Binary**: Invokes CMake + Ninja inside an isolated `build_artifacts/` directory, enforcing aggressive static linking (`-static -static-libstdc++`) and modern processor targeting flags (`-O3 -march=x86-64-v3`).
5. **Signs the Target Application**: Digitally seals the resulting service binary using `osslsigncode`.

The finalized application bundles are deployed into the `build_artifacts/` directory.

---

## ⚙️ Installation, Maintenance & OS Integration

The compiled executable `WinFspLinuxReaderService_Signed.exe` acts as both the user-mode FSD runtime and its own service configuration tool.

### Installation & Launch:
Open a Windows Command Prompt (`cmd.exe`) as an **Administrator** and run:
```cmd
WinFspLinuxReaderService_Signed.exe --install
```
**System-level actions performed:**
1. The application invokes `certutil.exe -addstore -f "Root" "fake_cert.cer"`. The custom generated certificate is safely injected into the Windows Trusted Root Certification Authorities store. The "Digital Signatures" tab under file properties turns valid (green).
2. The application registers a persistent Windows NT Service named `WinFspLinuxReaderService` with an automatic startup type and a comprehensive service description in `services.msc`.
3. The service registers itself as a native event logging provider. All core notifications (successful mounting, filesystem type detected, disk access errors) are written asynchronously to the standard **Windows Event Viewer (Application Log)**.

### Uninstallation:
To cleanly remove the driver from the operating system, execute:
```cmd
WinFspLinuxReaderService_Signed.exe --uninstall
```
The driver safely terminates active worker threads, unmounts the virtual drive, destroys the service from the Windows SCM database, and removes the self-signed certificate from the Windows Root Store, leaving the host system completely clean.

---

## 📂 Project Directory Layout

```text
WinFspLinuxMegaDriver/
├── CMakeLists.txt                      # Clang compiler parameters, AVX2/LTO flags, and WinFSP path lookups
├── build_and_sign.sh                   # Fully autonomous "one-click" shell deployment script
└── src/                                # Source files
    ├── main.cpp                        # UNIFIED monolithic implementation (Core FSD, System APIs, Drivers)
    └── resources/                      # Windows resources
        ├── app.manifest                # Manifest file enforcing Administrator privilege elevation (UAC)
        └── resources.rc                # Version properties, copyrights, and STRINGTABLE entries for Event Log
```
