
## VICE installation Ubuntu

sudo apt-get install vice

# The Zimmer download
#
# The VICE version at Zimmers is constantly changing, so the URL below might not exist.
# But, download the latest one.
# The problem is that the files chargen,kernal,basic, etc are missingin the later version
# being replaced with different .bin versions that can be renamed.

wget http://www.zimmers.net/anonftp/pub/cbm/crossplatform/emulators/VICE/vice-3.7.tar.gz
tar -zxvf vice-3.7.tar.gz 

# The safe bet
#
# Guaranteed to work is to use vice-3.4. 
# The tar file is included in the demofx-plus/lib folder.

cd vice-3.4/data/C64
sudo cp chargen kernal basic /usr/share/vice/C64
cd ../DRIVES/
sudo cp d1541II d1571cr dos* /usr/share/vice/DRIVES/

# Format D64, copy PRG to D64 and run D64.

c1541 -format test,xx d64 ~/code/disk.d64 8
c1541 ~/code/disk.d64 -write ~/code/demo.prg 
x64sc ~/code/disk.d64

