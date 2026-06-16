# Core Enterprise Engine - Terminal Sector Service Control Platform

[Русский](#русский) | [English](#english)

---

## Русский

### Описание проекта
**Core Enterprise Engine (RDP Wrapper Service)** — это высокотехнологичная системная служба Windows на C++ (Win32), предназначенная для расширения функционала сервера удаленного рабочего стола (RDP) на десктопных версиях операционных систем Windows 10 и Windows 11 (включая ветки 24H2, 25H2 и 26H1+). 

Платформа позволяет обойти жесткие лицензионные ограничения десктопных редакций ОС, блокирующие параллельные сессии и одновременную работу нескольких пользователей под разными учетными записями. Дополнительно проект предоставляет защищенный бэкэнд управления и автоматизирует скрытое развертывание режима **RDP Shadowing** (удаленное наблюдение/управление) без вывода интерактивных запросов пользователям внутри локальной сети (LAN).

---

### Архитектура и ключевые особенности

* **Полная динамическая инжекция и Splicing (Защита от обновлений ОС)**: Проект не изменяет файлы на диске. Вместо этого он находит легитимный процесс `svchost.exe`, обслуживающий `TermService`, динамически вычисляет смещения модулей с учетом **ASLR** и производит межпроцессный инлайн-перехват (сплайсинг) функции `SLGetWindowsProductPolicy` из `slc.dll`, подменяя ответы ОС для токена `"Enterprise-Rdp-Server"`. Полностью совместим с изменениями ядра Windows 24H2/25H2+.
* **Интерактивное вытеснение конкурентов**: При инсталляции служба сканирует систему на наличие альтернативных решений (*StasCorp RDP Wrapper, SuperRDP, TermPatch*) на уровне папок, реестра и активных служб, предлагая пользователю выполнить их безопасную деинсталляцию во избежание падения подсистемы терминалов.
* **Bypass проактивной защиты (Defender / Kaspersky)**: Интегрированы механизмы обхода ложных срабатываний антивирусов — манифесты совместимости, валидная иконка, заполненные метаданные версии под легитимный *Windows Update Core Agent*, скрытое добавление папок, бинарников и задач в белые списки **Windows Defender** (через WMI COM) и **Kaspersky Endpoint Security** (через `avp.com`), а также подпись собранного PE-файла сгенерированным на лету доверенным сертификатом.
* **Асинхронный NetCtrl Сервер**: Поднимает изолированный управляющий TCP-сервер на порту `44389`. Все входящие пакеты шифруются по алгоритму **AES-256-CBC**. Благодаря неблокирующим сокетам и селекторам (`select()`, `ioctlsocket`), сервер мгновенно высвобождает ресурсы и останавливается по сигналу SCM без зависаний.
* **Автономная работа с роутерами**: Автоматически определяет адрес сетевого шлюза, идентифицирует тип прошивки маршрутизатора (**KeeneticOS, Padavan, Tomato**) по HTTP-ответам и осуществляет проброс портов `3389` и `44389` через соответствующие Web-API и CGI-интерфейсы.
* **Потокобезопасный UTF-8 Логгер**: Ведет лог операций в кодировке UTF-8 без BOM. Путь к файлу по умолчанию (`C:\rdp_wrapper_service.log`) может быть переопределен через аргументы командной строки.
* **Скрытое самовосстановление**: Регистрирует персистентную задачу в Планировщике задач (`ITaskService` COM), которая выполняется от имени системы (`NT AUTHORITY\SYSTEM`) при загрузке компьютера или установке обновлений ОС, проверяя целостность службы и обновляя базу сигнатур из удаленного репозитория.

---

### Системные требования и Среда сборки
* **Операционная система**: Windows 10 / 11 (X64)
* **Среда компиляции**: Среда **MSYS2 UCRT64** (использование MSYS или MINGW64 не поддерживается).
* **Сборочный инструмент**: **CMake** + генератор **Ninja**.
* **Линковка**: Принудительная статическая линтовка зависимостей рантайма (`-static -static-libgcc -static-libstdc++`). Нативные библиотеки Windows (WinSock2, WinInet, BCrypt, COM) связываются динамически с подсистемами ОС.

---

### Быстрый запуск и Компиляция

1. Откройте консоль **MSYS2 UCRT64**.
2. Перейдите в корневой каталог проекта, где расположены файлы `main.cpp`, `service_engine.hpp` и `build.sh`.
3. Задайте права на выполнение сборочного скрипта и запустите его:
   ```bash
   chmod +x build.sh
   ./build.sh
   ```
4. Скрипт проверит зависимости, установит `cmake` и `ninja` через `pacman`, скомпилирует бинарный файл, сгенерирует маскировочный сертификат и подпишет итоговый файл. Результат будет расположен в `build_ninja/rdpwrap_srv.exe`.

---

### Спецификация интерфейса командной строки (CLI)

Служба автоматически определяет язык операционной системы и адаптирует вывод. При запуске без параметров отображается краткая справка.

#### Первичные аргументы (Обязательные):
* `install` — Запуск комплексной инсталляции (зачистка конкурентов, обход АВ, создание службы и задачи, настройка брандмауэра).
* `uninstall` — Полная деинсталляция, удаление правил сетевого экрана, очистка планировщика и зачистка компонентов на диске.
* `restart` — Принудительный горячий перезапуск ядра службы, `TermService` и юзер-мод редиректора периферии `UmRdpService`.
* `status` — Отображение расширенной статистики службы, состояния модулей памяти и списка активных пользователей.

#### Дополнительные опции:
* `--help` — Вывод углубленного руководства по синтаксису и примеров.
* `--target <ip/name>` — Указание адреса удаленного хоста для выполнения действия на другой машине.
* `--user <login>` — Логин Администратора для авторизации на удаленной рабочей станции (через `ImpersonateLoggedOnUser`).
* `--pass <password>` — Пароль Администратора удаленной машины.
* `--log <path>` — Переопределение стандартного пути лог-файла.
* `--router` — Включение модуля автоматического маппинга портов на роутере шлюза.

#### Пример использования:
```cmd
:: Установка службы с логированием на диск D и пробросом порта на роутере Keenetic/Padavan
rdpwrap_srv.exe install --log D:\Logs\rdp.log --router

:: Удаленный просмотр статистики сессий на машине в локальной сети
rdpwrap_srv.exe status --target 192.168.1.105 --user Administrator --pass "P@ssw0rd123"
```

---
---

## English

### Project Overview
**Core Enterprise Engine (RDP Wrapper Service)** is an advanced Windows system service written in C++ (Win32 API) architected to extend Remote Desktop Server (RDP) functionality on desktop distributions of Windows 10 and Windows 11 (including 24H2, 25H2, and 26H1+ branches).

The platform bypasses artificial licensing restrictions embedded into client Windows editions that restrict parallel terminal sessions and concurrent multi-user workstation access. Additionally, the project exposes an isolated, encrypted orchestration backend and automates covert **RDP Shadowing** deployment (remote surveillance/control) with no confirmation prompts required for local area network (LAN) scopes.

---

### Architecture & Key Subsystems

* **Cross-Process Injection & Splicing Engine (Update Resilience)**: The system writes no static patches to disk. Instead, it captures the target `svchost.exe` instance hosting `TermService`, dynamically calculates module base offsets regardless of **ASLR**, and executes runtime inline splicing hooks over `SLGetWindowsProductPolicy` inside `slc.dll` to fake `"Enterprise-Rdp-Server"` license state. Fully adaptive to Windows 11 24H2/25H2 internal memory shifts.
* **Interactive Conflict Eviction**: During installation, the engine scans the operating system for competing instances (*StasCorp RDP Wrapper, SuperRDP, TermPatch*) across filesystem directories, registry keys, and Active SCM Services. It prompts the user for safe automated uninstallation to avoid terminal sub-system crashes.
* **Proactive Security Bypass (Defender / KES)**: Implements specialized False-Positive remediation techniques, including data isolation manifests, valid application icons, and matching version metadata masking as a trusted *Windows Update Core Agent*. Folders, binaries, and task sequences are programmatically whitelisted within **Windows Defender** (via WMI COM) and **Kaspersky Endpoint Security** (via `avp.com`) followed by signing the final PE structure with a trusted, on-the-fly generated certificate.
* **Asynchronous Non-Blocking NetCtrl Server**: Spawns an isolated management listener on TCP port `44389`. Data transiting the socket is fully encrypted using **AES-256-CBC**. Leveraging non-blocking sockets and asynchronous event loops (`select()`, `ioctlsocket`), the engine releases sockets instantly and handles SCM termination signals without deadlocks.
* **Autonomous Gateways Provisioning**: Automatically resolves network gateway IP, fingerprints router firmware type (**KeeneticOS, Padavan, Tomato**) by testing HTTP responses, and automates port forwarding rules configuration for ports `3389` and `44389` via native REST-API/CGI handlers.
* **Thread-Safe UTF-8 Logger**: Records atomic engine states into raw UTF-8 (without BOM) data files. The default installation path (`C:\rdp_wrapper_service.log`) can be arbitrarily overridden using CLI arguments.
* **Persistent Auto-Recovery Task**: Establishes a self-healing scheduler layout (`ITaskService` COM) running under `NT AUTHORITY\SYSTEM` context triggered at system startup or OS update routines to validate service health and dynamically pull configuration updates from remote master repositories.

---

### System Requirements & Build Environment
* **Target Operating System**: Windows 10 / 11 (X64)
* **Toolchain Environment**: **MSYS2 UCRT64** Console (MSYS or MINGW64 environments are strictly unsupported).
* **Build System Generator**: **CMake** + **Ninja** generator.
* **Linker Targets**: Hard-coded static compilation for MSYS2 runtime dependencies (`-static -static-libgcc -static-libstdc++`). Native Windows core subsystems (WinSock2, WinInet, BCrypt, COM) link dynamically against host platform components.

---

### Build Instructions

1. Launch the **MSYS2 UCRT64** terminal emulator.
2. Navigate to the project root path containing `main.cpp`, `service_engine.hpp`, and `build.sh`.
3. Set execution flags on the compilation wrapper script and execute it:
   ```bash
   chmod +x build.sh
   ./build.sh
   ```
4. The wrapper script handles automated tool validation, downloads missing `cmake`/`ninja` requirements via `pacman`, runs compiler tasks, binds resources, and attaches certificates. The final binary will be emitted at `build_ninja/rdpwrap_srv.exe`.

---

### Command Line Interface (CLI) Specification

The executable dynamically requests UI language configurations from the operating system kernel and translates CLI text matching user OS preferences. Launching the executable without parameters displays primary usage menus.

#### Primary Arguments (Required):
* `install` — Triggers global deployment (purges competitors, injects AV exclusions, registers service/scheduler entities, provisions firewall rules).
* `uninstall` — Conducts global teardown (unwinds firewall rule layers, destroys scheduled tasks, drops operational binaries from disk).
* `restart` — Forces hot reload routines across core service engine, host `TermService`, and `UmRdpService` peripheral redirectors.
* `status` — Extracts live analytics from the service instance, memory mapping status, and active user session states.

#### Additional Modifiers:
* `--help` — Displays exhaustive argument configuration structures and usage scenarios.
* `--target <ip/name>` — Redirects execution scope toward a remote workstation target.
* `--user <login>` — Supplies target Administrator credentials for remote station security handshakes (via `ImpersonateLoggedOnUser`).
* `--pass <password>` — Password matching specified administrative credential.
* `--log <path>` — Overrides default engine logger file destination path.
* `--router` — Commands the setup layout to try automatic port mapping on the network gateway.

#### Command Invocation Examples:
```cmd
:: Install the core engine with logging piped to drive D and port mapping enabled on a Keenetic/Padavan gateway
rdpwrap_srv.exe install --log D:\Logs\rdp.log --router

:: Review active user sessions on a remote machine inside the local network
rdpwrap_srv.exe status --target 192.168.1.105 --user Administrator --pass "P@ssw0rd123"
```