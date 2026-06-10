#!/bin/bash
set -o pipefail

# ==============================================================================
# НАСТРОЙКИ
# ==============================================================================
BASE_DIR="$(pwd)/codycross_mod_env"
TOOLS_DIR="$BASE_DIR/tools"
BACKUP_FILE="$BASE_DIR/codycross_progress_backup.tar.gz"
PACKAGE_NAME="com.fanatee.cody"

REMOTE_WORK_DIR="/sdcard/Download"
REMOTE_ARCHIVE_NAME="codycross_backup_temp.tar.gz"
REMOTE_ARCHIVE_PATH="$REMOTE_WORK_DIR/$REMOTE_ARCHIVE_NAME"
SOURCE_DATA_DIR="/sdcard/Android/data/$PACKAGE_NAME"

log_info() { echo -e "\e[34m[*]\e[0m $1"; }
log_success() { echo -e "\e[32m[+]\e[0m $1"; }
log_warn() { echo -e "\e[33m[!]\e[0m $1"; }
log_error() { echo -e "\e[31m[-]\e[0m $1"; exit 1; }

check_dependencies() {
    local missing_deps=()
    if ! command -v curl &> /dev/null; then missing_deps+=("curl"); fi
    if ! command -v unzip &> /dev/null; then missing_deps+=("unzip"); fi

    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Не найдены пакеты: ${missing_deps[*]}"
    fi
    log_success "Зависимости (curl, unzip) найдены."
}

setup_adb() {
    mkdir -p "$TOOLS_DIR"
    export PATH="$TOOLS_DIR/platform-tools:$TOOLS_DIR:$PATH"
    if command -v adb &> /dev/null; then
        log_info "ADB найден: $(which adb)"
        return 0
    fi
    # (Блок скачивания ADB оставлен для полноты, но у вас он уже есть)
    log_info "Скачивание Android Platform Tools (пропуск, так как ADB уже найден выше, но структура сохранена)"
}

# ==============================================================================
# ГЛАВНАЯ ЧАСТЬ
# ==============================================================================

check_dependencies
setup_adb

log_info "Поиск подключенного Android-устройства..."
adb wait-for-device

if adb devices | grep -q "unauthorized"; then
    log_error "Устройство не авторизовано! Подтвердите отладку на экране телефона."
fi
log_success "Устройство готово."

echo "=========================================="
echo "    CodyCross ADB Manager (SDCard Mode)"
echo "=========================================="
echo "1) Создать бэкап (в /sdcard)"
echo "2) Восстановить прогресс (из /sdcard)"
echo "3) Выход"
read -p "Выберите действие (1-3): " action_choice

case $action_choice in
    1)
        # --- НАДЕЖНАЯ ПРОВЕРКА TAR ---
        log_info "Проверка наличия 'tar' на устройстве (строгая проверка кода возврата)..."
        
        # Выполняем команду tar на телефоне. 
        # Toybox вернет код 0 (успех), даже если выведет справку.
        # Мы сохраняем код выхода в переменную.
        adb shell "tar" > /dev/null 2>&1
        local tar_check_code=$?

        if [ $tar_check_code -ne 0 ]; then
            log_error "На телефоне НЕТ утилиты 'tar'. Код ошибки: $tar_check_code.
Примечание: На современных Android используется Toybox. Если вы видите эту ошибку, 
значит ADB не может выполнить команду на телефоне."
        fi
        log_success "Утилита 'tar' подтверждена (код возврата 0). Это может быть Toybox."
        # ---------------------------------

        log_info "Подготовка директории на телефоне: $REMOTE_WORK_DIR"
        adb shell "mkdir -p $REMOTE_WORK_DIR"
        
        # Тест записи (игнорируем вывод, смотрим только на успех)
        adb shell "touch $REMOTE_WORK_DIR/test_write_check.tmp" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
             log_warn "Предупреждение: Не удалось создать тестовый файл. 
Возможно, ограничения Android 11+ на запись в SD Card через ADB."
        else
            adb shell "rm -f $REMOTE_WORK_DIR/test_write_check.tmp" > /dev/null 2>&1
        fi

        log_info "Создание архива на телефоне..."
        log_info "Источник: $SOURCE_DATA_DIR"
        log_info "Назначение: $REMOTE_ARCHIVE_PATH"
        
        # ВАЖНО: Используем одинарные кавычки внутри shell команды для путей
        adb shell "cd /sdcard/Android/data && tar \
            --exclude='cache' \
            --exclude='files/UnityCache' \
            --exclude='files/vast_rtb_cache' \
            --exclude='no_backup/vungle_cache' \
            -czf '$REMOTE_ARCHIVE_PATH' '$PACKAGE_NAME'" 2>&1
        
        local tar_exit_code=$?
        if [ $tar_exit_code -ne 0 ]; then
            log_error "Ошибка при создании архива! Код: $tar_exit_code
Возможные причины: 
1. Нет прав на чтение $SOURCE_DATA_DIR (Android 11+).
2. Игра запущена.
3. Недостаточно места."
        fi

        # Проверка существования файла
        adb shell "test -f '$REMOTE_ARCHIVE_PATH'" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            log_error "Файл архива на телефоне не создан! Проверьте логи выше."
        fi
        
        # Проверка размера
        local remote_size=$(adb shell "ls -l '$REMOTE_ARCHIVE_PATH' | awk '{print \$5}'")
        # Убираем возможные пробелы вокруг числа
        remote_size=$(echo "$remote_size" | tr -d '[:space:]')
        
        if [ -z "$remote_size" ] || [ "$remote_size" -eq 0 ]; then
            log_error "Архив создан, но он ПУСТОЙ (0 байт)."
        fi

        log_info "Скачивание бэкапа с телефона..."
        mkdir -p "$BASE_DIR"
        adb pull "$REMOTE_ARCHIVE_PATH" "$BACKUP_FILE"
        local pull_exit_code=$?

        if [ $pull_exit_code -ne 0 ]; then
            log_error "Ошибка при скачивании файла! Код: $pull_exit_code"
        fi

        log_info "Очистка временных файлов на телефоне..."
        adb shell "rm -f '$REMOTE_ARCHIVE_PATH'" > /dev/null 2>&1
        
        if [ ! -f "$BACKUP_FILE" ] || [ ! -s "$BACKUP_FILE" ]; then
            log_error "Локальный файл бэкапа отсутствует или пуст!"
        fi

        log_success "Бэкап успешно создан!"
        log_success "Путь на ПК: $BACKUP_FILE"
        ;;

    2)
        if [ ! -f "$BACKUP_FILE" ]; then
            log_error "Файл бэкапа '$BACKUP_FILE' не найден!"
        fi

        log_info "Закрытие игры на телефоне..."
        adb shell "am force-stop $PACKAGE_NAME" > /dev/null 2>&1
        
        log_info "Загрузка архива на телефон в: $REMOTE_ARCHIVE_PATH"
        adb push "$BACKUP_FILE" "$REMOTE_ARCHIVE_PATH"
        if [ $? -ne 0 ]; then 
            log_error "Ошибка загрузки файла на телефон." 
        fi

        log_info "Распаковка архива на телефоне..."
        adb shell "cd /sdcard/Android/data && tar -xzf '$REMOTE_ARCHIVE_PATH'" 2>&1
        if [ $? -ne 0 ]; then 
            log_error "Ошибка распаковки на телефоне." 
        fi

        log_info "Очистка временного файла на телефоне..."
        adb shell "rm -f '$REMOTE_ARCHIVE_PATH'" > /dev/null 2>&1

        log_success "Прогресс успешно восстановлен!"
        ;;
    *)
        log_info "Выход."
        exit 0
        ;;
esac
