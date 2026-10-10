"""(AI-assisted) Validate actual ELF process parameters, not just native-writer source."""
from pathlib import Path
import struct,sys
path=Path(sys.argv[1]);data=path.read_bytes()
assert data[:6]==b'\x7fELF\x02\x01'
phoff=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
headers=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*size) for i in range(count)]
def offset(address):
    for typ,flags,file,virtual,physical,length,memory,align in headers:
        if virtual<=address<virtual+length:return file+address-virtual
    raise AssertionError(f'unmapped address {address:x}')
dynamic=next(h for h in headers if h[0]==2)
tags=dict(struct.unpack_from('<QQ',data,at) for at in range(dynamic[2],dynamic[2]+dynamic[5],16))
rela=offset(tags[7]);relocations={}
for at in range(rela,rela+tags[8],24):
    address,info,addend=struct.unpack_from('<QQq',data,at)
    if info&0xffffffff==8:relocations[address]=addend
def pointer(address):return relocations.get(address,struct.unpack_from('<Q',data,offset(address))[0])
def word(address):return struct.unpack_from('<Q',data,offset(address))[0]
process=next(h for h in headers if h[0]==0x61000001)[3]
libc=pointer(process+0x38);kernel=pointer(process+0x40)
assert word(libc+8)==0x0e,'libc was configured to use the small internal heap'
assert word(pointer(libc+0x58))==0x100000000,'wrong kernel internal-memory budget'
assert word(pointer(libc+0x68))==0x100000000,'wrong maximum system-memory budget'
assert word(pointer(kernel+0x10))==0xfc000000,'wrong flexible-memory budget'
print('ELF memory parameters match working PS5SX2: flexible heap, 4-GiB memory budgets and relocations verified (AI-assisted)')
