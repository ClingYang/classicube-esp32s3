// SPDX-License-Identifier: MIT
#include "silt_host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp_audio.h"
#include "bsp_lcd.h"
#include "bsp_pins.h"
#include "bsp_sd.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SILT_SD_MOUNT "/sdcard"
#define PATH_CAPACITY 256
#define DISPLAY_TIMEOUT_MS 1000

static bool s_sd_ready;
static bool s_audio_enabled;

void silt_host_set_sd_ready(bool ready)
{
    s_sd_ready = ready;
}

void silt_host_set_audio_enabled(bool enabled)
{
    s_audio_enabled = enabled;
}

static int32_t from_esp(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return MODULE_OK;
    case ESP_ERR_INVALID_ARG: return MODULE_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE: return MODULE_ERR_BAD_STATE;
    case ESP_ERR_NO_MEM: return MODULE_ERR_NO_MEMORY;
    case ESP_ERR_TIMEOUT: return MODULE_ERR_BUSY;
    case ESP_ERR_NOT_SUPPORTED: return MODULE_ERR_UNSUPPORTED;
    default: return MODULE_ERR_IO;
    }
}

static TickType_t ticks(uint32_t ms)
{
    if (ms == MODULE_WAIT_FOREVER) return portMAX_DELAY;
    uint64_t rounded = ((uint64_t)ms * configTICK_RATE_HZ + 999) / 1000;
    return rounded < portMAX_DELAY ? (TickType_t)rounded : (portMAX_DELAY - 1);
}

static void sleep_ms(uint32_t ms)
{
    if (ms) {
        vTaskDelay(ticks(ms));
    } else {
        taskYIELD();
    }
}

static uint64_t micros(void)
{
    return (uint64_t)esp_timer_get_time();
}

static uint32_t millis(void)
{
    return (uint32_t)(micros() / 1000);
}

static uint32_t heap_flags(uint32_t caps)
{
    uint32_t flags = 0;
    if (caps & MODULE_HEAP_INTERNAL) flags |= MALLOC_CAP_INTERNAL;
    if (caps & MODULE_HEAP_PSRAM) flags |= MALLOC_CAP_SPIRAM;
    if (caps & MODULE_HEAP_DMA) flags |= MALLOC_CAP_DMA;
    if (caps & MODULE_HEAP_8BIT) flags |= MALLOC_CAP_8BIT;
    if (caps & MODULE_HEAP_32BIT) flags |= MALLOC_CAP_32BIT;
    return flags ? flags : MALLOC_CAP_DEFAULT;
}

static void *host_malloc(size_t size, uint32_t caps)
{
    return heap_caps_malloc(size, heap_flags(caps));
}

static size_t free_size(uint32_t caps)
{
    return heap_caps_get_free_size(heap_flags(caps));
}

static size_t largest_free_block(uint32_t caps)
{
    return heap_caps_get_largest_free_block(heap_flags(caps));
}

static int32_t serial_write(const void *data, size_t len)
{
    if (!data || len > INT32_MAX) return MODULE_ERR_INVALID_ARG;
    return (int32_t)fwrite(data, 1, len, stdout);
}

static int32_t serial_line(const char *text)
{
    return text && puts(text) >= 0 ? MODULE_OK : MODULE_ERR_IO;
}

/* All engine-owned tasks use the caps-aware allocator, including IRAM-facing
 * presenter workers. Then every engine task can use the same delete callback. */
static int32_t task_create_ex(const char *name, void (*entry)(void *), void *arg,
                              uint32_t stack_bytes, uint32_t priority, int32_t core,
                              uint32_t caps, void **out_task)
{
    if (!entry || !out_task || !stack_bytes ||
        (core != -1 && (core < 0 || core >= configNUMBER_OF_CORES)) ||
        priority >= configMAX_PRIORITIES) return MODULE_ERR_INVALID_ARG;
    *out_task = NULL;
    TaskHandle_t task = NULL;
    if (caps == MODULE_HEAP_DEFAULT) caps = MODULE_HEAP_INTERNAL | MODULE_HEAP_8BIT;
    if (xTaskCreatePinnedToCoreWithCaps(entry, name, stack_bytes, arg, priority,
                                       &task, core < 0 ? tskNO_AFFINITY : core,
                                       heap_flags(caps)) != pdPASS) {
        return MODULE_ERR_NO_MEMORY;
    }
    *out_task = task;
    return MODULE_OK;
}

static int32_t task_create(const char *name, void (*entry)(void *), void *arg,
                           uint32_t stack_bytes, uint32_t priority, int32_t core,
                           void **out_task)
{
    return task_create_ex(name, entry, arg, stack_bytes, priority, core,
                          MODULE_HEAP_INTERNAL | MODULE_HEAP_8BIT, out_task);
}

static void task_remove(void *task)
{
    vTaskDeleteWithCaps((TaskHandle_t)task);
}

static int32_t sync_counting(uint32_t limit, uint32_t initial, module_sync_handle_t *out)
{
    if (!out || !limit || initial > limit) return MODULE_ERR_INVALID_ARG;
    *out = xSemaphoreCreateCounting(limit, initial);
    return *out ? MODULE_OK : MODULE_ERR_NO_MEMORY;
}

static int32_t sync_mutex(module_sync_handle_t *out)
{
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = xSemaphoreCreateMutex();
    return *out ? MODULE_OK : MODULE_ERR_NO_MEMORY;
}

static int32_t sync_take(module_sync_handle_t handle, uint32_t timeout_ms)
{
    if (!handle) return MODULE_ERR_INVALID_ARG;
    return xSemaphoreTake((SemaphoreHandle_t)handle, ticks(timeout_ms)) == pdTRUE
        ? MODULE_OK : MODULE_ERR_BUSY;
}

static int32_t sync_give(module_sync_handle_t handle)
{
    if (!handle) return MODULE_ERR_INVALID_ARG;
    return xSemaphoreGive((SemaphoreHandle_t)handle) == pdTRUE ? MODULE_OK : MODULE_ERR_BUSY;
}

static void sync_destroy(module_sync_handle_t handle)
{
    if (handle) vSemaphoreDelete((SemaphoreHandle_t)handle);
}

typedef struct {
    TaskHandle_t writer;
    bool owned, pending, window;
    int x, y, width, height, next_row;
} Display;

static Display s_display;

static bool display_owned(void *surface)
{
    return surface == &s_display && s_display.owned;
}

static bool display_writing(void *surface)
{
    return display_owned(surface) && s_display.writer == xTaskGetCurrentTaskHandle();
}

static int32_t display_caps(module_display_caps_t *out)
{
    if (!out || out->size < sizeof(*out)) return MODULE_ERR_INVALID_ARG;
    *out = (module_display_caps_t){
        .size = sizeof(*out),
        .width = BSP_LCD_H_RES,
        .height = BSP_LCD_V_RES,
        .pixel_formats = MODULE_PIXEL_RGB565,
        .max_dma_rows = BSP_LCD_BAND_LINES,
    };
    return MODULE_OK;
}

static int32_t display_acquire(const char *owner, const module_display_desc_t *desc, void **out)
{
    (void)owner;
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = NULL;
    if (!desc || desc->size < sizeof(*desc) || desc->width != BSP_LCD_H_RES ||
        desc->height != BSP_LCD_V_RES || desc->pixel_format != MODULE_PIXEL_RGB565)
        return MODULE_ERR_INVALID_ARG;
    if (s_display.owned) return MODULE_ERR_BUSY;
    if (!bsp_lcd_panel()) return MODULE_ERR_BAD_STATE;
    bsp_lcd_flush_pending(); /* splash (if any) has completed before acquisition */
    s_display = (Display){.owned = true};
    *out = &s_display;
    return MODULE_OK;
}

static int32_t display_drain(void)
{
    if (!s_display.pending) return MODULE_OK;
    if (!bsp_lcd_wait_transfer(DISPLAY_TIMEOUT_MS)) return MODULE_ERR_BUSY;
    s_display.pending = false;
    return MODULE_OK;
}

static int32_t display_release(void *surface)
{
    if (!display_owned(surface)) return MODULE_ERR_INVALID_ARG;
    if (s_display.writer) return MODULE_ERR_BUSY;
    int32_t result = display_drain();
    if (result == MODULE_OK) s_display = (Display){0};
    return result;
}

static int32_t display_start(void *surface)
{
    if (!display_owned(surface)) return MODULE_ERR_INVALID_ARG;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (s_display.writer && s_display.writer != self) return MODULE_ERR_BUSY;
    int32_t result = display_drain();
    if (result != MODULE_OK) return result;
    s_display.writer = self;
    s_display.window = false;
    return MODULE_OK;
}

static bool display_region(int x, int y, int w, int h)
{
    return x >= 0 && y >= 0 && w > 0 && h > 0 &&
           w <= BSP_LCD_H_RES && h <= BSP_LCD_V_RES &&
           x <= BSP_LCD_H_RES - w && y <= BSP_LCD_V_RES - h;
}

static int32_t display_image(void *surface, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, const uint16_t *pixels)
{
    if (!display_writing(surface)) return MODULE_ERR_BAD_STATE;
    if (!pixels || !display_region(x, y, w, h)) return MODULE_ERR_INVALID_ARG;
    int32_t result = display_drain();
    if (result != MODULE_OK) return result;

    /* The renderer alternates two internal DMA bands. One pending transaction
     * leaves the other writable while the SPI driver consumes its predecessor. */
    if (!esp_ptr_dma_capable(pixels) || ((uintptr_t)pixels & 3u)) {
        return from_esp(bsp_lcd_blit_staged(x, y, x + w, y + h, pixels));
    }
    esp_err_t err = bsp_lcd_blit_async(x, y, x + w, y + h, pixels);
    s_display.pending = err == ESP_OK;
    return from_esp(err);
}

static int32_t display_end(void *surface)
{
    if (!display_writing(surface)) return MODULE_ERR_BAD_STATE;
    int32_t result = display_drain();
    if (result == MODULE_OK) {
        s_display.writer = NULL;
        s_display.window = false;
    }
    return result;
}

static int32_t display_fill(void *surface, uint16_t color)
{
    if (!display_owned(surface)) return MODULE_ERR_INVALID_ARG;
    if (s_display.writer || s_display.pending) return MODULE_ERR_BUSY;
    bsp_lcd_fill(color);
    return MODULE_OK;
}

static int32_t display_window(void *surface, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!display_writing(surface)) return MODULE_ERR_BAD_STATE;
    if (!display_region(x, y, w, h)) return MODULE_ERR_INVALID_ARG;
    s_display.x = x;
    s_display.y = y;
    s_display.width = w;
    s_display.height = h;
    s_display.next_row = 0;
    s_display.window = true;
    return MODULE_OK;
}

static int32_t display_pixels(void *surface, const uint16_t *pixels, size_t len)
{
    if (!display_writing(surface) || !s_display.window) return MODULE_ERR_BAD_STATE;
    size_t width = (size_t)s_display.width;
    if (!len || len % width || len / width > (size_t)(s_display.height - s_display.next_row))
        return MODULE_ERR_INVALID_ARG;
    int rows = (int)(len / width);
    int32_t result = display_image(surface, s_display.x, s_display.y + s_display.next_row,
                                   s_display.width, rows, pixels);
    if (result == MODULE_OK) s_display.next_row += rows;
    return result;
}

static int32_t sd_mounted(void)
{
    return s_sd_ready || bsp_sd_ready();
}

static int32_t from_errno(void)
{
    switch (errno) {
    case ENOENT: return MODULE_ERR_NOT_FOUND;
    case ENOMEM: return MODULE_ERR_NO_MEMORY;
    case EINVAL: return MODULE_ERR_INVALID_ARG;
    case EBUSY: return MODULE_ERR_BUSY;
    default: return MODULE_ERR_IO;
    }
}

static bool is_mount_prefix(const char *path, const char *prefix, size_t n)
{
    return !strncmp(path, prefix, n) && (path[n] == '/' || path[n] == '\0');
}

static int32_t sd_path(char dest[PATH_CAPACITY], const char *path)
{
    if (!path) return MODULE_ERR_INVALID_ARG;
    if (!sd_mounted()) return MODULE_ERR_BAD_STATE;
    const char *suffix;
    if (is_mount_prefix(path, SILT_SD_MOUNT, sizeof(SILT_SD_MOUNT) - 1)) {
        suffix = path + sizeof(SILT_SD_MOUNT) - 1;
    } else if (is_mount_prefix(path, "/sd", 3)) {
        suffix = path + 3;
    } else {
        return MODULE_ERR_INVALID_ARG;
    }

    for (const char *p = suffix; *p;) {
        if (*p++ == '/' && p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0')) return MODULE_ERR_INVALID_ARG;
    }
    if (strchr(suffix, '\\')) return MODULE_ERR_INVALID_ARG;
    int len = snprintf(dest, PATH_CAPACITY, "%s%s", SILT_SD_MOUNT, suffix);
    return len >= 0 && len < PATH_CAPACITY ? MODULE_OK : MODULE_ERR_INVALID_ARG;
}

static int32_t sd_exists(const char *path)
{
    char full[PATH_CAPACITY];
    struct stat state;
    return sd_path(full, path) == MODULE_OK && stat(full, &state) == 0;
}

static int32_t sd_mkdir(const char *path)
{
    char full[PATH_CAPACITY];
    int32_t result = sd_path(full, path);
    if (result != MODULE_OK) return result;
    if (mkdir(full, 0775) == 0) return MODULE_OK;
    if (errno != EEXIST) return from_errno();
    struct stat state;
    return stat(full, &state) == 0 && S_ISDIR(state.st_mode) ? MODULE_OK : MODULE_ERR_IO;
}

static int file_descriptor(void *handle)
{
    if (!handle || (uintptr_t)handle > INT_MAX) return -1;
    return (int)(uintptr_t)handle - 1;
}

static int32_t sd_open(const char *path, uint32_t mode, void **out)
{
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = NULL;
    char full[PATH_CAPACITY];
    int32_t result = sd_path(full, path);
    if (result != MODULE_OK) return result;
    int flags = O_RDONLY;
    if (mode & MODULE_FILE_WRITE) flags = mode & MODULE_FILE_READ ? O_RDWR : O_WRONLY;
    if (mode & MODULE_FILE_APPEND) flags |= O_APPEND;
    if (mode & MODULE_FILE_CREATE) flags |= O_CREAT;
    if (mode & MODULE_FILE_TRUNC) flags |= O_TRUNC;
    int fd = open(full, flags, 0664);
    if (fd < 0) return from_errno();
    *out = (void *)(uintptr_t)(fd + 1u);
    return MODULE_OK;
}

static int32_t file_close(void *handle)
{
    int fd = file_descriptor(handle);
    if (fd < 0) return MODULE_ERR_INVALID_ARG;
    return close(fd) == 0 ? MODULE_OK : from_errno();
}

static int32_t file_read(void *handle, void *buf, size_t len, size_t *out)
{
    if (!out || (!buf && len)) return MODULE_ERR_INVALID_ARG;
    *out = 0;
    int fd = file_descriptor(handle);
    if (fd < 0) return MODULE_ERR_INVALID_ARG;
    ssize_t n = read(fd, buf, len);
    if (n < 0) return from_errno();
    *out = (size_t)n;
    return MODULE_OK;
}

static int32_t file_write(void *handle, const void *buf, size_t len, size_t *out)
{
    if (!out || (!buf && len)) return MODULE_ERR_INVALID_ARG;
    *out = 0;
    int fd = file_descriptor(handle);
    if (fd < 0) return MODULE_ERR_INVALID_ARG;
    while (*out < len) {
        ssize_t n = write(fd, (const uint8_t *)buf + *out, len - *out);
        if (n < 0) return from_errno();
        if (n == 0) return MODULE_ERR_IO;
        *out += (size_t)n;
    }
    return MODULE_OK;
}

static int32_t file_seek(void *handle, int64_t offset, uint32_t mode)
{
    int fd = file_descriptor(handle);
    if (fd < 0 || mode > MODULE_SEEK_END || (int64_t)(off_t)offset != offset)
        return MODULE_ERR_INVALID_ARG;
    int method = mode == MODULE_SEEK_SET ? SEEK_SET :
                 mode == MODULE_SEEK_CUR ? SEEK_CUR : SEEK_END;
    return lseek(fd, (off_t)offset, method) >= 0 ? MODULE_OK : from_errno();
}

static int32_t file_position(void *handle, uint64_t *out)
{
    int fd = file_descriptor(handle);
    if (fd < 0 || !out) return MODULE_ERR_INVALID_ARG;
    off_t position = lseek(fd, 0, SEEK_CUR);
    if (position < 0) return from_errno();
    *out = (uint64_t)position;
    return MODULE_OK;
}

static int32_t file_size(void *handle, uint64_t *out)
{
    int fd = file_descriptor(handle);
    if (fd < 0 || !out) return MODULE_ERR_INVALID_ARG;
    struct stat state;
    if (fstat(fd, &state) != 0) return from_errno();
    *out = (uint64_t)state.st_size;
    return MODULE_OK;
}

typedef enum { DIRECTORY_OPEN = 1, DIRECTORY_ENTRY } DirectoryKind;

typedef struct {
    DirectoryKind kind;
    char name[PATH_CAPACITY];
    bool directory;
} DirectoryEntry;

typedef struct {
    DirectoryKind kind;
    DIR *stream;
    char path[PATH_CAPACITY];
    DirectoryEntry entry;
} Directory;

static int32_t dir_open(const char *path, void **out)
{
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = NULL;
    char full[PATH_CAPACITY];
    int32_t result = sd_path(full, path);
    if (result != MODULE_OK) return result;
    DIR *stream = opendir(full);
    if (!stream) return from_errno();
    Directory *dir = malloc(sizeof(*dir));
    if (!dir) {
        closedir(stream);
        return MODULE_ERR_NO_MEMORY;
    }
    dir->kind = DIRECTORY_OPEN;
    dir->stream = stream;
    memcpy(dir->path, full, strlen(full) + 1);
    dir->entry.kind = DIRECTORY_ENTRY;
    *out = dir;
    return MODULE_OK;
}

static int32_t dir_next(void *handle, void **out)
{
    Directory *d = handle;
    if (!d || d->kind != DIRECTORY_OPEN || !out) return MODULE_ERR_INVALID_ARG;
    *out = NULL;

    for (;;) {
        errno = 0;
        const struct dirent *entry = readdir(d->stream);
        if (!entry) return errno ? from_errno() : MODULE_ERR_NOT_FOUND;
        size_t n = strlen(entry->d_name);
        if (!n || n >= sizeof(d->entry.name) ||
            !strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char full[PATH_CAPACITY];
        int len = snprintf(full, sizeof(full), "%s/%s", d->path, entry->d_name);
        if (len < 0 || len >= sizeof(full)) continue;
        struct stat state;
        if (stat(full, &state) != 0) continue;
        memcpy(d->entry.name, entry->d_name, n + 1);
        d->entry.directory = S_ISDIR(state.st_mode);
        *out = &d->entry; /* borrowed until the next call on this directory */
        return MODULE_OK;
    }
}

static const char *dir_name(void *handle)
{
    const DirectoryEntry *entry = handle;
    return entry && entry->kind == DIRECTORY_ENTRY ? entry->name : NULL;
}

static int32_t dir_is_directory(void *handle, int32_t *out)
{
    const DirectoryEntry *entry = handle;
    if (!entry || entry->kind != DIRECTORY_ENTRY || !out) return MODULE_ERR_INVALID_ARG;
    *out = entry->directory;
    return MODULE_OK;
}

static int32_t dir_close(void *handle)
{
    if (!handle) return MODULE_ERR_INVALID_ARG;
    DirectoryKind kind = *(DirectoryKind *)handle;
    if (kind == DIRECTORY_ENTRY) return MODULE_OK;
    if (kind != DIRECTORY_OPEN) return MODULE_ERR_INVALID_ARG;
    Directory *d = handle;
    int32_t result = closedir(d->stream) == 0 ? MODULE_OK : from_errno();
    free(d);
    return result;
}

typedef struct {
    unsigned rate, phase;
    bool open, speaker_on;
    int16_t previous;
} AudioOutput;

static AudioOutput s_audio;

static int32_t audio_begin(const module_audio_desc_t *desc, void **out)
{
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = NULL;
    if (!s_audio_enabled) return MODULE_ERR_UNSUPPORTED;
    if (!desc || desc->size < sizeof(*desc) || desc->channels != 1 ||
        desc->bits_per_sample != 16 || desc->sample_rate < BSP_AUDIO_SAMPLE_RATE ||
        desc->sample_rate > 48000) return MODULE_ERR_INVALID_ARG;
    if (s_audio.open) return MODULE_ERR_BUSY;
    s_audio = (AudioOutput){.rate = desc->sample_rate, .open = true};
    *out = &s_audio;
    return MODULE_OK;
}

static int32_t play_audio_frames(const int16_t *stereo, size_t frames)
{
    if (!s_audio.speaker_on) {
        bool audible = false;
        for (size_t i = 0; i < frames; i++) audible |= stereo[2 * i] != 0;
        if (!audible) return MODULE_OK;
        int32_t result = from_esp(bsp_audio_speaker(true));
        if (result != MODULE_OK) return result;
        s_audio.speaker_on = true;
    }
    return from_esp(bsp_audio_write(stereo, frames));
}

static int32_t audio_write(void *handle, const void *samples, size_t len, size_t *out)
{
    if (!out) return MODULE_ERR_INVALID_ARG;
    *out = 0;
    if (handle != &s_audio || !s_audio.open) return MODULE_ERR_BAD_STATE;
    if ((!samples && len) || ((uintptr_t)samples & 1u) || (len & 1u)) return MODULE_ERR_INVALID_ARG;

    const int16_t *mono = samples;
    size_t frames = len / sizeof(*mono), used = 0;
    int16_t stereo[BSP_AUDIO_DMA_FRAMES * 2];
    for (size_t i = 0; i < frames; i++) {
        int sample = mono[i];
        unsigned advance = s_audio.phase + BSP_AUDIO_SAMPLE_RATE;
        if (advance >= s_audio.rate) {
            int fraction = (int)(s_audio.rate - s_audio.phase);
            int interpolated = s_audio.previous +
                (sample - s_audio.previous) * fraction / BSP_AUDIO_SAMPLE_RATE;
            stereo[2 * used] = stereo[2 * used + 1] = (int16_t)interpolated;
            used++;
            advance -= s_audio.rate;
        }
        s_audio.phase = advance;
        s_audio.previous = (int16_t)sample;
        if (used == BSP_AUDIO_DMA_FRAMES) {
            int32_t result = play_audio_frames(stereo, used);
            if (result != MODULE_OK) return result;
            used = 0;
        }
    }
    if (used) {
        int32_t result = play_audio_frames(stereo, used);
        if (result != MODULE_OK) return result;
    }
    *out = len;
    return MODULE_OK;
}

static int32_t audio_available(void *handle, size_t *out)
{
    if (handle != &s_audio || !s_audio.open || !out) return MODULE_ERR_INVALID_ARG;
    /* A block fits. The I2S driver paces the worker while its DMA queue is full. */
    *out = 512;
    return MODULE_OK;
}

static int32_t audio_end(void *handle)
{
    if (handle != &s_audio || !s_audio.open) return MODULE_ERR_INVALID_ARG;
    int32_t result = MODULE_OK;
    if (s_audio.speaker_on) {
        sleep_ms(40);
        result = from_esp(bsp_audio_speaker(false));
    }
    s_audio = (AudioOutput){0};
    return result;
}

const module_host_api_v2 *silt_host_api(void)
{
    static const module_host_api_v2 host = {
        .serial = { .write = serial_write, .println = serial_line },
        .sd = {
            .mounted = sd_mounted, .exists = sd_exists, .mkdir = sd_mkdir, .open = sd_open,
        },
        .file = {
            .close = file_close, .read = file_read, .write = file_write, .seek = file_seek,
            .position = file_position, .size_bytes = file_size,
        },
        .dir = {
            .open = dir_open, .open_next = dir_next, .name = dir_name,
            .is_dir = dir_is_directory, .close = dir_close,
        },
        .display = {
            .get_caps = display_caps, .acquire = display_acquire, .release = display_release,
            .startWrite = display_start, .pushImageDMA = display_image, .endWrite = display_end,
            .fillScreen = display_fill, .setAddrWindow = display_window, .pushPixelsDMA = display_pixels,
        },
        .audio = {
            .begin = audio_begin, .write = audio_write,
            .available = audio_available, .end = audio_end,
        },
        .time = { .millis = millis, .micros = micros, .delay = sleep_ms },
        .heap = {
            .malloc = host_malloc, .free = heap_caps_free, .free_size = free_size,
            .largest_free_block = largest_free_block,
        },
        .task = {
            .create = task_create, .create_ex = task_create_ex,
            .remove = task_remove, .delay = sleep_ms,
        },
        .sync = {
            .create_counting = sync_counting, .create_mutex = sync_mutex,
            .take = sync_take, .give = sync_give, .destroy = sync_destroy,
        },
    };
    return &host;
}
