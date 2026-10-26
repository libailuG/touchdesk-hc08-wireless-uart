"""PC lower-device simulator. Install pyserial; open the board serial app first."""
import argparse
import math
import struct
import time
import serial

WIRE = struct.Struct('<III8fI')
START = b'\xaa' * 4
END = b'\xee' * 4

def pack(values):
    if len(values) != 10:
        raise ValueError('Expected data1 through data10')
    integers = [int(v, 10) for v in values[:2]]
    if any(v < 0 or v > 0xffffffff for v in integers):
        raise ValueError('data1/data2 must be uint32')
    floats = [float(v) for v in values[2:]]
    if not all(math.isfinite(v) for v in floats):
        raise ValueError('Enter finite float32 values')
    return WIRE.pack(0xaaaaaaaa, *integers, *floats, 0xeeeeeeee)

def frames(buffer):
    while True:
        pos = buffer.find(START)
        if pos < 0:
            del buffer[:-3]
            return
        del buffer[:pos]
        if len(buffer) < WIRE.size:
            return
        if buffer[44:48] != END:
            del buffer[0]
            continue
        frame = bytes(buffer[:48])
        del buffer[:48]
        yield frame

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM10')
    parser.add_argument('--echo', action='store_true', help='Return each valid received frame once')
    parser.add_argument('--values', nargs=10, metavar='VALUE', help='Send one frame on opening')
    args = parser.parse_args()
    outgoing = pack(args.values) if args.values else None
    with serial.Serial(args.port, 921600, timeout=.1, rtscts=False, dsrdtr=False) as port:
        print(f'{args.port}: 921600 8N1, 48-byte LE, no CRC. Ctrl+C to stop.', flush=True)
        if outgoing:
            port.write(outgoing)
            port.flush()
            print('TX:', outgoing.hex(' '), flush=True)
        buffer = bytearray()
        while True:
            buffer.extend(port.read(port.in_waiting or 1))
            for raw in frames(buffer):
                print(time.strftime('%H:%M:%S'), 'RX data1..10:', WIRE.unpack(raw)[1:-1], flush=True)
                if args.echo:
                    port.write(raw)
                    port.flush()
                    print('Echoed 48 bytes', flush=True)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
