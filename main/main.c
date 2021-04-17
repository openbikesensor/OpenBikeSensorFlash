/*
 * Copyright (C) 2019-2021 OpenBikeSensor Contributors
 * Contact: https://openbikesensor.org
 *
 * This file is part of the OpenBikeSensor firmware.
 *
 * The OpenBikeSensor firmware is free software: you can
 * redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * OpenBikeSensor firmware is distributed in the hope that
 * it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the OpenBikeSensor firmware.  If not,
 * see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "driver/sdspi_host.h"
#include "esp_partition.h"


// Version only change the "vN.M" part if needed.
const char *VERSION = "v0.1" BUILD_NUMBER;


// SD Card
// Selected the same values as for the OBS!
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

#define SPI_DMA_CHAN    1

#define MOUNT_POINT "/sd"

const uint8_t partition_table[] = {
// nvs,      data, nvs,     0x009000, 0x005000,
        0xAA, 0x50, 0x01, 0x02, 0x00, 0x90, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x6E, 0x76, 0x73, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// otadata,  data, ota,     0x00e000, 0x002000,
        0xAA, 0x50, 0x01, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x6F, 0x74, 0x61, 0x64,
        0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// app,      app,  ota_0,   0x010000, 0x380000,
        0xAA, 0x50, 0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x61, 0x70, 0x70, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// sdflash,  app,  ota_1,   0x390000, 0x040000,
        0xAA, 0x50, 0x00, 0x11, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x04, 0x00, 0x73, 0x64, 0x66, 0x6C,
        0x61, 0x73, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// spiffs,   data, spiffs,  0x3D0000, 0x030000,
        0xAA, 0x50, 0x01, 0x82, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x03, 0x00, 0x73, 0x70, 0x69, 0x66,
        0x66, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
// END
        0xEB, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFE, 0xCD, 0xE8, 0x51, 0x9F, 0xAE, 0x28, 0xAB, 0xE8, 0x12, 0x4E, 0x8C, 0xCC, 0xDE, 0x1B, 0x82
};

static const char *TAG = "esp32-flash-sd";

static void logAppVersion(const esp_partition_t *partition) {
    esp_app_desc_t app_desc;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(partition, &app_desc));
    char hash_print[17];
    hash_print[16] = 0;
    for (int i = 0; i < 8; ++i) {
        sprintf(&hash_print[i * 2], "%02x", app_desc.app_elf_sha256[i]);
    }
    ESP_LOGI(TAG, "App '%s', Version: '%s', IDF-Version: '%s', sha-256: %s..., date: '%s', time: '%s'",
             app_desc.project_name, app_desc.version, app_desc.idf_ver, hash_print, app_desc.date, app_desc.time);
}

static void ensureOta1() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running->label);

    if (running->type == ESP_PARTITION_TYPE_APP && running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        const esp_partition_t *ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                               NULL);
        ESP_ERROR_CHECK(esp_partition_erase_range(ota1, 0, ota1->size));
        ESP_LOGI(TAG, "Copying ota%d (%s) -> ota0 (%s)", running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0,
                 running->label, ota1->label);
        ESP_LOGI(TAG, "Target partition: size 0x%0x", ota1->size);
        // TODO: Check size; we get error if size differs!
        const uint16_t s = 8192;
        void *buffer = malloc(s);
        for (int i = 0; i < ota1->size; i += s) {
            ESP_ERROR_CHECK(esp_partition_read(running, i, buffer, s));
            ESP_ERROR_CHECK(esp_partition_write(ota1, i, buffer, s));
        }
        free(buffer);
        ESP_ERROR_CHECK(esp_ota_set_boot_partition(ota1));
        ESP_LOGE(TAG, "Partition swap done will restart.");
        esp_restart();
    }
}

void write_partition_table() {
    ESP_LOGI(TAG, "Will replace partition table.");
    ESP_LOGD(TAG, "Will erase partition table.");
    ESP_ERROR_CHECK(spi_flash_erase_range(CONFIG_PARTITION_TABLE_OFFSET, 0x2000));
    ESP_LOGD(TAG, "Will write new partition table.");
    ESP_ERROR_CHECK(spi_flash_write(CONFIG_PARTITION_TABLE_OFFSET, partition_table, sizeof(partition_table)));
    ESP_LOGI(TAG, "New partition table created, will reboot.");
    esp_restart();
}

const esp_partition_t *ensureNewPartitionTable() {
    const esp_partition_t *app = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0,
            "app"); // already there?
    if (app) {
        ESP_LOGI(TAG, "Partition already there, size is 0x%0x bytes.", app->size);
    } else {
        write_partition_table();
    }
    return app;
}

const sdmmc_card_t *mountSdCard() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 1,
            .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    ESP_LOGI(TAG, "Initializing SD card");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .sclk_io_num = PIN_NUM_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN));
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card));
    return card;
}

void flashFromSd(const esp_partition_t *appPartition) {
    ESP_LOGI(TAG, "Will flash from SD card.");
    FILE *f = fopen(MOUNT_POINT"/sdflash/app.bin", "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to find 'sdflash/app.bin' on sd card.");
        return;
    }
    esp_ota_handle_t out_handle;
    esp_ota_begin(appPartition, OTA_SIZE_UNKNOWN/*?*/, &out_handle);

    const uint16_t size = 8192;
    void *buffer = malloc(size);
    uint16_t read;
    while ((read = fread(buffer, 1, size, f)) > 0) {
        ESP_ERROR_CHECK(esp_ota_write(out_handle, buffer, read));
    }
    free(buffer);
    if (!feof(f)) {
        ESP_LOGE(TAG, "Failed to read 'sdflash/app.bin'.");
        // FIXME!!
        return;
    }
    fclose(f);

    ESP_ERROR_CHECK(esp_ota_end(out_handle));
    ESP_LOGI(TAG, "Flash from SD card done.");
}

void app_main() {

    // todo check reset reason!
    esp_reset_reason_t resetReason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason 0x%x.", resetReason); // should be ESP_RST_POWERON or ESP_RST_SW

    // todo ensureSdContent() / check if already flashed at that version?
    // check if there is a version at all

    esp_ota_mark_app_valid_cancel_rollback();

    ensureOta1();
    const esp_partition_t *appPartition = ensureNewPartitionTable();
    sdmmc_card_t *card = mountSdCard();

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running->label);
    logAppVersion(running);

    flashFromSd(appPartition);
    logAppVersion(appPartition);
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(appPartition));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card));
    esp_restart();
}