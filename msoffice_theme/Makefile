OUT_DIR = build_output
BUILD_DIR = $(OUT_DIR)/tmp
EXTENSION_NAME = msoffice_theme_2022.oxt
APTOS_URL = https://download.microsoft.com/download/8/6/0/860a94fa-7feb-44ef-ac79-c072d9113d69/Microsoft%20Aptos%20Fonts.zip

ifeq ($(OS),Windows_NT)
    RM = rm -rf
    MKDIR = mkdir -p
    CURL = curl -L
    UNZIP = unzip
    DOS2UNIX = dos2unix
    SYS_FONTS_DIR = %SYSTEMROOT%/Fonts
    FONT_REG_SCRIPT = register_fonts.ps1
    MINIFY  = $(OUT_DIR)/minify.exe
    OXIPNG  = $(OUT_DIR)/oxipng.exe
    ECT     = $(OUT_DIR)/ect.exe

    LO_PATH := $(shell reg.exe query "HKLM\SOFTWARE\LibreOffice\UNO\InstallPath" /reg:64 2>/dev/null | grep -i "Default" | awk '{print $$NF}' | sed 's|\\|/|g' | sed 's|/program||' | sed 's|/program/||' | tr -d '\r'; \
                 if [ -z "$$LO_PATH" ]; then \
                     reg.exe query "HKLM\SOFTWARE\Wow6432Node\LibreOffice\UNO\InstallPath" /reg:32 2>/dev/null | grep -i "Default" | awk '{print $$NF}' | sed 's|\\|/|g' | sed 's|/program||' | sed 's|/program/||' | tr -d '\r'; \
                 fi)
    ifeq ($(LO_PATH),)
        LO_PATH = %SYSTEMROOT%/..
    endif

    LO_ICONS_LIGHT = $(LO_PATH)/share/config/images_colibre.zip
    LO_ICONS_DARK = $(LO_PATH)/share/config/images_colibre_dark.zip
else
    RM = rm -rf
    MKDIR = mkdir -p
    CURL = curl -L
    UNZIP = unzip
    DOS2UNIX = dos2unix
    SYS_FONTS_DIR = /usr/local/share/fonts/truetype/aptos
    MINIFY  = minify
    OXIPNG  = oxipng
    ECT     = ect

    LO_ICONS_LIGHT := $(shell for p in /usr/share/libreoffice/share/config/images_colibre.zip \
                             /usr/lib/libreoffice/share/config/images_colibre.zip \
                             /var/lib/flatpak/app/org.libreoffice.LibreOffice/current/active/files/lib/libreoffice/share/config/images_colibre.zip; do \
                 if [ -f "$$p" ]; then echo "$$p"; break; fi; done)
    LO_ICONS_DARK := $(shell for p in /usr/share/libreoffice/share/config/images_colibre_dark.zip \
                             /usr/lib/libreoffice/share/config/images_colibre_dark.zip \
                             /var/lib/flatpak/app/org.libreoffice.LibreOffice/current/active/files/lib/libreoffice/share/config/images_colibre_dark.zip; do \
                 if [ -f "$$p" ]; then echo "$$p"; break; fi; done)
endif

# Определение URL для утилит (вынесено за пределы рецептов)
ifeq ($(OS),Windows_NT)
    MINIFY_URL := $(shell $(CURL) -s "https://api.github.com/repos/tdewolff/minify/releases/latest" | grep '"browser_download_url".*windows.*x86_64.*zip' | cut -d '"' -f 4 | head -1)
    OXIPNG_URL := $(shell $(CURL) -s "https://api.github.com/repos/shssoichiro/oxipng/releases/latest" | grep '"browser_download_url".*windows.*x86_64.*zip' | cut -d '"' -f 4 | head -1)
    ECT_URL := $(shell $(CURL) -s "https://api.github.com/repos/fhanau/Efficient-Compression-Tool/releases/latest" | grep '"browser_download_url".*win64.*zip' | cut -d '"' -f 4 | head -1)
else
    MINIFY_URL := $(shell $(CURL) -s "https://api.github.com/repos/tdewolff/minify/releases/latest" | grep '"browser_download_url".*linux.*x86_64.*tar.gz' | cut -d '"' -f 4 | head -1)
    OXIPNG_URL := $(shell $(CURL) -s "https://api.github.com/repos/shssoichiro/oxipng/releases/latest" | grep '"browser_download_url".*linux.*x86_64.*tar.gz' | cut -d '"' -f 4 | head -1)
    ECT_URL := $(shell $(CURL) -s "https://api.github.com/repos/fhanau/Efficient-Compression-Tool/releases/latest" | grep '"browser_download_url".*linux64.*tar.gz' | cut -d '"' -f 4 | head -1)
endif

.PHONY: all clean check_deps sanitize_sources fetch_aptos extract_libreoffice pack install-fonts


all: check_deps sanitize_sources fetch_aptos extract_libreoffice pack


check_deps:
	@echo "=== Проверка зависимостей ==="
	@$(MKDIR) $(OUT_DIR)
	@MISSING=""; \
	which $(DOS2UNIX) >/dev/null 2>&1 || MISSING="$$MISSING dos2unix"; \
	which $(UNZIP) >/dev/null 2>&1 || MISSING="$$MISSING unzip"; \
	which $(CURL) >/dev/null 2>&1 || MISSING="$$MISSING curl"; \
	if [ ! -z "$$MISSING" ]; then \
		echo "Отсутствуют утилиты: $$MISSING"; \
	ifeq ($(OS),Windows_NT) \
		echo "Установите базовые утилиты вручную."; \
	else \
		echo "Требуется sudo для установки: sudo apt install dos2unix unzip curl"; exit 1; \
	endif \
	fi

	# Загрузка утилит с GitHub (если отсутствуют)
	@for util in minify oxipng ect; do \
		target=$$(if [ "$$util" = "minify" ]; then echo $(MINIFY); else echo $(OUT_DIR)/$$util; fi); \
		if [ ! -f "$$target" ]; then \
			echo "Загрузка $$util..."; \
			if [ "$$util" = "minify" ]; then LATEST_URL="$(MINIFY_URL)"; \
		elif [ "$$util" = "oxipng" ]; then LATEST_URL="$(OXIPNG_URL)"; \
		else LATEST_URL="$(ECT_URL)"; fi; \
		if [ -n "$$LATEST_URL" ]; then \
			$(CURL) -L "$$LATEST_URL" -o $(OUT_DIR)/temp_util; \
		ifeq ($(OS),Windows_NT) \
			$(UNZIP) -j -o $(OUT_DIR)/temp_util -d $(OUT_DIR); \
		else \
			tar -xzf $(OUT_DIR)/temp_util -C $(OUT_DIR) --strip-components=1; \
		fi; \
		$(RM) $(OUT_DIR)/temp_util; \
		chmod +x "$$target"; \
	else \
		echo "Ошибка загрузки $$util"; exit 1; \
	fi; \
	done

sanitize_sources:
	@echo "=== Подготовка папки сборки ==="
	$(RM) $(OUT_DIR)/tmp
	$(MKDIR) $(BUILD_DIR)
	cp -r META-INF registry Macros $(BUILD_DIR)/
	cp description.xml $(BUILD_DIR)/
	find $(BUILD_DIR) -type f \( -name "*.xml" -o -name "*.xcu" -o -name "*.xdl" -o -name "*.xba" -o -name "*.xlb" \) -exec $(DOS2UNIX) -q {} \;

fetch_aptos:
	@echo "=== Загрузка шрифтов Aptos ==="
	$(MKDIR) $(OUT_DIR)/aptos_cache
	@if [ ! -f "$(OUT_DIR)/aptos_cache/aptos.zip" ]; then \
		$(CURL) "$(APTOS_URL)" -o "$(OUT_DIR)/aptos_cache/aptos.zip"; \
	fi
	cd "$(OUT_DIR)/aptos_cache" && $(UNZIP) -q -o aptos.zip
	$(MKDIR) "$(BUILD_DIR)/fonts"
	cp "$(OUT_DIR)/aptos_cache/"*Aptos*.ttf "$(BUILD_DIR)/fonts/" 2>/dev/null || \
	cp "$(OUT_DIR)/aptos_cache/"*.ttf "$(BUILD_DIR)/fonts/"


extract_libreoffice:
	@echo "=== Распаковка и оптимизация иконок ==="
	$(RM) "$(OUT_DIR)/icons_extracted_light" "$(OUT_DIR)/icons_extracted_dark"
	$(MKDIR) "$(OUT_DIR)/icons_extracted_light" "$(OUT_DIR)/icons_extracted_dark" "$(BUILD_DIR)/icons"
	$(UNZIP) -q -o "$(LO_ICONS_LIGHT)" -d "$(OUT_DIR)/icons_extracted_light"
	$(UNZIP) -q -o "$(LO_ICONS_DARK)" -d "$(OUT_DIR)/icons_extracted_dark"
	$(OXIPNG) -o 4 --strip safe --alpha "$(OUT_DIR)/icons_extracted_light/"**/*.png
	$(OXIPNG) -o 4 --strip safe --alpha "$(OUT_DIR)/icons_extracted_dark/"**/*.png
	cd "$(OUT_DIR)/icons_extracted_light" && \
	../../$(ECT) -9 -zip ../../$(BUILD_DIR)/icons/images_ms2022.zip * >/dev/null 2>&1 || \
	zip -q -9r ../../$(BUILD_DIR)/icons/images_ms2022.zip *
	cd "$(OUT_DIR)/icons_extracted_dark" && \
	../../$(ECT) -9 -zip ../../$(BUILD_DIR)/icons/images_ms2022_dark.zip * >/dev/null 2>&1 || \
	zip -q -9r ../../$(BUILD_DIR)/icons/images_ms2022_dark.zip *

pack:
	@echo "=== Минимизация кода и упаковка ==="
	find "$(BUILD_DIR)" -type f \
	\( -name "*.xml" -o -name "*.xcu" -o -name "*.xdl" -o -name "*.xba" -o -name "*.xlb" \) \
	-exec $(MINIFY) -o {} {} \;
	@echo "Сжатие OXT‑контейнера через ECT (максимальная компрессия ZIP)..."
	cd "$(BUILD_DIR)" && \
	../$(ECT) -9 -zip ../$(EXTENSION_NAME) * >/dev/null 2>&1 || \
	zip -q -9Xrc ../$(EXTENSION_NAME) *
	@echo "=== СБОРКА УСПЕШНО ЗАВЕРШЕНА! Пакет: $(OUT_DIR)/$(EXTENSION_NAME) ==="

install-fonts: fetch_aptos
	@echo "=== Установка шрифтов Aptos на уровень операционной системы ==="
ifeq ($(OS),Windows_NT)
	@printf '$$FontsFolder = "%s"; $$RegistryKey = "HKLM:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"; Get-ChildItem "%s/fonts/*.ttf" | ForEach-Object { $$Target = Join-Path $$FontsFolder $$_.Name; if (-not (Test-Path $$Target)) { Copy-Item $$_.FullName $$Target }; $$FontName = (New-Object -ComObject Shell.Application).Namespace($$FontsFolder).ParseName($$_.Name).ExtendedProperty("Title"); if ($$_.Name -like "*Bold*") { $$FontName += " Bold" }; if ($$_.Name -like "*Italic*") { $$FontName += " Italic" }; $$FontName += " (TrueType)"; New-ItemProperty -Path $$RegistryKey -Name $$FontName -Value $$_.Name -PropertyType String -Force | Out-Null }' "$(SYS_FONTS_DIR)" "$(BUILD_DIR)" > "$(OUT_DIR)/$(FONT_REG_SCRIPT)"
	@echo "Для установки шрифтов требуются права администратора."
	@echo "Выполните в PowerShell с правами администратора:"
	@echo "  powershell -NoProfile -ExecutionPolicy Bypass -File $(OUT_DIR)/$(FONT_REG_SCRIPT)"
else
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Требуется sudo для установки в системную директорию. Запустите: sudo make install-fonts" && exit 1; \
	fi
	$(MKDIR) "$(SYS_FONTS_DIR)" && \
	cp "$(BUILD_DIR)/fonts/"*.ttf "$(SYS_FONTS_DIR)/" && \
	chmod 644 "$(SYS_FONTS_DIR)"/*.ttf && \
	fc-cache -f -v && \
	echo "Шрифты Aptos успешно добавлены в глобальный каталог Linux."
endif

clean:
	$(RM) "$(OUT_DIR)"

.PHONY: all clean check_deps sanitize_sources fetch_aptos extract_libreoffice pack install-fonts
