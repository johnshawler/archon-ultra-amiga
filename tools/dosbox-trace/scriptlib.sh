mk () 
{ 
    mkdir -p $1;
    python3 -I -c "
ev=[]
def key(f,k,dur=4): ev.append((f,f'key {k} 1')); ev.append((f+dur,f'key {k} 0'))
$2
for f,c in sorted(ev,key=lambda x:x[0]): print(f,c)
print('$3 cov'); print('$3 quit')" > $1/script.txt
}
sheet () 
{ 
    python3 -I shots.py $1/vram.bin.gz shots_$1 ${@:2} > /dev/null;
    python3 -I -c "
from PIL import Image
fr=[$(echo ${@:2} | tr ' ' ',')]
W=Image.new('RGB',(960,200*((len(fr)+2)//3)))
for i,f in enumerate(fr): W.paste(Image.open(f'shots_$1/f{f}.png'),((i%3)*320,(i//3)*200))
W.save('shots/sheet_$1.png')"
}
sheet () 
{ 
    python3 -I shots.py $1/vram.bin.gz shots_$1 ${@:2} > /dev/null;
    python3 -I -c "
from PIL import Image
fr=[$(echo ${@:2} | tr ' ' ',')]
cw,ch=150,130
W=Image.new('RGB',(cw*2*4,ch*2*((len(fr)+3)//4)))
for i,f in enumerate(fr): W.paste(Image.open(f'shots_$1/f{f}.png').crop((88,35,88+cw,35+ch)).resize((cw*2,ch*2)),((i%4)*cw*2,(i//4)*ch*2))
W.save('shots/sheet_$1.png')"
}
