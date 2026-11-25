// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * In-process subset of the HoloCubic services used by the ClassiCube port.
 * Every consumer is compiled into the same firmware. This table is not the
 * dynamic loader ABI and is never passed to a downloaded module.
 */
enum {
    MODULE_OK = 0,
    MODULE_ERR_FAILED = -1,
    MODULE_ERR_INVALID_ARG = -2,
    MODULE_ERR_NO_MEMORY = -3,
    MODULE_ERR_NOT_FOUND = -4,
    MODULE_ERR_UNSUPPORTED = -5,
    MODULE_ERR_BUSY = -6,
    MODULE_ERR_IO = -7,
    MODULE_ERR_BAD_STATE = -8,
};

enum {
    MODULE_HEAP_DEFAULT = 0,
    MODULE_HEAP_INTERNAL = 1u << 0,
    MODULE_HEAP_PSRAM = 1u << 1,
    MODULE_HEAP_DMA = 1u << 2,
    MODULE_HEAP_8BIT = 1u << 4,
    MODULE_HEAP_32BIT = 1u << 5,
};

enum {
    MODULE_FILE_READ = 1u << 0,
    MODULE_FILE_WRITE = 1u << 1,
    MODULE_FILE_APPEND = 1u << 2,
    MODULE_FILE_CREATE = 1u << 3,
    MODULE_FILE_TRUNC = 1u << 4,
};

enum { MODULE_SEEK_SET = 0, MODULE_SEEK_CUR, MODULE_SEEK_END };
enum { MODULE_PIXEL_RGB565 = 1 };
#define MODULE_WAIT_FOREVER UINT32_MAX

typedef void *module_sync_handle_t;

typedef struct {
    uint32_t size;
    uint16_t width, height;
    uint32_t pixel_format, flags;
} module_display_desc_t;

typedef struct {
    uint32_t size;
    uint16_t width, height;
    uint32_t pixel_formats;
    uint16_t max_dma_rows, reserved;
} module_display_caps_t;

typedef struct {
    uint32_t size, sample_rate;
    uint16_t bits_per_sample, channels;
    uint32_t flags;
} module_audio_desc_t;

typedef struct {
    struct {
        int32_t (*write)(const void *data, size_t len);
        int32_t (*println)(const char *text);
    } serial;
    struct {
        int32_t (*mounted)(void);
        int32_t (*exists)(const char *path);
        int32_t (*mkdir)(const char *path);
        int32_t (*open)(const char *path, uint32_t mode, void **out_file);
    } sd;
    struct {
        int32_t (*close)(void *file);
        int32_t (*read)(void *file, void *buf, size_t len, size_t *out_read);
        int32_t (*write)(void *file, const void *buf, size_t len, size_t *out_written);
        int32_t (*seek)(void *file, int64_t offset, uint32_t mode);
        int32_t (*position)(void *file, uint64_t *out_pos);
        int32_t (*size_bytes)(void *file, uint64_t *out_size);
    } file;
    struct {
        int32_t (*open)(const char *path, void **out_dir);
        int32_t (*open_next)(void *dir, void **out_entry);
        const char *(*name)(void *entry);
        int32_t (*is_dir)(void *entry, int32_t *out_is_dir);
        int32_t (*close)(void *handle);
    } dir;
    struct {
        int32_t (*get_caps)(module_display_caps_t *out_caps);
        int32_t (*acquire)(const char *owner, const module_display_desc_t *desc, void **out_surface);
        int32_t (*release)(void *surface);
        int32_t (*startWrite)(void *surface);
        int32_t (*pushImageDMA)(void *surface, int16_t x, int16_t y,
                               uint16_t w, uint16_t h, const uint16_t *pixels);
        int32_t (*endWrite)(void *surface);
        int32_t (*fillScreen)(void *surface, uint16_t color);
        int32_t (*setAddrWindow)(void *surface, int32_t x, int32_t y, int32_t w, int32_t h);
        int32_t (*pushPixelsDMA)(void *surface, const uint16_t *pixels, size_t len);
    } display;
    struct {
        int32_t (*begin)(const module_audio_desc_t *desc, void **out_stream);
        int32_t (*write)(void *stream, const void *samples, size_t bytes, size_t *out_written);
        int32_t (*available)(void *stream, size_t *out_bytes);
        int32_t (*end)(void *stream);
    } audio;
    struct {
        uint32_t (*millis)(void);
        uint64_t (*micros)(void);
        void (*delay)(uint32_t ms);
    } time;
    struct {
        void *(*malloc)(size_t size, uint32_t caps);
        void (*free)(void *ptr);
        size_t (*free_size)(uint32_t caps);
        size_t (*largest_free_block)(uint32_t caps);
    } heap;
    struct {
        int32_t (*create)(const char *name, void (*entry)(void *), void *arg,
                          uint32_t stack_bytes, uint32_t priority, int32_t core,
                          void **out_task);
        void (*remove)(void *task);
        void (*delay)(uint32_t ms);
        int32_t (*create_ex)(const char *name, void (*entry)(void *), void *arg,
                             uint32_t stack_bytes, uint32_t priority, int32_t core,
                             uint32_t heap_caps, void **out_task);
    } task;
    struct {
        int32_t (*create_counting)(uint32_t max_count, uint32_t initial_count,
                                   module_sync_handle_t *out_handle);
        int32_t (*create_mutex)(module_sync_handle_t *out_handle);
        int32_t (*take)(module_sync_handle_t handle, uint32_t timeout_ms);
        int32_t (*give)(module_sync_handle_t handle);
        void (*destroy)(module_sync_handle_t handle);
    } sync;
} module_host_api_v2;
