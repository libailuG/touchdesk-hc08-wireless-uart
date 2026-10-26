from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
with tempfile.TemporaryDirectory(prefix='uart-frame-') as temp:
 exe=Path(temp)/'test.exe'
 compat=Path(temp)/'float_compat.h'
 compat.write_text('#ifndef HOST_FLOAT_COMPAT\n#define HOST_FLOAT_COMPAT\n#include <stdlib.h>\nstatic float host_strtof(const char *s,char **end){return (float)strtod(s,end); }\n#define strtof host_strtof\n#endif\n',encoding='utf-8')
 subprocess.run([cc,'-include',str(compat),'-I'+str(root/'main/services'),str(root/'tests/serial_frame_test.c'),str(root/'main/services/serial_frame.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
