import gzip,struct,sys,os
from PIL import Image
d=gzip.open(sys.argv[1]).read(); o=0; recs=[]
while o<len(d):
    h=struct.unpack_from('<6I',d,o); recs.append((h,o+24)); o+=24+768+h[1]
print(len(recs),"records")
def at(f):
    c=[r for r in recs if r[0][0]<=f]; return c[-1] if c else recs[0]
out=sys.argv[2]; os.makedirs(out,exist_ok=True)
for w in [int(x) for x in sys.argv[3:]]:
    h,o=at(w); fr,vn,wh,ds,sl,pp=h; mode=sl>>16; sl&=0xffff
    pal=d[o:o+768]; vm=d[o+768:o+768+vn]
    W,H=wh&0xffff,wh>>16
    print(w,"rec",fr,"mode",mode,W,H,"dstart",ds,"scanlen",sl)
    pitch=sl*8 if mode==3 else 320
    base=(ds*4)%vn if mode==3 else 0
    buf=bytearray()
    for y in range(200): 
        a=base+y*pitch; buf+= (vm[a:a+320] if a+320<=vn else (vm[a:]+vm[:a+320-vn]))
    im=Image.frombytes('P',(320,200),bytes(buf))
    p=[]
    for i in range(256): p+=[min(255,c*4) for c in pal[i*3:i*3+3]]
    im.putpalette(p); im.convert('RGB').save(f'{out}/f{w}.png')
