"""Pure RGB565 status page verification; no peripheral or target access."""
from pathlib import Path
import subprocess,shutil,struct,zlib,argparse
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--preview",type=Path);args=parser.parse_args()
root=Path(__file__).resolve().parents[2];out=root/'Logs/UI/U0';out.mkdir(parents=True,exist_ok=True)
exe=out/'test_status.exe'
sources=['Tests/UI/test_status.c','App/UI/status_view.c','App/UI/rgb565_canvas.c','App/UI/status_screen.c']
cmd=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I',str(root/'App'),*[str(root/s) for s in sources],'-o',str(exe)]
subprocess.run(cmd,check=True,timeout=30);subprocess.run([str(exe),str(out/'status.rgb565')],check=True,timeout=30)

if args.preview:
 data=(out/'status.rgb565').read_bytes()
 pixels=[v for (v,) in struct.iter_unpack('<H',data)]
 scanlines=bytearray()
 for y in range(272):
  scanlines.append(0)
  for value in pixels[y*480:(y+1)*480]:scanlines.extend((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
 def chunk(kind,payload):
  return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
 png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',480,272,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(scanlines)))+chunk(b'IEND',b'')
 args.preview.parent.mkdir(parents=True,exist_ok=True);args.preview.write_bytes(png)
 print('Offline framebuffer preview:',args.preview)
