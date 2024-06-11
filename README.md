## charliefax

*WARNING: This is very much an in-development project! This file is currently 100% of my documentation and the code is shaky at best.*

A fax receiving system built around a hacked-up copy of modem_test from the slmodem softmodem driver.

It expects an asterisk CDR table in postgres to populate metadata, though it'll work fine without.
However, it does require a patched AudioSocket app for asterisk. This change switches the codec from `slin` to `slin48` so that we have an integer multiple of the 9600Hz sample rate that slmodem likes (and won't operate without!!)


I did commit the db password! Thankfully it's not publicly accessible :eyes: 🤦

<hr>

testing asterisk dialplan
```
exten=>1002,1,Answer()
exten=>1002,n,AGI(cdruuidgen.py, ${CDR(uniqueid)})
exten=>1002,n,Set(CDR(userfield)=${UUID})
exten=>1002,n,AudioSocket(${UUID},192.168.1.35:9092)
exten=>1002,n,Hangup()
```

<hr>

agi script to generate uuid: `/var/lib/asterisk/agi-bin/cdruuidgen.py`
```python3
#!/usr/bin/env python3

from asterisk.agi import AGI
import sys
import hashlib
import uuid

if len(sys.argv) != 2:
 print("usage: cdruuidgen.py <system unique id>")
 exit()

agi = AGI()
sysid = sys.argv[1]

hash = hashlib.md5(sys.argv[1].encode('ascii')).digest()
agi.set_variable("UUID", str(uuid.UUID(int=int.from_bytes(hash, 'big'))) )
```

<hr>

big TODOs:
- create RX spooler/handler (go thru inbound directory and handle as appropriate) [integrate into main program?]
- remove image conversion dependency (speaking of, I should probably perhaps integrate efax?)
- make CMakeLists detect whether -m32 is usable or not
- *try and remove the dsplibs.o dependency and get 64-bit code??*
