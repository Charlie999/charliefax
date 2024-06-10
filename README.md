## charliefax

A fax receiving system built around a hacked-up copy of modem_test from the slmodem softmodem driver.

It expects an asterisk CDR table in postgres to populate metadata, though it'll work fine without.
However, it does require a patched AudioSocket app for asterisk. This change switches the codec from `slin` to `slin48` so that we have an integer multiple of the 9600Hz sample rate that slmodem likes (and won't operate without!!)


I did commit the db password! Thankfully it's not publicly accessible :eyes: 🤦
