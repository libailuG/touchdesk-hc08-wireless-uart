#include "resource_store.h"
#include "resource_manifest.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include <string.h>

static const uint8_t *base;
static esp_partition_mmap_handle_t mapping;
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool valid_bundle(const uint8_t *data)
{
    return !memcmp(data,"TDRES001",8) && read_u32(data+8)==1 &&
        read_u32(data+12)==RESOURCE_BUNDLE_SIZE && read_u32(data+16)==RESOURCE_BUNDLE_COUNT &&
        read_u32(data+20)==RESOURCE_BUNDLE_CRC &&
        esp_rom_crc32_le(0,data+64,RESOURCE_BUNDLE_SIZE-64)==RESOURCE_BUNDLE_CRC;
}
const uint8_t *resource_store_data(size_t offset,size_t length)
{
    if(!base || offset<64 || offset>RESOURCE_BUNDLE_SIZE || length>RESOURCE_BUNDLE_SIZE-offset) return NULL;
    return base+offset;
}
esp_err_t resource_store_init(void)
{
    if(base) return ESP_OK;
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"assets");
    if(!part || part->size<RESOURCE_BUNDLE_SIZE) return ESP_ERR_NOT_FOUND;
    const void *mapped=NULL;
    esp_err_t err=esp_partition_mmap(part,0,RESOURCE_BUNDLE_SIZE,ESP_PARTITION_MMAP_DATA,&mapped,&mapping);
    if(err!=ESP_OK) return err;
    if(!valid_bundle(mapped)) {
        esp_partition_munmap(mapping);
        ESP_LOGE("resources","Missing, corrupt or incompatible resource bundle");
        return ESP_ERR_INVALID_CRC;
    }
    base=mapped;
    if(!weather_art_bind_resources() || !font_ui_bind_resources() ||
       !weather_fonts_bind_resources() || !weather_number_fonts_bind_resources()) {
        base=NULL; esp_partition_munmap(mapping); return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI("resources","Mapped %u bytes in Flash; blocks=%u CRC32=%08x; no bulk RAM copy",
        RESOURCE_BUNDLE_SIZE,RESOURCE_BUNDLE_COUNT,RESOURCE_BUNDLE_CRC);
    return ESP_OK;
}
