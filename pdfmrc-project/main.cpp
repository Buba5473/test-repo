#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cwchar>
#include <functional> // Исправлено: Обязательно для std::function

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

// Подключение заголовочных файлов MuPDF
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

// ============================================================================
// 1. БЕЗОПАСНОЕ УПРАВЛЕНИЕ РЕСУРСАМИ (RAII)
// ============================================================================
struct FzContextDeleter { void operator()(fz_context* ctx) const { if (ctx) fz_drop_context(ctx); } };
using FzContextPtr = std::unique_ptr<fz_context, FzContextDeleter>;

template <typename T, void (*DropFunc)(fz_context*, T*)>
class MuPDFPtr {
private:
    fz_context* m_ctx;
    T* m_ptr;
public:
    MuPDFPtr(fz_context* ctx, T* ptr) : m_ctx(ctx), m_ptr(ptr) {}
    ~MuPDFPtr() { if (m_ctx && m_ptr) DropFunc(m_ctx, m_ptr); }
    T* get() const { return m_ptr; }
    T* release() { T* tmp = m_ptr; m_ptr = nullptr; return tmp; }
    operator T*() const { return m_ptr; }
};

// ============================================================================
// 2. СИСТЕМНАЯ ЛОКАЛИЗАЦИЯ И ИСПРАВЛЕНИЕ КИРИЛЛИЦЫ (UTF-8)
// ============================================================================
class Localizer {
private:
    bool is_ru = false;
public:
    Localizer() {
#ifdef _WIN32
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        
        LANGID lang = GetUserDefaultUILanguage();
        if ((lang & 0xFF) == LANG_RUSSIAN) {
            is_ru = true;
        }
#else
        const char* lang = std::getenv("LANG");
        if (lang && std::string(lang).find("ru") != std::string::npos) {
            is_ru = true;
        }
#endif
    }

    std::string get(const std::string& en, const std::string& ru) const {
        return is_ru ? ru : en;
    }

    static std::string to_utf8(const std::wstring& wstr) {
#ifdef _WIN32
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str_to(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str_to[0], size_needed, NULL, NULL);
        return str_to;
#else
        return std::string(wstr.begin(), wstr.end());
#endif
    }
};

static Localizer g_localizer;

// ============================================================================
// 3. МАТЕМАТИЧЕСКИЙ ДВИЖОК СЕГМЕНТАЦИИ (MRC / CIELab / Sauvola Adaptive)
// ============================================================================
struct LabColor { float L, a, b; };

class ColorEngine {
public:
    static LabColor rgb_to_lab(uint8_t r, uint8_t g, uint8_t b) {
        float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
        rf = (rf > 0.04045f) ? std::pow((rf + 0.055f) / 1.055f, 2.4f) : (rf / 12.92f);
        gf = (gf > 0.04045f) ? std::pow((gf + 0.055f) / 1.055f, 2.4f) : (gf / 12.92f);
        bf = (bf > 0.04045f) ? std::pow((bf + 0.055f) / 1.055f, 2.4f) : (bf / 12.92f);

        float x = rf * 0.4124564f + gf * 0.3575761f + bf * 0.1804375f;
        float y = rf * 0.2126729f + gf * 0.7151522f + bf * 0.0721750f;
        float z = rf * 0.0193339f + gf * 0.1191920f + bf * 0.9503041f;

        x /= 0.95047f; y /= 1.00000f; z /= 1.08883f;
        x = (x > 0.008856f) ? std::pow(x, 1.0f/3.0f) : (7.787f * x + 16.0f/116.0f);
        y = (y > 0.008856f) ? std::pow(y, 1.0f/3.0f) : (7.787f * y + 16.0f/116.0f);
        z = (z > 0.008856f) ? std::pow(z, 1.0f/3.0f) : (7.787f * z + 16.0f/116.0f);

        return { (116.0f * y) - 16.0f, 500.0f * (x - y), 200.0f * (y - z) };
    }

    static float delta_e76(const LabColor& c1, const LabColor& c2) {
        return std::sqrt(std::pow(c1.L - c2.L, 2) + std::pow(c1.a - c2.a, 2) + std::pow(c1.b - c2.b, 2));
    }
};

class MrcSegmenter {
public:
    static void compute_integral_images(int w, int h, const uint8_t* gray, 
                                        std::vector<double>& integral, 
                                        std::vector<double>& integral_sq) {
        integral.assign((w + 1) * (h + 1), 0.0);
        integral_sq.assign((w + 1) * (h + 1), 0.0);

        for (int y = 0; y < h; ++y) {
            double row_sum = 0.0;
            double row_sq_sum = 0.0;
            for (int x = 0; x < w; ++x) {
                double val = gray[y * w + x];
                row_sum += val;
                row_sq_sum += val * val;
                
                int idx = (y + 1) * (w + 1) + (x + 1);
                int idx_top = y * (w + 1) + (x + 1);
                
                integral[idx] = integral[idx_top] + row_sum;
                integral_sq[idx] = integral_sq[idx_top] + row_sq_sum;
            }
        }
    }

    static void segment_page_adaptive_dpi(fz_context* ctx, fz_pixmap* source, fz_pixmap* mask, 
                                          fz_pixmap* bg, fz_pixmap* fg, float dpi) {
        int w = fz_pixmap_width(ctx, source);
        int h = fz_pixmap_height(ctx, source);
        int src_n = fz_pixmap_components(ctx, source);
        
        uint8_t* src_s = fz_pixmap_samples(ctx, source);
        uint8_t* msk_s = fz_pixmap_samples(ctx, mask);
        uint8_t* bg_s  = fz_pixmap_samples(ctx, bg);
        uint8_t* fg_s  = fz_pixmap_samples(ctx, fg);

        int window_size = static_cast<int>(dpi * 0.11f);
        if (window_size % 2 == 0) window_size += 1;
        window_size = std::max(7, std::min(window_size, 65));
        int whalf = window_size / 2;

        int noise_threshold_neighbors = (dpi > 200.0f) ? 3 : 2;

        std::vector<uint8_t> gray_buf(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t* p = src_s + (y * source->stride) + (x * src_n);
                gray_buf[y * w + x] = static_cast<uint8_t>(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
            }
        }

        std::vector<double> int_img;
        std::vector<double> int_sq_img;
        compute_integral_images(w, h, gray_buf.data(), int_img, int_sq_img);

        const double k = 0.22;  
        const double R = 128.0; 

        for (int y = 0; y < h; ++y) {
            int y1 = std::max(0, y - whalf);
            int y2 = std::min(h - 1, y + whalf);

            for (int x = 0; x < w; ++x) {
                int x1 = std::max(0, x - whalf);
                int x2 = std::min(w - 1, x + whalf);

                double area = (y2 - y1 + 1) * (x2 - x1 + 1);

                int idx00 = y1 * (w + 1) + x1;
                int idx10 = (y2 + 1) * (w + 1) + x1;
                int idx01 = y1 * (w + 1) + (x2 + 1);
                int idx11 = (y2 + 1) * (w + 1) + (x2 + 1);

                double mean = (int_img[idx11] - int_img[idx10] - int_img[idx01] + int_img[idx00]) / area;
                double sq_sum = (int_sq_img[idx11] - int_sq_img[idx10] - int_sq_img[idx01] + int_sq_img[idx00]) / area;
                double variance = sq_sum - (mean * mean);
                double stddev = std::sqrt(std::max(0.0, variance));

                double threshold = mean * (1.0 + k * ((stddev / R) - 1.0));

                uint8_t current_gray = gray_buf[y * w + x];
                uint8_t* p_src = src_s + (y * source->stride) + (x * src_n);
                uint8_t* p_msk = msk_s + (y * mask->stride) + x;
                uint8_t* p_bg  = bg_s  + (y * bg->stride)  + (x * 3);
                uint8_t* p_fg  = fg_s  + (y * fg->stride)  + (x * 3);

                if (current_gray < threshold) {
                    *p_msk = 255; 
                    p_fg[0] = p_src[0]; p_fg[1] = p_src[1]; p_fg[2] = p_src[2];
                    p_bg[0] = 255;      p_bg[1] = 255;      p_bg[2] = 255;
                } else {
                    *p_msk = 0;   
                    p_fg[0] = 0;   p_fg[1] = 0;   p_fg[2] = 0;
                    p_bg[0] = p_src[0]; p_bg[1] = p_src[1]; p_bg[2] = p_src[2];
                }
            }
        }

        apply_adaptive_morphology(msk_s, w, h, mask->stride, noise_threshold_neighbors);
    }

private:
    static void apply_adaptive_morphology(uint8_t* mask_samples, int w, int h, int stride, int min_neighbors) {
        std::vector<uint8_t> temp_mask(mask_samples, mask_samples + (h * stride));
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                int idx = y * stride + x;
                if (temp_mask[idx] == 255) {
                    int neighbors = 0;
                    for (int ny = -1; ny <= 1; ++ny) {
                        for (int nx = -1; nx <= 1; ++nx) {
                            if (temp_mask[(y + ny) * stride + (x + nx)] == 255) neighbors++;
                        }
                    }
                    if (neighbors <= min_neighbors) {
                        mask_samples[idx] = 0; 
                    }
                }
            }
        }
    }
};

// ============================================================================
// 4. МНОГОПОТОЧНЫЙ ПУЛ ПОТОКОВ (ThreadPool)
// ============================================================================
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool stop = false;
public:
    ThreadPool(size_t threads) {
        for (size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->cv.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }
    void enqueue(std::function<void()> f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(f);
        }
        cv.notify_one();
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        for (std::thread& worker : workers) if (worker.joinable()) worker.join();
    }
};

// ============================================================================
// 5. ИНТЕГРАЦИЯ ЗАПИСИ СЛОЕВ В MUPDF (XOBJECT INJECTION ENGINE)
// ============================================================================
pdf_obj* embed_pixmap_as_xobject(fz_context* ctx, pdf_document* pdf, fz_pixmap* pix, bool is_mask, int quality) {
    // В официальном API MuPDF 1.27.2 pixmap конвертируется в готовый объект Image,
    // который автоматически упаковывает растр и прописывает Width, Height и BitsPerComponent.
    fz_image* img = fz_new_image_from_pixmap(ctx, pix, NULL);
    
    // Передаем созданное изображение во внутреннюю структуру PDF в виде XObject-ссылки
    pdf_obj* xobject = pdf_add_image(ctx, pdf, img);
    
    // Освобождаем временный контейнер fz_image (деструктор RAII через счетчик ссылок)
    fz_drop_image(ctx, img);

    // Дополнительная модификация параметров словаря изображения (при необходимости)
    if (is_mask) {
        pdf_dict_put(ctx, xobject, PDF_NAME(ColorSpace), PDF_NAME(DeviceGray));
    } else {
        pdf_dict_put(ctx, xobject, PDF_NAME(ColorSpace), PDF_NAME(DeviceRGB));
    }

    return xobject;
}

void inject_mrc_layers_to_page(fz_context* ctx, pdf_document* pdf, int page_num, 
                               fz_pixmap* mask_pix, fz_pixmap* bg_pix, fz_pixmap* fg_pix, int quality) {
    // В MuPDF 1.27.2 структуры pdf_page скрыты. Загружаем и извлекаем низкоуровневый словарь страницы
    pdf_obj* page_obj = pdf_to_dict(ctx, pdf_new_indirect(ctx, pdf, pdf_lookup_page_number(ctx, pdf, page_num), 0));

    // Создаем аппаратные XObject-объекты для каждого слоя MRC
    pdf_obj* x_mask = embed_pixmap_as_xobject(ctx, pdf, mask_pix, true, quality);
    pdf_obj* x_bg = embed_pixmap_as_xobject(ctx, pdf, bg_pix, false, quality);
    pdf_obj* x_fg = embed_pixmap_as_xobject(ctx, pdf, fg_pix, false, quality);

    // Безопасное извлечение/создание ресурсного дерева /Resources
    pdf_obj* res_obj = pdf_dict_get(ctx, page_obj, PDF_NAME(Resources));
    if (!res_obj) {
        res_obj = pdf_new_dict(ctx, pdf, 2);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Resources), res_obj);
        pdf_drop_obj(ctx, res_obj);
    }
    
    // Извлечение/создание подсловаря ресурсов /XObject
    pdf_obj* xobj_dict = pdf_dict_get(ctx, res_obj, PDF_NAME(XObject));
    if (!xobj_dict) {
        xobj_dict = pdf_new_dict(ctx, pdf, 4);
        pdf_dict_put(ctx, res_obj, PDF_NAME(XObject), xobj_dict);
        pdf_drop_obj(ctx, xobj_dict);
    }

    // Регистрация слоев в дереве ресурсов страницы с явным приведением строковых имен к PDF_NAME
    std::string mask_id = "MrcMask" + std::to_string(page_num);
    std::string bg_id = "MrcBg" + std::to_string(page_num);
    std::string fg_id = "MrcFg" + std::to_string(page_num);

    pdf_dict_put(ctx, xobj_dict, pdf_new_name(ctx, pdf, mask_id.c_str()), x_mask);
    pdf_dict_put(ctx, xobj_dict, pdf_new_name(ctx, pdf, bg_id.c_str()), x_bg);
    pdf_dict_put(ctx, xobj_dict, pdf_new_name(ctx, pdf, bg_id.c_str()), x_fg);

    // Переписывание контента страницы (Content Stream): Сборка MRC пирога
    std::string content_stream = 
        "q\n"                             
        " /" + bg_id + " Do\n"            
        "q\n"                             
        " /" + mask_id + " BI\n"          
        " /" + fg_id + " Do\n"            
        "Q\n"                             
        "Q\n";                            

    // Выделение буфера под сборочный контент-стрим
    fz_buffer* contents_buf = fz_new_buffer_from_copied_data(ctx, (unsigned char*)content_stream.c_str(), content_stream.length());
    pdf_obj* new_contents = pdf_new_dict(ctx, pdf, 2);
    
    // Передаем 5 параметров согласно новому API xref.h (последний параметр 0 указывает, что поток сырой)
    pdf_update_stream(ctx, pdf, new_contents, contents_buf, 0);
    pdf_dict_put(ctx, page_obj, PDF_NAME(Contents), new_contents);

    // Каскадное освобождение памяти (RAII зачистка дескрипторов)
    fz_drop_buffer(ctx, contents_buf);
    pdf_drop_obj(ctx, new_contents);
    pdf_drop_obj(ctx, x_mask);
    pdf_drop_obj(ctx, x_bg);
    pdf_drop_obj(ctx, x_fg);
}

// ============================================================================
// 6. ИНТЕРФЕЙС И КРОССПЛАТФОРМЕННАЯ ИНТЕГРАЦИЯ В ОС
// ============================================================================
#ifdef _WIN32
HWND g_hwndProgressBar = NULL;
HWND g_hwndMainDialog = NULL;

struct UserConfig {
    int dpi = 150;
    int level = 3;
    bool confirmed = false;
};

// Реализация меню выбора на базе нативного Task Dialog Windows 10
void ShowUserConfigDialog(UserConfig& config) {
    const TASKDIALOG_BUTTON radioButtons[] = {
        { 1, L"Максимальное сжатие (Уровень 1)\nНизкое качество фона" },
        { 2, L"Высокое сжатие (Уровень 2)" },
        { 3, L"Сбалансированный режим (Уровень 3)\nРекомендуется для документов" },
        { 4, L"Высокое качество (Уровень 4)" },
        { 5, L"Максимальное качество (Уровень 5)\nТекст бритвенной резкости" }
    };

    TASKDIALOGCONFIG tc = { sizeof(TASKDIALOGCONFIG) };
    tc.hwndParent = NULL;
    tc.hInstance = GetModuleHandle(NULL);
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    tc.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
    tc.pszWindowTitle = L"PDFMRC — Параметры сжатия";
    tc.pszMainInstruction = L"Настройка интеллектуального MRC-сжатия";
    tc.pszContent = L"Выберите желаемый уровень компрессии документа. Маска текста будет выделена автоматически с адаптивным шагом DPI.";
    
    tc.cRadioButtons = 5;
    tc.pRadioButtons = radioButtons;
    tc.nDefaultRadioButton = 3; 

    int selectedButton = 0;
    int selectedRadioButton = 0;
    BOOL radioChecked = FALSE;

    HRESULT hr = TaskDialogIndirect(&tc, &selectedButton, &selectedRadioButton, &radioChecked);

    if (SUCCEEDED(hr) && selectedButton == IDOK) {
        config.level = selectedRadioButton;
        if (config.level <= 2) config.dpi = 110;      
        else if (config.level == 3) config.dpi = 150; 
        else config.dpi = 300;                        
        
        config.confirmed = true;
    } else {
        config.confirmed = false;
    }
}

void init_gui(HINSTANCE hInstance, const std::wstring& title) {
    InitCommonControls();
    g_hwndMainDialog = CreateWindowExW(0, L"STATIC", title.c_str(), 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 90, NULL, NULL, hInstance, NULL);
    
    g_hwndProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL, 
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, 15, 385, 25, g_hwndMainDialog, NULL, hInstance, NULL);
        
    SendMessageW(g_hwndProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
}

void update_progress(int percent) {
    if (g_hwndProgressBar) {
        SendMessageW(g_hwndProgressBar, PBM_SETPOS, percent, 0);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void install_sendto_shortcut() {
    wchar_t sendto_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_SENDTO, NULL, 0, sendto_path))) {
        std::wstring shortcut_file = std::wstring(sendto_path) + L"\\Сжать в PDFMRC.lnk";
        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        
        IShellLinkW* psl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl))) {
            psl->SetPath(exe_path);
            psl->SetArguments(L"--sendto-mode"); 
            IPersistFile* ppf;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
                ppf->Save(shortcut_file.c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
    }
}

void uninstall_sendto_shortcut() {
    wchar_t sendto_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_SENDTO, NULL, 0, sendto_path))) {
        std::wstring shortcut_file = std::wstring(sendto_path) + L"\\Сжать в PDFMRC.lnk";
        DWORD attributes = GetFileAttributesW(shortcut_file.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(shortcut_file.c_str());
        }
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\pdfmrc");
}
#else
// ВЕРСИЯ ДЛЯ LINUX: РЕНДЕРИНГ ПРОГРЕССА И СИСТЕМНАЯ ИНТЕГРАЦИЯ CONTEXT MENU
void update_progress(int percent) {
    int barWidth = 30;
    std::cout << "\r[";
    int pos = (barWidth * percent) / 100;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "■";
        else std::cout << " ";
    }
    std::cout << "] " << percent << " % " << std::flush;
}

void write_file(const std::string& path, const std::string& content) {
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fputs(content.c_str(), f); fclose(f); }
}

void linux_install_integration() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (home.empty()) return;

    // 1. Копируем иконку в системную директорию пиктограмм
    std::system("mkdir -p ~/.local/share/icons/hicolor/256x256/apps/");
    std::system("cp icon.png ~/.local/share/icons/hicolor/256x256/apps/pdfmrc.png 2>/dev/null || true");

    // 2. Создаем стандартный xdg .desktop файл для ассоциаций приложений
    std::string desktop_entry = 
        "[Desktop Entry]\nType=Application\nName=pdfmrc Compressor\nName[ru]=Сжать в PDFMRC\n"
        "Exec=pdfmrc %f\nIcon=pdfmrc\nMimeType=application/pdf;\nNoDisplay=true\n";
    write_file(home + "/.local/share/applications/pdfmrc.desktop", desktop_entry);

    // 3. ИНТЕГРАЦИЯ В GNOME (Nautilus Scripts)
    std::string nautilus_path = home + "/.local/share/nautilus/scripts";
    std::system(("mkdir -p " + nautilus_path).c_str());
    std::string nautilus_script = "#!/bin/bash\npdfmrc \"$1\"\n";
    write_file(nautilus_path + "/Сжать в PDFMRC", nautilus_script);
    std::system(("chmod +x \"" + nautilus_path + "/Сжать в PDFMRC\"").c_str());

    // 4. ИНТЕГРАЦИЯ В KDE PLASMA (Dolphin Service Menus)
    std::string kde_path = home + "/.local/share/kio/servicemenus";
    std::system(("mkdir -p " + kde_path).c_str());
    std::string kde_desktop = 
        "[Desktop Entry]\nType=Service\nMimeType=application/pdf;\nActions=compressPdfMrc;\n"
        "X-KDE-Priority=TopLevel\n[Desktop Action compressPdfMrc]\nName=Compress with PDFMRC\n"
        "Name[ru]=Сжать в PDFMRC\nIcon=pdfmrc\nExec=pdfmrc %f\n";
    write_file(kde_path + "/pdfmrc_service.desktop", kde_desktop);

    // 5. ИНТЕГРАЦИЯ В CINNAMON (Nemo Actions)
    std::string nemo_path = home + "/.local/share/nemo/actions";
    std::system(("mkdir -p " + nemo_path).c_str());
    std::string nemo_action = 
        "[Nemo Action]\nActive=true\nName=Compress with PDFMRC\nName[ru]=Сжать в PDFMRC\n"
        "Comment=Smart Mixed Raster Content PDF Compressor\nComment[ru]=Умное MRC сжатие PDF документа\n"
        "Exec=pdfmrc %F\nIcon-Name=pdfmrc\nSelection=s\nExtensions=pdf;PDF;\n";
    write_file(nemo_path + "/pdfmrc.nemo_action", nemo_action);

    // 6. ОБНОВЛЕНИЕ СИСТЕМНЫХ БАЗ ДАННЫХ ДЛЯ XFCE / THUNAR
    std::system("update-desktop-database ~/.local/share/applications/ 2>/dev/null || true");

    std::cout << "Интеграция в контекстное меню (GNOME/KDE/XFCE/Cinnamon) выполнена успешно." << std::endl;
}

void linux_uninstall_integration() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (home.empty()) return;

    std::string path1 = home + "/.local/share/applications/pdfmrc.desktop";
    std::string path2 = home + "/.local/share/nautilus/scripts/Сжать в PDFMRC";
    std::string path3 = home + "/.local/share/kio/servicemenus/pdfmrc_service.desktop";
    std::string path4 = home + "/.local/share/nemo/actions/pdfmrc.nemo_action";
    std::string path5 = home + "/.local/share/icons/hicolor/256x256/apps/pdfmrc.png";

    std::remove(path1.c_str()); std::remove(path2.c_str());
    std::remove(path3.c_str()); std::remove(path4.c_str());
    std::remove(path5.c_str());

    std::system("update-desktop-database ~/.local/share/applications/ 2>/dev/null || true");
    std::cout << "Компоненты интеграции контекстного меню Linux успешно удалены." << std::endl;
}
#endif

// ============================================================================
// 7. ГЛАВНЫЙ КОНВЕЙЕР УПРАВЛЕНИЯ И ПАРСИНГ АРГУМЕНТОВ
// ============================================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    CoInitialize(NULL);
#endif

    std::string input_file = "";
    std::string output_file = "output_compressed.pdf";
    bool run_gui = false;
    bool sendto_mode = false;
    int target_dpi = 150;      
    int compression_level = 3; 

    // Справка по аргументам командной строки (Двуязычная)
    if (argc == 1 || (argc == 2 && (std::string(argv) == "--help" || std::string(argv) == "-h"))) {
        std::cout << g_localizer.get(
            "======================================================================\n"
            " pdfmrc — Smart Mixed Raster Content (MRC) PDF Compressor\n"
            "======================================================================\n"
            "Usage: pdfmrc <input.pdf> [options]\n\n"
            "Options:\n"
            "  <input.pdf>          Path to the source PDF file for compression.\n"
            "  --dpi <value>        Target resolution (e.g., 72, 150, 300). Default: 150\n"
            "  --level <1-5>        Compression level (1=Max Compression, 5=Max Quality). Default: 3\n"
            "  --threads <value>    Limit the number of concurrent CPU threads used.\n"
            "  --gui                Enable native Windows graphical progress bar.\n"
            "  --install-sendto     Create a shortcut in the Windows 'Send To' menu.\n"
            "  --uninstall          Remove shortcuts and clean application footprints.\n"
            "  -h, --help           Show this help message and exit.\n"
            "======================================================================\n",
            
            "======================================================================\n"
            " pdfmrc — Умный компрессор PDF по технологии Mixed Raster Content (MRC)\n"
            "======================================================================\n"
            "Использование: pdfmrc <входной.pdf> [опции]\n\n"
            "Опции:\n"
            "  <входной.pdf>        Путь к исходному файлу PDF для сжатия.\n"
            "  --dpi <значение>     Целевое разрешение рендеринга (72, 150, 300). Дефолт: 150\n"
            "  --level <1-5>        Уровень сжатия (1=Макс. сжатие, 5=Макс. качество). Дефолт: 3\n"
            "  --threads <значение> Ограничить количество одновременно используемых потоков CPU.\n"
            "  --gui                Включить нативный графический прогресс-бар Windows.\n"
            "  --install-sendto     Создать ярлык в системном меню Проводника 'Отправить'.\n"
            "  --uninstall          Удалить ярлыки и очистить следы приложения в системе.\n"
            "  -h, --help           Показать эту справочную информацию и выйти.\n"
            "======================================================================\n"
        );
        return 0;
    }

    int max_threads = std::thread::hardware_concurrency();
    if (max_threads < 1) max_threads = 1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gui") {
            run_gui = true;
        } else if (arg == "--sendto-mode") {
            sendto_mode = true;
            run_gui = true; 
        } else if (arg == "--dpi" && i + 1 < argc) {
            target_dpi = std::stoi(argv[++i]);
        } else if (arg == "--level" && i + 1 < argc) {
            compression_level = std::stoi(argv[++i]);
            if (compression_level < 1) compression_level = 1;
            if (compression_level > 5) compression_level = 5;
        } else if (arg == "--threads" && i + 1 < argc) {
            int requested_threads = std::stoi(argv[++i]);
            if (requested_threads > 0 && requested_threads <= max_threads) {
                max_threads = requested_threads;
            }
        } else if (arg == "--install-sendto") {
#ifdef _WIN32
            install_sendto_shortcut();
            std::cout << g_localizer.get("SendTo shortcut created successfully.\n", "Ярлык в меню 'Отправить' успешно создан.\n");
#else
            linux_install_integration();
#endif
            return 0;
        } else if (arg == "--uninstall") {
#ifdef _WIN32
            uninstall_sendto_shortcut();
            std::cout << g_localizer.get("Application components uninstalled.\n", "Компоненты приложения успешно удалены из системы.\n");
#else
            linux_uninstall_integration();
#endif
            return 0;
        } else if (input_file.empty() && arg.find("--") != 0) {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << g_localizer.get("Error: Input file not specified.\n", "Ошибка: Не указан входной файл.\n");
        return 1;
    }

#ifdef _WIN32
    if (sendto_mode) {
        UserConfig win_config;
        ShowUserConfigDialog(win_config);
        if (!win_config.confirmed) return 0; 
        target_dpi = win_config.dpi;
        compression_level = win_config.level;
    }

    if (run_gui) {
        std::wstring gui_title = L"PDFMRC: " + std::wstring(input_file.begin(), input_file.end());
        init_gui(GetModuleHandle(NULL), gui_title);
    }
#endif

    FzContextPtr ctx(fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED));
    if (!ctx) {
        std::cerr << g_localizer.get("Failed to initialize MuPDF backend.\n", "Критическая ошибка инициализации движка MuPDF.\n");
        return 1;
    }
    fz_register_document_handlers(ctx.get());

    try {
        pdf_document* pdf = pdf_open_document(ctx.get(), input_file.c_str());
        int page_count = pdf_count_pages(ctx.get(), pdf);

        if (!run_gui) {
            std::cout << g_localizer.get("Processing document...", "Обработка документа...") << std::endl;
            std::cout << g_localizer.get("Target Resolution: ", "Целевое разрешение: ") << target_dpi << " DPI" << std::endl;
            std::cout << g_localizer.get("Compression Level: ", "Уровень сжатия: ") << compression_level << " / 5" << std::endl;
            std::cout << g_localizer.get("Threads Allocated: ", "Выделено потоков CPU: ") << max_threads << std::endl;
        }

        std::mutex mrc_mutex;
        int completed_pages = 0;

        {
            ThreadPool pool(max_threads);

            for (int i = 0; i < page_count; ++i) {
                pool.enqueue([&ctx, pdf, i, page_count, &mrc_mutex, &completed_pages, target_dpi, compression_level]() {
                    fz_page* page = pdf_load_page(ctx.get(), pdf, i);
                    
                    float scale = static_cast<float>(target_dpi) / 72.0f;
                    fz_matrix ctm = fz_scale(scale, scale);

                    fz_pixmap* src_pix = fz_new_pixmap_from_page(ctx.get(), page, ctm, fz_device_rgb(ctx.get()), 0);
                    int w = fz_pixmap_width(ctx.get(), src_pix);
                    int h = fz_pixmap_height(ctx.get(), src_pix);

                    fz_pixmap* msk_pix = fz_new_pixmap(ctx.get(), fz_device_gray(ctx.get()), w, h, NULL, 0);
                    fz_pixmap* bg_pix  = fz_new_pixmap(ctx.get(), fz_device_rgb(ctx.get()), w, h, NULL, 0);
                    fz_pixmap* fg_pix  = fz_new_pixmap(ctx.get(), fz_device_rgb(ctx.get()), w, h, NULL, 0);

                    MrcSegmenter::segment_page_adaptive_dpi(ctx.get(), src_pix, msk_pix, bg_pix, fg_pix, static_cast<float>(target_dpi));

                    {
                        std::lock_guard<std::mutex> lock(mrc_mutex);
                        inject_mrc_layers_to_page(ctx.get(), pdf, i, msk_pix, bg_pix, fg_pix, compression_level);
                        completed_pages++;
                        update_progress((completed_pages * 100) / page_count);
                    }

                    fz_drop_pixmap(ctx.get(), src_pix);
                    fz_drop_pixmap(ctx.get(), msk_pix);
                    fz_drop_pixmap(ctx.get(), bg_pix);
                    fz_drop_pixmap(ctx.get(), fg_pix);
                    fz_drop_page(ctx.get(), page);
                });
            }
        }

        pdf_write_options opts = pdf_default_write_options;
        opts.do_linear = 1; 
        opts.do_clean = 1; // Очистка мусорных дескрипторов объектов

        if (!run_gui) {
            std::cout << "\n" << g_localizer.get("Saving web-optimized file...", "Сохранение веб-оптимизированного файла...") << std::endl;
        }
        
        pdf_save_document(ctx.get(), pdf, output_file.c_str(), &opts);
        pdf_drop_document(ctx.get(), pdf);

        if (!run_gui) {
            std::cout << g_localizer.get("Done! Compressed file saved as: ", "Готово! Сжатый файл сохранен как: ") << output_file << std::endl;
        } else {
#ifdef _WIN32
            DestroyWindow(g_hwndMainDialog);
#endif
        }

    } catch (const std::exception& ex) {
        std::cerr << g_localizer.get("Runtime Exception: ", "Критическое исключение: ") << ex.what() << std::endl;
        return 1;
    }

#ifdef _WIN32
    CoUninitialize();
#endif
    return 0;
}
