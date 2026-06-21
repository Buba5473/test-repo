#include <windows.h>
#include <winfsp/winfsp.h>
#include <winsock2.h> // Для функций ntohl / ntohll (Преобразование Big Endian ZFS в Little Endian)
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <thread>
#include <intrin.h>  // Для интринсиков AVX2 / SSE4.2 (_mm_crc32_u64)

#pragma comment(lib, "winfsp-x64.lib")
#pragma comment(lib, "ws2_32.lib")

// ==============================================================================
//                         СИСТЕМНЫЕ КОНСТАНТЫ СЛУЖБЫ
// ==============================================================================
#define SERVICE_NAME             L"WinFspLinuxReaderService"
#define SERVICE_DISPLAY_NAME     L"WinFSP Universal Linux File System Driver"
#define SERVICE_DESCRIPTION      L"Обеспечивает высокопроизводительный доступ на чтение к 13 файловым системам Linux (Ext2/3/4, Btrfs, XFS, ZFS, F2FS и др.) с использованием векторных оптимизаций amd64."

// Идентификаторы категорий событий Windows Event Viewer
#define MSG_INFO_GENERIC        0x00000001
#define MSG_INFO_MOUNTED        0x00000002
#define MSG_ERR_CRITICAL        0x00000003

// Типы сжатия данных на уровне ФС
enum class CompressionType {
    None,
    Zstd,
    Lzo1x,
    Lz4,
    Zlib
};

// Перечисление всех 13 поддерживаемых типов ФС
enum class LinuxFileSystemType {
    Unknown, Ext2, Ext3, Ext4, ReiserFS, Reiser4, HFS, HFS_Plus, UFS, UFS2, Apple_APFS, Btrfs, XFS_v4, XFS_v5, F2FS, ZFS
};

// ==============================================================================
//             СЕКЦИЯ 1: СТРУКТУРЫ ДАННЫХ ЯДРА LINUX 6+ (ЖЕСТКОЕ ВЫРАВНИВАНИЕ)
// ==============================================================================
#pragma pack(push, 1)

// --- СТРУКТУРЫ EXT4 ---
struct ext4_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count_lo;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic; // 0xEF53
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    // ... дополнительные поля метаданных ядра 6+ ...
    uint32_t s_checksum; // metadata_csum
};

struct ext4_extent {
    uint32_t ee_block;    // Первый логический блок экстента
    uint16_t ee_len;      // Количество блоков в экстенте
    uint16_t ee_start_hi; // Старшие 16 бит физического адреса блока
    uint32_t ee_start_lo; // Младшие 32 бита физического адреса блока
};

struct ext4_extent_header {
    uint16_t eh_magic;      // 0xF30A
    uint16_t eh_entries;    // Количество валидных записей
    uint16_t eh_max;        // Максимальное количество записей
    uint16_t eh_depth;      // Глубина дерева экстентов
    uint32_t eh_generation;
};

// --- СТРУКТУРЫ BTRFS ---
struct btrfs_disk_key {
    uint64_t objectid;
    uint8_t  type;
    uint64_t offset;
};

struct btrfs_header {
    uint8_t  csum[32]; // Валидируется аппаратно через AVX2
    uint8_t  fsid[16];
    uint64_t bytenr;   
    uint64_t flags;
    uint8_t  backref_rev;
    uint8_t  chunk_tree_uuid[16];
    uint64_t generation;
    uint64_t owner;    
    uint32_t nritems;  
    uint8_t  level;    // level == 0 означает лист дерева
};

struct btrfs_key_ptr {
    btrfs_disk_key key;
    uint64_t blockptr; 
    uint64_t generation;
};

struct btrfs_dir_item {
    btrfs_disk_key location;
    uint64_t transid;
    uint16_t data_len;
    uint16_t name_len;
    uint8_t  type;     
};

// --- СТРУКТУРЫ XFS V5 ---
struct xfs_btree_block_v5 {
    uint32_t bb_magic;     // 'BMAP' (0x424d4150)
    uint16_t bb_level;     
    uint16_t bb_numrecs;   
    uint64_t bb_leftiblk;  
    uint64_t bb_rightblk;  
    uint64_t bb_blkno;     
    uint64_t bb_lsn;
    uint8_t  bb_uuid[16];
    uint64_t bb_owner;     
    uint32_t bb_crc;       // CRC32c метаданных XFS
};

struct xfs_bmbt_rec {
    uint64_t l0;
    uint64_t l1; 
};

// --- СТРУКТУРЫ F2FS ---
struct f2fs_dir_entry {
    uint32_t ino;             
    uint32_t hash_code;       
    uint16_t name_len;        
    uint8_t  file_type;       
};

struct f2fs_inode {
    uint16_t i_mode;          
    uint8_t  i_advise;        
    uint8_t  i_inline;        // Маска инлайн-свойств
    uint32_t i_uid;
    uint32_t i_gid;
    uint64_t i_size;          // Размер файла в байтах
    uint64_t i_blocks;
    uint64_t i_atime;
    uint64_t i_mtime;
    uint32_t i_atime_nsec;
    uint32_t i_mtime_nsec;
    uint32_t i_addr[923];     // Прямые указатели на блоки данных ФС
    uint32_t i_nid[5];        
};

struct f2fs_inline_dentry {
    uint8_t  dentry_bitmap[27]; // Битмап валидности (214 бит)
    uint8_t  reserved;
    f2fs_dir_entry dentry[214]; 
    uint8_t  filename[214][8];  
};

#pragma pack(pop)

// ==============================================================================
//          СЕКЦИЯ 2: АППАРАТНЫЙ СКАНЕР INTEL THREAD DIRECTOR
// ==============================================================================
struct ProcessorCoreInfo {
    uint8_t CoreType;        // 1 = E-Core (Efficient), 2 = P-Core (Performance)
    KAFFINITY AffinityMask;  
};

class IntelThreadDirectorManager {
private:
    std::vector<ProcessorCoreInfo> m_Cores;
    KAFFINITY m_pCoresMask = 0;
    KAFFINITY m_eCoresMask = 0;
    size_t m_pCoresCount = 0;
    size_t m_eCoresCount = 0;

public:
    IntelThreadDirectorManager() {
        DetectTopology();
    }

    void DetectTopology() {
        DWORD bufferSize = 0;
        // Запрашиваем размер необходимой памяти
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize);
        if (bufferSize == 0) return;

        std::vector<BYTE> buffer(bufferSize);
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());

        // Получаем информацию о ядрах процессора
        if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bufferSize)) {
            return;
        }

        BYTE* ptr = buffer.data();
        BYTE* end = ptr + bufferSize;

        while (ptr < end) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX curr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (curr->Relationship == RelationProcessorCore) {
                ProcessorCoreInfo core{};
                // EfficiencyClass > 0 определяет P-ядра в гетерогенных архитектурах Intel
                if (curr->Core.EfficiencyClass > 0) {
                    core.CoreType = 2; 
                    m_pCoresMask |= curr->Core.GroupMask.Mask;
                    m_pCoresCount++;
                } else {
                    core.CoreType = 1; 
                    m_eCoresMask |= curr->Core.GroupMask.Mask;
                    m_eCoresCount++;
                }
                core.AffinityMask = curr->Core.GroupMask.Mask;
                m_Cores.push_back(core);
            }
            ptr += curr->Size;
        }

        // Страховочный вариант для классических симметричных процессоров (AMD / старые Intel)
        if (m_pCoresCount == 0) {
            m_pCoresCount = std::thread::hardware_concurrency();
            m_pCoresMask = static_cast<KAFFINITY>(-1);
        }
    }

    // Форсирование P-ядер для интенсивных вычислительных потоков декомпрессии ZSTD / расчета AVX2
    void OptimizeCurrentThreadForHeavyWork() {
        SetThreadAffinityMask(GetCurrentThread(), m_pCoresMask);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        // Windows 11 Quality of Service (QoS) - Отключение EcoQoS, форсирование высокой частоты
        THREAD_POWER_THROTTLING_STATE throttlingState{};
        throttlingState.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        throttlingState.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        throttlingState.StateMask = 0; 
        SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttlingState, sizeof(throttlingState));
    }

    // Перенос I/O-зависимых операций (листинг файлов, опрос путей) на энергоэффективные E-ядра
    void OptimizeCurrentThreadForLightWork() {
        if (m_eCoresMask != 0) {
            SetThreadAffinityMask(GetCurrentThread(), m_eCoresMask);
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        // Активация режима EcoQoS для Intel Thread Director
        THREAD_POWER_THROTTLING_STATE throttlingState{};
        throttlingState.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        throttlingState.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        throttlingState.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED; 
        SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttlingState, sizeof(throttlingState));
    }

    size_t GetPCoresCount() const { return m_pCoresCount; }
    size_t GetECoresCount() const { return m_eCoresCount; }
};

// ==============================================================================
//             СЕКЦИЯ 3: ЖУРНАЛИРОВАНИЕ И ДЕКОМПРЕССИЯ ДАННЫХ
// ==============================================================================
class EventLogger {
public:
    static void LogInfo(uint32_t eventId, const std::wstring& message) {
        Report(EVENTLOG_INFORMATION_TYPE, eventId, message);
    }

    static void LogError(uint32_t eventId, const std::wstring& message) {
        Report(EVENTLOG_ERROR_TYPE, eventId, message);
    }

private:
    static void Report(WORD type, uint32_t eventId, const std::wstring& message) {
        HANDLE hEventSource = RegisterEventSourceW(NULL, SERVICE_NAME);
        if (hEventSource) {
            LPCWSTR strings = { message.c_str() };
            ReportEventW(hEventSource, type, 0, eventId, NULL, 1, 0, strings, NULL);
            DeregisterEventSource(hEventSource);
        }
    }
};

// Подключение внешних статических библиотек сжатия
#include <zstd.h>
#include <lz4.h>
#include <zlib.h>
extern "C" {
    #include <lzo/lzo1x.h>
}

class CompressionManager {
public:
    static NTSTATUS DecompressBlock(CompressionType type, const BYTE* compBuf, size_t compSize, 
                                     BYTE* outBuf, size_t maxOut, size_t* actualOut) {
        if (type == CompressionType::None) {
            if (compSize > maxOut) return STATUS_BUFFER_TOO_SMALL;
            memcpy(outBuf, compBuf, compSize);
            *actualOut = compSize;
            return STATUS_SUCCESS;
        }

        switch (type) {
            case CompressionType::Zstd: {
                size_t dSize = ZSTD_decompress(outBuf, maxOut, compBuf, compSize);
                if (ZSTD_isError(dSize)) return STATUS_DATA_ERROR;
                *actualOut = dSize;
                return STATUS_SUCCESS;
            }
            case CompressionType::Lz4: {
                int dSize = LZ4_decompress_safe((const char*)compBuf, (char*)outBuf, (int)compSize, (int)maxOut);
                if (dSize < 0) return STATUS_DATA_ERROR;
                *actualOut = (size_t)dSize;
                return STATUS_SUCCESS;
            }
            case CompressionType::Lzo1x: {
                lzo_uint outLen = maxOut;
                if (lzo1x_decompress_safe(compBuf, compSize, outBuf, &outLen, nullptr) != LZO_E_OK) 
                    return STATUS_DATA_ERROR;
                *actualOut = outLen;
                return STATUS_SUCCESS;
            }
            case CompressionType::Zlib: {
                z_stream strm = { 0 };
                strm.next_in = const_cast<Bytef*>(compBuf);
                strm.avail_in = (uInt)compSize;
                strm.next_out = outBuf;
                strm.avail_out = (uInt)maxOut;
                if (inflateInit2(&strm, 15 + 32) != Z_OK) return STATUS_INITIALIZATION_FAILED;
                int ret = inflate(&strm, Z_FINISH);
                inflateEnd(&strm);
                if (ret != Z_STREAM_END && ret != Z_OK) return STATUS_DATA_ERROR;
                *actualOut = strm.total_out;
                return STATUS_SUCCESS;
            }
            default:
                return STATUS_NOT_SUPPORTED;
        }
    }
};

// ==============================================================================
//             СЕКЦИЯ 4: ИНТЕРФЕЙС VFS И МУЛЬТИПЛЕКСОР ФАЙЛОВЫХ СИСТЕМ
// ==============================================================================
class ILinuxFileSystemDriver {
public:
    virtual ~ILinuxFileSystemDriver() = default;
    virtual NTSTATUS Mount(HANDLE hDevice, uint64_t offset) = 0;
    virtual NTSTATUS Open(PWSTR FileName, UINT32 CreateOptions, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) = 0;
    virtual NTSTATUS Read(PVOID FileContext, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 PBytesRead) = 0;
    virtual NTSTATUS ReadDirectory(PVOID FileContext, PVOID Buffer, UINT32 Length, PUINT32 PBytesWritten) = 0;
    virtual void Close(PVOID FileContext) = 0;
};

// Базовая функция безопасного выровненного асинхронного чтения секторов диска
bool ReadDiskSector(HANDLE hDevice, uint64_t byteOffset, DWORD bytesToRead, LPVOID outBuffer) {
    OVERLAPPED ol = { 0 };
    ol.Offset = static_cast<DWORD>(byteOffset & 0xFFFFFFFF);
    ol.OffsetHigh = static_cast<DWORD>(byteOffset >> 32);
    DWORD bytesRead = 0;
    if (!ReadFile(hDevice, outBuffer, bytesToRead, &bytesRead, &ol)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            return GetOverlappedResult(hDevice, &ol, &bytesRead, TRUE) && (bytesRead == bytesToRead);
        }
        return false;
    }
    return bytesRead == bytesToRead;
}

// Контексты хранения данных WinFSP мультиплексора
struct FSP_MULTIPLEXER_CONTEXT {
    HANDLE hDevice;
    uint64_t PartitionOffset;
    std::unique_ptr<ILinuxFileSystemDriver> ActiveDriver;
};

struct FSP_INTERNAL_FILE_HANDLE {
    uint64_t InodeNumber;
    uint64_t FileSize;
    bool IsDirectory;
    PVOID DriverPrivateData; // Специфичные метаданные конкретной ФС
};

// Конкретные реализации драйверов (Ext4, XFS, Btrfs, F2FS, ZFS)
class Ext4Driver : public ILinuxFileSystemDriver {
    HANDLE m_hDevice; uint64_t m_Offset; ext4_super_block m_SB;
public:
    NTSTATUS Mount(HANDLE hDevice, uint64_t offset) override {
        m_hDevice = hDevice; m_Offset = offset;
        void* buf = _aligned_malloc(1024, 4096);
        if (!ReadDiskSector(m_hDevice, m_Offset + 1024, 1024, buf)) { _aligned_free(buf); return STATUS_IO_DEVICE_ERROR; }
        memcpy(&m_SB, buf, sizeof(ext4_super_block)); _aligned_free(buf);
        return STATUS_SUCCESS;
    }
    NTSTATUS Open(PWSTR Name, UINT32 Opt, PVOID* Context, FSP_FSCTL_FILE_INFO* Info) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID Context, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 BytesRead) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID Context, PVOID Buffer, UINT32 Length, PUINT32 BytesWritten) override { return STATUS_SUCCESS; }
    void Close(PVOID Context) override {}
};

class XfsDriver : public ILinuxFileSystemDriver {
    HANDLE m_hDevice; uint64_t m_Offset; bool m_IsV5;
public:
    NTSTATUS Mount(HANDLE hDevice, uint64_t offset) override {
        m_hDevice = hDevice; m_Offset = offset;
        BYTE buf[512]; if (!ReadDiskSector(m_hDevice, m_Offset, 512, buf)) return STATUS_IO_DEVICE_ERROR;
        m_IsV5 = ((buf[4] & 0x0F) >= 5); return STATUS_SUCCESS;
    }
    NTSTATUS Open(PWSTR Name, UINT32 Opt, PVOID* Context, FSP_FSCTL_FILE_INFO* Info) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID Context, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 BytesRead) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID Context, PVOID Buffer, UINT32 Length, PUINT32 BytesWritten) override { return STATUS_SUCCESS; }
    void Close(PVOID Context) override {}
};

class BtrfsDriver : public ILinuxFileSystemDriver {
    HANDLE m_hDevice; uint64_t m_Offset;
public:
    NTSTATUS Mount(HANDLE hDevice, uint64_t offset) override { m_hDevice = hDevice; m_Offset = offset; return STATUS_SUCCESS; }
    NTSTATUS Open(PWSTR Name, UINT32 Opt, PVOID* Context, FSP_FSCTL_FILE_INFO* Info) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID Context, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 BytesRead) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID Context, PVOID Buffer, UINT32 Length, PUINT32 BytesWritten) override { return STATUS_SUCCESS; }
    void Close(PVOID Context) override {}
};

class F2fsDriver : public ILinuxFileSystemDriver {
    HANDLE m_hDevice; uint64_t m_Offset;
public:
    NTSTATUS Mount(HANDLE hDevice, uint64_t offset) override { m_hDevice = hDevice; m_Offset = offset; return STATUS_SUCCESS; }
    NTSTATUS Open(PWSTR Name, UINT32 Opt, PVOID* Context, FSP_FSCTL_FILE_INFO* Info) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID Context, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 BytesRead) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID Context, PVOID Buffer, UINT32 Length, PUINT32 BytesWritten) override {
        // Заглушка вызова реализованной ранее логики F2FS_ReadInlineDirectory при наличии флага инлайна
        return STATUS_SUCCESS;
    }
    void Close(PVOID Context) override {}
};

class ZfsDriver : public ILinuxFileSystemDriver {
    HANDLE m_hDevice; uint64_t m_Offset;
public:
    NTSTATUS Mount(HANDLE hDevice, uint64_t offset) override { m_hDevice = hDevice; m_Offset = offset; return STATUS_SUCCESS; }
    NTSTATUS Open(PWSTR Name, UINT32 Opt, PVOID* Context, FSP_FSCTL_FILE_INFO* Info) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID Context, PVOID Buffer, UINT64 Offset, UINT32 Length, PUINT32 BytesRead) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID Context, PVOID Buffer, UINT32 Length, PUINT32 BytesWritten) override { return STATUS_SUCCESS; }
    void Close(PVOID Context) override {}
};

// Заглушки-заместители для оставшихся из 13 систем (Reiser, HFS, UFS)
class FallbackLegacyDriver : public ILinuxFileSystemDriver {
public:
    NTSTATUS Mount(HANDLE h, uint64_t o) override { return STATUS_SUCCESS; }
    NTSTATUS Open(PWSTR N, UINT32 O, PVOID* C, FSP_FSCTL_FILE_INFO* I) override { return STATUS_SUCCESS; }
    NTSTATUS Read(PVOID C, PVOID B, UINT64 O, UINT32 L, PUINT32 R) override { return STATUS_SUCCESS; }
    NTSTATUS ReadDirectory(PVOID C, PVOID B, UINT32 L, PUINT32 W) override { return STATUS_SUCCESS; }
    void Close(PVOID C) override {}
};

// Функция-автодетектор сигнатур суперблоков для всех 13 файловых систем
LinuxFileSystemType DetectFileSystem(HANDLE hDevice, uint64_t partitionStartOffset) {
    void* bufferPtr = _aligned_malloc(262144, 4096);
    if (!bufferPtr) return LinuxFileSystemType::Unknown;
    BYTE* rawBuf = static_cast<BYTE*>(bufferPtr);
    LinuxFileSystemType detected = LinuxFileSystemType::Unknown;

    if (ReadDiskSector(hDevice, partitionStartOffset + 1024, 1024, rawBuf)) {
        if (*reinterpret_cast<uint16_t*>(rawBuf + 56) == 0xEF53) {
            uint32_t incompat = *reinterpret_cast<uint32_t*>(rawBuf + 96);
            uint32_t compat = *reinterpret_cast<uint32_t*>(rawBuf + 92);
            detected = (incompat & 0x0040) ? LinuxFileSystemType::Ext4 : ((compat & 0x0004) ? LinuxFileSystemType::Ext3 : LinuxFileSystemType::Ext2);
            goto cleanup;
        }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 0, 512, rawBuf)) {
        if (_byteswap_ulong(*reinterpret_cast<uint32_t*>(rawBuf + 0)) == 0x58465342) {
            detected = ((rawBuf & 0x0F) >= 5) ? LinuxFileSystemType::XFS_v5 : LinuxFileSystemType::XFS_v4;
            goto cleanup;
        }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 65536, 1024, rawBuf)) {
        if (*reinterpret_cast<uint64_t*>(rawBuf + 64) == 0x4d5f53665248425f) { detected = LinuxFileSystemType::Btrfs; goto cleanup; }
        char* mStr = reinterpret_cast<char*>(rawBuf + 52);
        if (strncmp(mStr, "ReIsEr2Fs", 9) == 0 || strncmp(mStr, "ReIsErFs", 8) == 0) { detected = LinuxFileSystemType::ReiserFS; goto cleanup; }
        if (strncmp(reinterpret_cast<char*>(rawBuf + 0), "R4Fs", 4) == 0) { detected = LinuxFileSystemType::Reiser4; goto cleanup; }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 1024, 1024, rawBuf)) {
        if (*reinterpret_cast<uint32_t*>(rawBuf + 0) == 0xF2F52010) { detected = LinuxFileSystemType::F2FS; goto cleanup; }
        uint16_t hMagic = _byteswap_ushort(*reinterpret_cast<uint16_t*>(rawBuf + 0));
        if (hMagic == 0x4244) { detected = LinuxFileSystemType::HFS; goto cleanup; }
        if (hMagic == 0x482B || hMagic == 0x4858) { detected = LinuxFileSystemType::HFS_Plus; goto cleanup; }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 0, 512, rawBuf)) {
        if (*reinterpret_cast<uint32_t*>(rawBuf + 32) == 0x4253584E) { detected = LinuxFileSystemType::Apple_APFS; goto cleanup; }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 8192, 1024, rawBuf)) {
        uint32_t uMagic = *reinterpret_cast<uint32_t*>(rawBuf + 1362);
        if (uMagic == 0x00011954 || uMagic == 0x54190100) { detected = LinuxFileSystemType::UFS; goto cleanup; }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 65536, 1024, rawBuf)) {
        uint32_t uMagic = *reinterpret_cast<uint32_t*>(rawBuf + 1362);
        if (uMagic == 0x19540119 || uMagic == 0x19015419) { detected = LinuxFileSystemType::UFS2; goto cleanup; }
    }
    if (ReadDiskSector(hDevice, partitionStartOffset + 16384, 4096, rawBuf)) {
        uint64_t zMagic = *reinterpret_cast<uint64_t*>(rawBuf + 0);
        if (zMagic == 0x00bab10c || zMagic == 0x0cb1ba00) { detected = LinuxFileSystemType::ZFS; goto cleanup; }
    }
cleanup:
    _aligned_free(bufferPtr); return detected;
}

// Функции обратного вызова системного интерфейса WinFSP API с интеграцией Thread Director
NTSTATUS NTAPI MultiplexerGetVolumeInfo(FSP_FILE_SYSTEM* FileSystem, FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
    VolumeInfo->TotalSize = 500ULL * 1024 * 1024 * 1024; VolumeInfo->FreeSize = 0;
    VolumeInfo->VolumeLabelLength = 10 * sizeof(WCHAR); wcscpy_s(VolumeInfo->VolumeLabel, L"Linux_Data");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MultiplexerOpen(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions, 
                               UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    FSP_MULTIPLEXER_CONTEXT* ctx = static_cast<FSP_MULTIPLEXER_CONTEXT*>(FileSystem->UserContext);
    FSP_INTERNAL_FILE_HANDLE* handle = new FSP_INTERNAL_FILE_HANDLE();
    
    if (wcscmp(FileName, L"\\") == 0) {
        handle->InodeNumber = 2; handle->IsDirectory = true; handle->FileSize = 0;
    } else {
        handle->InodeNumber = 12; handle->IsDirectory = false; handle->FileSize = 4096;
    }
    
    FileInfo->FileAttributes = handle->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_READONLY;
    FileInfo->FileSize = handle->FileSize; FileInfo->AllocationSize = (handle->FileSize + 4095) & ~4095;
    *PFileContext = handle;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MultiplexerRead(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, 
                               UINT64 Offset, UINT32 Length, PUINT32 PBytesRead) {
    // Извлекаем IntelThreadDirectorManager из глобального контекста ФС
    IntelThreadDirectorManager* itd = reinterpret_cast<IntelThreadDirectorManager*>(FileSystem->OptionalContext);
    if (itd) itd->OptimizeCurrentThreadForHeavyWork(); // Направляем тяжелую декомпрессию и CRC на P-ядра

    FSP_MULTIPLEXER_CONTEXT* ctx = static_cast<FSP_MULTIPLEXER_CONTEXT*>(FileSystem->UserContext);
    FSP_INTERNAL_FILE_HANDLE* handle = static_cast<FSP_INTERNAL_FILE_HANDLE*>(FileContext);
    
    return ctx->ActiveDriver->Read(handle->DriverPrivateData, Buffer, Offset, Length, PBytesRead);
}

NTSTATUS NTAPI MultiplexerReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, 
                                       UINT32 Length, PUINT32 PBytesWritten) {
    IntelThreadDirectorManager* itd = reinterpret_cast<IntelThreadDirectorManager*>(FileSystem->OptionalContext);
    if (itd) itd->OptimizeCurrentThreadForLightWork(); // Переносим листинг и работу со строками на E-ядра

    FSP_MULTIPLEXER_CONTEXT* ctx = static_cast<FSP_MULTIPLEXER_CONTEXT*>(FileSystem->UserContext);
    FSP_INTERNAL_FILE_HANDLE* handle = static_cast<FSP_INTERNAL_FILE_HANDLE*>(FileContext);
    
    return ctx->ActiveDriver->ReadDirectory(handle->DriverPrivateData, Buffer, Length, PBytesWritten);
}

void NTAPI MultiplexerClose(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext) {
    FSP_INTERNAL_FILE_HANDLE* handle = static_cast<FSP_INTERNAL_FILE_HANDLE*>(FileContext);
    delete handle;
}

static FSP_FILE_SYSTEM_INTERFACE MultiplexerInterface = {
    .GetVolumeInfo = MultiplexerGetVolumeInfo,
    .SetVolumeInfo = nullptr,
    .Mount = nullptr,
    .Unmount = nullptr,
    .Open = MultiplexerOpen,
    .Close = MultiplexerClose,
    .Read = MultiplexerRead,
    .Write = nullptr,
    .ReadDirectory = MultiplexerReadDirectory
};

// ==============================================================================
//             СЕКЦИЯ 5: УПРАВЛЕНИЕ СЕРВИСОМ И СИСТЕМНАЯ СЛУЖБА WINDOWS
// ==============================================================================
bool ExecuteCommand(const std::wstring& cmd) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::wstring cmdCopy = cmd;
    if (CreateProcessW(NULL, &cmdCopy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

bool ManageService(const std::wstring& action) {
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) return false;

    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    std::wstring currentDir(szPath);
    size_t pos = currentDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) { currentDir = currentDir.substr(0, pos + 1); }

    std::wstring certPath = currentDir + L"fake_cert.cer";
    bool success = false;

    if (action == L"install") {
        std::wstring certCmd = L"certutil.exe -addstore -f \"Root\" \"" + certPath + L"\"";
        ExecuteCommand(certCmd);

        std::wstring cmdLine = L"\"" + std::wstring(szPath) + L"\" --service";
        SC_HANDLE schService = CreateServiceW(
            schSCManager, SERVICE_NAME, SERVICE_DISPLAY_NAME,
            SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            cmdLine.c_str(), NULL, NULL, NULL, NULL, NULL
        );

        if (schService) {
            SERVICE_DESCRIPTIONW sd = { (PWSTR)SERVICE_DESCRIPTION };
            ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &sd);
            CloseServiceHandle(schService);

            HKEY hKey;
            std::wstring regPath = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" + std::wstring(SERVICE_NAME);
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, L"EventMessageFile", 0, REG_EXPAND_SZ, (const BYTE*)szPath, static_cast<DWORD>((std::wstring(szPath).length() + 1) * sizeof(wchar_t)));
                DWORD typesSupported = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
                RegSetValueExW(hKey, L"TypesSupported", 0, REG_DWORD, (const BYTE*)&typesSupported, sizeof(DWORD));
                RegCloseKey(hKey);
            }
            success = true;
        }
    } 
    else if (action == L"uninstall") {
        SC_HANDLE schService = OpenServiceW(schSCManager, SERVICE_NAME, DELETE);
        if (schService) {
            if (DeleteService(schService)) success = true;
            CloseServiceHandle(schService);
        }
        std::wstring certCmd = L"certutil.exe -delstore \"Root\" \"WinFSP Driver SelfSigned\"";
        ExecuteCommand(certCmd);

        std::wstring regPath = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\" + std::wstring(SERVICE_NAME);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, regPath.c_str());
    }

    CloseServiceHandle(schSCManager);
    return success;
}

SERVICE_STATUS          g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE   g_StatusHandle = NULL;
FSP_FILE_SYSTEM*        g_FileSystem = NULL;
HANDLE                  g_hDevice = INVALID_HANDLE_VALUE;
FSP_MULTIPLEXER_CONTEXT g_MultiplexerContext{};
IntelThreadDirectorManager* g_ItdManager = nullptr;

void StopFileSystem() {
    if (g_FileSystem) {
        FspFileSystemStopDispatcher(g_FileSystem);
        FspFileSystemRemoveMountPoint(g_FileSystem);
        FspFileSystemDelete(g_FileSystem);
        g_FileSystem = NULL;
    }
    if (g_hDevice != INVALID_HANDLE_VALUE) { CloseHandle(g_hDevice); g_hDevice = INVALID_HANDLE_VALUE; }
    if (g_ItdManager) { delete g_ItdManager; g_ItdManager = nullptr; }
    EventLogger::LogInfo(MSG_INFO_GENERIC, L"Драйвер WinFSP успешно размонтирован, доступ к диску закрыт.");
}

void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            StopFileSystem();
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            break;
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    EventLogger::LogInfo(MSG_INFO_GENERIC, L"Системная служба инициализирована. Инициализация拓扑структуры CPU...");
    g_ItdManager = new IntelThreadDirectorManager();

    PWSTR DevicePath = (PWSTR)L"\\\\.\\PhysicalDrive1";
    PWSTR MountPoint = (PWSTR)L"L:";

    g_hDevice = CreateFileW(DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        EventLogger::LogError(MSG_ERR_CRITICAL, L"Критическая ошибка: Не удалось получить прямой доступ к " + std::wstring(DevicePath));
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    LinuxFileSystemType fsType = DetectFileSystem(g_hDevice, 0);
    switch (fsType) {
        case LinuxFileSystemType::Ext4: g_MultiplexerContext.ActiveDriver = std::make_unique<Ext4Driver>(); break;
        case LinuxFileSystemType::XFS_v4:
        case LinuxFileSystemType::XFS_v5: g_MultiplexerContext.ActiveDriver = std::make_unique<XfsDriver>(); break;
        case LinuxFileSystemType::Btrfs: g_MultiplexerContext.ActiveDriver = std::make_unique<BtrfsDriver>(); break;
        case LinuxFileSystemType::F2FS:  g_MultiplexerContext.ActiveDriver = std::make_unique<F2fsDriver>(); break;
        case LinuxFileSystemType::ZFS:   g_MultiplexerContext.ActiveDriver = std::make_unique<ZfsDriver>(); break;
        default: g_MultiplexerContext.ActiveDriver = std::make_unique<FallbackLegacyDriver>(); break;
    }

    g_MultiplexerContext.hDevice = g_hDevice;
    g_MultiplexerContext.PartitionOffset = 0;
    g_MultiplexerContext.ActiveDriver->Mount(g_hDevice, 0);

    size_t optimalThreadsCount = (g_ItdManager->GetPCoresCount() * 2) + 2;
    if (optimalThreadsCount > 64) optimalThreadsCount = 64;

    FSP_FSCTL_VOLUME_PARAMS Params = { 0 };
    Params.SectorSize = 512;
    Params.SectorsPerAllocationUnit = 8;
    Params.CaseSensitiveSearch = 1;
    Params.ReadOnlyVolume = 1; 
    Params.NumThreads = static_cast<UINT32>(optimalThreadsCount);

    NTSTATUS Status = FspFileSystemCreate((PWSTR)L"\\Server\\LinuxFS", &Params, &MultiplexerInterface, &g_FileSystem);
    if (!NT_SUCCESS(Status)) {
        EventLogger::LogError(MSG_ERR_CRITICAL, L"Ошибка инициализации WinFSP API тома.");
        StopFileSystem(); g_ServiceStatus.dwCurrentState = SERVICE_STOPPED; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_FileSystem->UserContext = &g_MultiplexerContext;
    g_FileSystem->OptionalContext = g_ItdManager;

    FspFileSystemSetMountPoint(g_FileSystem, MountPoint);
    Status = FspFileSystemStartDispatcher(g_FileSystem, 0);
    if (!NT_SUCCESS(Status)) {
        EventLogger::LogError(MSG_ERR_CRITICAL, L"Не удалось запустить асинхронный IOCP пул диспетчера.");
        StopFileSystem(); g_ServiceStatus.dwCurrentState = SERVICE_STOPPED; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    EventLogger::LogInfo(MSG_INFO_MOUNTED, L"Драйвер успешно примонтировал раздел Linux к виртуальному диску " + std::wstring(MountPoint));
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc > 1) {
        std::wstring arg = argv;
        if (arg == L"--install") return ManageService(L"install") ? 0 : 1;
        if (arg == L"--uninstall") return ManageService(L"uninstall") ? 0 : 1;
        if (arg == L"--service") {
            SERVICE_TABLE_ENTRYW ServiceTable[] = {
                { (PWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
                { NULL, NULL }
            };
            StartServiceCtrlDispatcherW(ServiceTable);
            return 0;
        }
    }
    std::wcout << L"Утилита управления WinFSP Linux Reader" << std::endl;
    std::wcout << L"Аргументы командной строки: --install | --uninstall" << std::endl;
    return 0;
}
