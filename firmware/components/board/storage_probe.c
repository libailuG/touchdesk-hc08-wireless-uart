/* CRC-checked TF transport: read-protected by default; explicit test-file write gate. */
#include "board.h"
#include "ff.h"
#include "diskio_impl.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static spi_device_handle_t device;
static char summary[64]="TF: not checked";
static esp_err_t io_error;
static bool high_capacity;
static uint32_t sector_count;
static bool available;
static bool test_write_allowed;
static FATFS filesystem;
static uint8_t exchange(uint8_t value)
{
    spi_transaction_t t={.flags=SPI_TRANS_USE_TXDATA|SPI_TRANS_USE_RXDATA,.length=8};
    t.tx_data[0]=value;
    esp_err_t e=spi_device_polling_transmit(device,&t);
    if(e!=ESP_OK) io_error=e;
    return e==ESP_OK?t.rx_data[0]:0xff;
}
static void deselect(void)
{
    esp_err_t e=board_sd_select(false); if(e!=ESP_OK) io_error=e;
    exchange(0xff);
}
static uint8_t command(unsigned cmd,uint32_t arg)
{
    deselect();
    esp_err_t e=board_sd_select(true); if(e!=ESP_OK) { io_error=e; return 0xff; }
    int64_t until=esp_timer_get_time()+300000;
    while(exchange(0xff)!=0xff) if(io_error!=ESP_OK||esp_timer_get_time()>=until) return 0xff;
    uint8_t packet[]={0x40|cmd,arg>>24,arg>>16,arg>>8,arg,cmd==0?0x95:cmd==8?0x87:0x01};
    spi_transaction_t t={.length=48,.tx_buffer=packet};
    e=spi_device_polling_transmit(device,&t); if(e!=ESP_OK) { io_error=e; return 0xff; }
    for(unsigned i=0;i<16;i++) { uint8_t r=exchange(0xff); if(!(r&0x80)) return r; }
    return 0xff;
}
static uint16_t crc16(const uint8_t *p,size_t n)
{
    uint16_t crc=0;
    while(n--) { crc^=(uint16_t)*p++<<8; for(unsigned i=0;i<8;i++) crc=(crc&0x8000)?(crc<<1)^0x1021:crc<<1; }
    return crc;
}
static esp_err_t read_data(unsigned cmd,uint32_t arg,uint8_t *out,size_t size)
{
    esp_err_t result=ESP_ERR_INVALID_RESPONSE;
    if(command(cmd,arg)!=0) goto done;
    int64_t until=esp_timer_get_time()+500000;
    uint8_t token;
    do { token=exchange(0xff); } while(token==0xff&&io_error==ESP_OK&&esp_timer_get_time()<until);
    if(token!=0xfe) { result=ESP_ERR_TIMEOUT; goto done; }
    /* Full-duplex clocks must send ones while the card supplies the data. */
    uint8_t ones[64]; memset(ones,0xff,sizeof(ones));
    result=ESP_OK;
    for(size_t offset=0;offset<size&&result==ESP_OK;offset+=sizeof(ones)) {
        size_t n=size-offset; if(n>sizeof(ones)) n=sizeof(ones);
        spi_transaction_t t={.length=n*8,.tx_buffer=ones,.rx_buffer=out+offset};
        result=spi_device_polling_transmit(device,&t);
    }
    uint16_t wire=(uint16_t)exchange(0xff)<<8; wire|=exchange(0xff);
    if(result==ESP_OK&&crc16(out,size)!=wire) result=ESP_ERR_INVALID_CRC;
 done:
    deselect(); return io_error!=ESP_OK?io_error:result;
}
static esp_err_t read_sector(uint32_t lba,uint8_t *out) { return read_data(17,high_capacity?lba:lba*512u,out,512); }
static uint32_t le32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static bool boot_sector(const uint8_t *b) { return (b[0]==0xeb||b[0]==0xe9)&&b[510]==0x55&&b[511]==0xaa; }
const char *board_storage_summary(void) { return summary; }
void board_storage_probe(void)
{
    bool bus=false,attached=false; const char *stage="SPI"; esp_err_t err;
    io_error=ESP_OK; high_capacity=false;
    /* Manufacturer BSP: SCLK47, MOSI21, MISO48; CS=XL9555 P0.2. */
    spi_bus_config_t pins={.mosi_io_num=21,.miso_io_num=48,.sclk_io_num=47,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=64};
    err=spi_bus_initialize(SPI2_HOST,&pins,SPI_DMA_DISABLED); if(err!=ESP_OK) goto cleanup;
    bus=true;
    spi_device_interface_config_t config={.clock_speed_hz=400000,.mode=0,.spics_io_num=-1,.queue_size=1};
    err=spi_bus_add_device(SPI2_HOST,&config,&device); if(err!=ESP_OK) goto cleanup;
    attached=true; err=board_sd_select(false); if(err!=ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(20));
    for(unsigned i=0;i<16;i++) exchange(0xff);
    stage="CMD0";
    uint8_t r=0xff;
    for(unsigned i=0;i<10&&r!=1;i++) { r=command(0,0); deselect(); vTaskDelay(pdMS_TO_TICKS(10)); }
    err=ESP_ERR_NOT_FOUND; if(r!=1) goto cleanup;
    stage="CMD8"; r=command(8,0x1aa);
    bool v2=r==1;
    if(v2) {
        uint8_t echo[4]; for(unsigned i=0;i<4;i++) echo[i]=exchange(0xff);
        if(echo[2]!=1||echo[3]!=0xaa) { err=ESP_ERR_INVALID_RESPONSE; goto cleanup; }
    } else if(r!=5) { err=ESP_ERR_INVALID_RESPONSE; goto cleanup; }
    deselect(); stage="ACMD41";
    int64_t until=esp_timer_get_time()+2500000;
    do {
        r=command(55,0); deselect(); if(r>1) break;
        r=command(41,v2?0x40000000:0); deselect();
        if(r==0) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while(io_error==ESP_OK&&esp_timer_get_time()<until);
    err=ESP_ERR_TIMEOUT; if(r!=0) goto cleanup;
    stage="OCR"; err=ESP_ERR_INVALID_RESPONSE;
    if(command(58,0)!=0) goto cleanup;
    uint8_t ocr=exchange(0xff); for(unsigned i=0;i<3;i++) exchange(0xff); deselect();
    high_capacity=(ocr&0x40)!=0;
    if(!high_capacity) { if(command(16,512)!=0) goto cleanup; deselect(); }
    uint8_t csd[16],cid[16],sector[512];
    stage="CSD"; err=read_data(9,0,csd,16); if(err!=ESP_OK) goto cleanup;
    uint64_t bytes=0;
    if((csd[0]>>6)==1) {
        uint32_t size=((csd[7]&63u)<<16)|(csd[8]<<8)|csd[9]; bytes=((uint64_t)size+1)*512*1024;
    } else if((csd[0]>>6)==0) {
        uint32_t size=((csd[6]&3u)<<10)|(csd[7]<<2)|(csd[8]>>6);
        unsigned mult=((csd[9]&3u)<<1)|(csd[10]>>7);
        bytes=((uint64_t)size+1)<<(mult+2+(csd[5]&15));
    }
    if(bytes<512||bytes/512>0xffffffffu) { err=ESP_ERR_NOT_SUPPORTED; goto cleanup; }
    sector_count=bytes/512;
    stage="CID"; err=read_data(10,0,cid,16); if(err!=ESP_OK) goto cleanup;
    char product[6]; for(unsigned i=0;i<5;i++) product[i]=cid[i+3]>=32&&cid[i+3]<127?cid[i+3]:'?'; product[5]=0;
    ESP_LOGI("storage","Card product=%s MID=%02x type=%s bytes=%llu GiB=%.3f",product,cid[0],high_capacity?"SDHC/SDXC":"SDSC",(unsigned long long)bytes,(double)bytes/1073741824.0);
    stage="MBR"; err=read_sector(0,sector); if(err!=ESP_OK) goto cleanup;
    uint32_t start=0;
    if(!boot_sector(sector)&&sector[510]==0x55&&sector[511]==0xaa) {
        for(unsigned i=0;i<4;i++) {
            const uint8_t *p=sector+446+i*16; uint32_t first=le32(p+8),length=le32(p+12);
            if(p[4]&&first&&length&&(uint64_t)first+length<=bytes/512) { start=first; break; }
        }
    }
    if(start) { stage="VBR"; err=read_sector(start,sector); if(err!=ESP_OK) goto cleanup; }
    const char *filesystem="unknown";
    if(boot_sector(sector)) {
        if(!memcmp(sector+3,"EXFAT   ",8)) filesystem="exFAT";
        else if(sector[11]==0&&sector[12]==2&&sector[13]&&!(sector[13]&(sector[13]-1))) {
            uint32_t reserved=sector[14]|(sector[15]<<8),roots=sector[17]|(sector[18]<<8);
            uint32_t total=sector[19]|(sector[20]<<8); if(!total) total=le32(sector+32);
            uint32_t fat=sector[22]|(sector[23]<<8); if(!fat) fat=le32(sector+36);
            uint64_t overhead=reserved+(uint64_t)sector[16]*fat+((roots*32+511)/512);
            if(reserved&&fat&&sector[16]&&overhead<total&&total<=bytes/512-start) {
                uint64_t clusters=(total-overhead)/sector[13]; filesystem=clusters<4085?"FAT12":clusters<65525?"FAT16":"FAT32";
            }
        }
    }
    ESP_LOGI("storage","Filesystem=%s partition_LBA=%lu sector_size=512",filesystem,(unsigned long)start);
    const uint32_t samples[]={1,(uint32_t)(bytes/1024),(uint32_t)(bytes/512-1)};
    for(unsigned i=0;i<3;i++) { stage="sample read"; err=read_sector(samples[i],sector); if(err!=ESP_OK) goto cleanup; }
    snprintf(summary,sizeof(summary),"TF: %.2f GiB %s",(double)bytes/1073741824.0,filesystem);
    ESP_LOGI("storage","PASS read-only sample reads + CRC16; no writes/format; %s",summary);
    err=ESP_OK;
    spi_bus_remove_device(device); attached=false;
    config.clock_speed_hz=10000000;
    err=spi_bus_add_device(SPI2_HOST,&config,&device);
    if(err==ESP_OK) { available=true; return; }
 cleanup:
    if(io_error!=ESP_OK) err=io_error;
    if(attached) { board_sd_select(false); exchange(0xff); spi_bus_remove_device(device); device=NULL; }
    if(bus) spi_bus_free(SPI2_HOST);
    if(err!=ESP_OK) { snprintf(summary,sizeof(summary),"TF: %s (%s)",esp_err_to_name(err),stage); ESP_LOGW("storage","Probe failed at %s: %s",stage,esp_err_to_name(err)); }
}

void board_storage_allow_test_write(bool allowed) { test_write_allowed=allowed; }
static DSTATUS tf_disk_init(BYTE drive) { return available ? (test_write_allowed?0:STA_PROTECT) : STA_NOINIT; }
static DRESULT tf_disk_read(BYTE drive,BYTE *out,DWORD sector,UINT count)
{
    if(!available || !count || (uint64_t)sector+count>sector_count) return RES_PARERR;
    io_error=ESP_OK;
    for(UINT i=0;i<count;i++) if(read_sector(sector+i,out+i*512)!=ESP_OK) return RES_ERROR;
    return RES_OK;
}
static DRESULT tf_disk_write(BYTE d,const BYTE *b,DWORD s,UINT n) {
    if(!test_write_allowed) return RES_WRPRT;
    if(!available||!n||(uint64_t)s+n>sector_count) return RES_PARERR;
    io_error=ESP_OK;
    for(UINT block=0;block<n;block++) {
        esp_err_t err=ESP_FAIL;
        if(command(24,high_capacity?s+block:(s+block)*512u)!=0) goto write_done;
        exchange(0xff); exchange(0xfe);
        const uint8_t *data=b+block*512;
        for(unsigned offset=0;offset<512;offset+=64) {
            spi_transaction_t t={.length=64*8,.tx_buffer=data+offset};
            if(spi_device_polling_transmit(device,&t)!=ESP_OK) goto write_done;
        }
        uint16_t crc=crc16(data,512); exchange(crc>>8); exchange(crc);
        if((exchange(0xff)&0x1f)!=5) goto write_done;
        int64_t until=esp_timer_get_time()+1500000;
        while(exchange(0xff)!=0xff) {
            if(io_error!=ESP_OK||esp_timer_get_time()>=until) goto write_done;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        err=ESP_OK;
write_done:
        deselect();
        if(err!=ESP_OK||io_error!=ESP_OK) return RES_ERROR;
    }
    return RES_OK;
}
static DRESULT tf_disk_ioctl(BYTE d,BYTE command,void *out)
{
    if(command==CTRL_SYNC) return RES_OK;
    if(command==GET_SECTOR_COUNT) { *(DWORD*)out=sector_count; return RES_OK; }
    if(command==GET_SECTOR_SIZE) { *(WORD*)out=512; return RES_OK; }
    if(command==GET_BLOCK_SIZE) { *(DWORD*)out=1; return RES_OK; }
    return RES_PARERR;
}
esp_err_t board_storage_mount(void)
{
    if(!available) return ESP_ERR_NOT_FOUND;
    const ff_diskio_impl_t impl={tf_disk_init,tf_disk_init,tf_disk_read,tf_disk_write,tf_disk_ioctl};
    ff_diskio_register(0,&impl);
    FRESULT result=f_mount(&filesystem,"0:",1);
    ESP_LOGI("storage","Read-only filesystem mount result=%d",result);
    return result==FR_OK?ESP_OK:ESP_FAIL;
}
