"""Check the real bundle and compile the production loader with Flash failure mocks."""
from pathlib import Path
import json,re,subprocess,tempfile,hashlib,struct,zlib
from host_regression import HEADERS,ROOT

build=ROOT/'build-wireless'
blob=(build/'resources.bin').read_bytes()
magic,version,size,count,crc=struct.unpack_from('<8sIIII',blob)
assert magic==b'TDRES001' and version==1 and size==len(blob) and crc==zlib.crc32(blob[64:])
entries=json.loads((ROOT/'assets/resource-data/manifest.json').read_text())
assert len(entries)==count
manifest=(build/'generated/resource_manifest.h').read_text()
for entry in entries:
    macro='RESOURCE_'+entry['key'].upper()
    offset=int(re.search(r'#define '+macro+r'_OFFSET (\d+)u',manifest)[1])
    data=blob[offset:offset+entry['size']]
    assert offset%4==0 and hashlib.sha256(data).hexdigest()==entry['sha256']
print(f'PASS bundle: {count} blocks; exact SHA256 matches, 4-byte alignment and whole-bundle CRC32')

headers=dict(HEADERS)
headers['esp_err.h']+='\n#define ESP_ERR_NOT_FOUND 10\n#define ESP_ERR_INVALID_CRC 11\n#define ESP_ERR_INVALID_SIZE 12\n'
headers['esp_partition.h']='''#include <stddef.h>
typedef int esp_partition_mmap_handle_t;
typedef struct {size_t size;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_MMAP_DATA 0
static esp_partition_t partition;
static unsigned char *blob;
static int missing,map_error,maps,unmaps;
static const esp_partition_t *esp_partition_find_first(int t,int s,const char *l){return missing?NULL:&partition;}
static int esp_partition_mmap(const esp_partition_t *p,size_t o,size_t n,int m,const void **data,int *h){maps++;*data=blob;*h=1;return map_error;}
static void esp_partition_munmap(int h){unmaps++;}
'''
headers['esp_rom_crc.h']='''#include <stdint.h>
#include <stddef.h>
static uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *p,size_t n){crc=~crc;while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(int)(crc&1));}return ~crc;}
'''
source=r'''
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "@ROOT@/main/services/resource_store.c"
static unsigned bindings;
static bool bind_ok=true;
bool weather_art_bind_resources(void){bindings++;return bind_ok;}
bool font_ui_bind_resources(void){bindings++;return true;}
bool weather_fonts_bind_resources(void){bindings++;return true;}
bool weather_number_fonts_bind_resources(void){bindings++;return true;}
int main(int argc,char **argv){
 FILE *f=fopen(argv[1],"rb");assert(f);blob=malloc(RESOURCE_BUNDLE_SIZE);assert(blob);
 assert(fread(blob,1,RESOURCE_BUNDLE_SIZE,f)==RESOURCE_BUNDLE_SIZE);fclose(f);
 partition.size=RESOURCE_BUNDLE_SIZE;
 missing=1;assert(resource_store_init()==ESP_ERR_NOT_FOUND&&!maps);missing=0;
 partition.size--;assert(resource_store_init()==ESP_ERR_NOT_FOUND&&!maps);partition.size++;
 map_error=7;assert(resource_store_init()==7&&!base&&!unmaps);map_error=0;
 blob[0]^=1;assert(resource_store_init()==ESP_ERR_INVALID_CRC&&!base&&unmaps==1);blob[0]^=1;
 blob[8]^=1;assert(resource_store_init()==ESP_ERR_INVALID_CRC&&!base);blob[8]^=1;
 blob[12]^=1;assert(resource_store_init()==ESP_ERR_INVALID_CRC&&!base);blob[12]^=1;
 blob[20]^=1;assert(resource_store_init()==ESP_ERR_INVALID_CRC&&!base);blob[20]^=1;
 blob[100]^=1;assert(resource_store_init()==ESP_ERR_INVALID_CRC&&!base);blob[100]^=1;
 bind_ok=false;assert(resource_store_init()==ESP_ERR_INVALID_SIZE&&!base);bind_ok=true;bindings=0;
 assert(resource_store_init()==ESP_OK&&base==blob&&bindings==4);
 int previous_maps=maps;assert(resource_store_init()==ESP_OK&&maps==previous_maps&&bindings==4);
 assert(!resource_store_data(0,1));assert(!resource_store_data(RESOURCE_BUNDLE_SIZE,1));
 assert(!resource_store_data((size_t)-1,1));assert(!resource_store_data(64,(size_t)-1));
 assert(resource_store_data(64,1)==blob+64);
 puts("PASS production resource loader: missing/short partition, mapping error, header/body corruption, binding failure, idempotence and offset overflow");
}
'''
with tempfile.TemporaryDirectory(prefix='resource-store-') as d:
    d=Path(d)
    for name,text in headers.items():
        path=d/name;path.parent.mkdir(parents=True,exist_ok=True)
        guard='RES_'+name.upper().replace('/','_').replace('.','_')
        path.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n',encoding='utf-8')
    src=d/'test.c';src.write_text(source.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=d/'test.exe'
    cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
    subprocess.run([cc,'-I'+str(d),'-I'+str(build/'generated'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(build/'resources.bin')],check=True)
