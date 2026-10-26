"""Build the deterministic, read-only Flash resource bundle and C offsets."""
from pathlib import Path
import argparse, hashlib, json, struct, zlib

ROOT=Path(__file__).resolve().parents[1]
MAGIC=b'TDRES001'
HEADER_SIZE=64

def pack(output):
    raw=ROOT/'assets/resource-data'
    entries=json.loads((raw/'manifest.json').read_text(encoding='utf-8'))
    blob=bytearray(HEADER_SIZE)
    defines=[]
    keys=set()
    for entry in entries:
        key=entry['key']
        if key in keys or not key.replace('_','').isalnum(): raise ValueError('Invalid resource key')
        keys.add(key)
        path=raw/entry['file']
        if path.parent!=raw or path.suffix!='.bin': raise ValueError('Invalid resource path')
        data=path.read_bytes()
        if len(data)!=entry['size'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise ValueError('Resource changed: update manifest size and SHA256: '+key)
        while len(blob)%4: blob.append(0)
        macro='RESOURCE_'+key.upper()
        defines += [f'#define {macro}_OFFSET {len(blob)}u', f'#define {macro}_SIZE {len(data)}u']
        blob.extend(data)
    if len(blob)>6*1024*1024: raise ValueError('Resources exceed the assets partition')
    crc=zlib.crc32(blob[HEADER_SIZE:])
    struct.pack_into('<8sIIII',blob,0,MAGIC,1,len(blob),len(entries),crc)
    header='\n'.join(['#pragma once',f'#define RESOURCE_BUNDLE_SIZE {len(blob)}u',
        f'#define RESOURCE_BUNDLE_COUNT {len(entries)}u',f'#define RESOURCE_BUNDLE_CRC 0x{crc:08x}u',*defines,''])
    output=Path(output); (output/'generated').mkdir(parents=True,exist_ok=True)
    for path,data in [(output/'resources.bin',bytes(blob)),(output/'generated/resource_manifest.h',header.encode())]:
        if not path.exists() or path.read_bytes()!=data: path.write_bytes(data)
    print(f'Resources: {len(entries)} blocks, {len(blob)} bytes, CRC32={crc:08x}')
    return bytes(blob),entries

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True)
    pack(parser.parse_args().output)
