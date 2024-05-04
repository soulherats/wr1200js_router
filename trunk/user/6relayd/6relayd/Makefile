all:  shared-app
install:  install-shared-app

MROOT		= /opt/rt-n56u/trunk/user/6relayd/6relayd/
prefix		= /usr

CROSS_TARGET	= 
CC		= /opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc
CXX		= /opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-g++ -mips32r2 -march=mips32r2 -mtune=1004kc
LD		= $(CROSS_TARGET)ld
AR		= $(CROSS_TARGET)ar
AS		= $(CROSS_TARGET)as
NM		= $(CROSS_TARGET)nm
OBJCOPY		= $(CROSS_TARGET)objcopy
OBJDUMP		= $(CROSS_TARGET)objdump
RANLIB		= $(CROSS_TARGET)ranlib
READELF		= $(CROSS_TARGET)readelf
STRIP		= $(CROSS_TARGET)strip

CFLAGS		= -Os -fomit-frame-pointer -pipe  -Dlinux -D__linux__ -Dunix -DEMBED -I/opt/rt-n56u/trunk/stage/include -Wall -Wno-trigraphs -Wno-strict-aliasing -Wno-format-security -fno-tree-slsr -fno-aggressive-loop-optimizations -Wno-pointer-sign -Wno-unused  
CXXFLAGS	= -Os -fomit-frame-pointer -pipe  -Dlinux -D__linux__ -Dunix -DEMBED -I/opt/rt-n56u/trunk/stage/include -Wall -Wno-trigraphs -Wno-strict-aliasing -Wno-format-security -fno-tree-slsr -fno-aggressive-loop-optimizations 
LDFLAGS		= -L/opt/rt-n56u/trunk/stage/lib  

INCLUDE		= 

include /opt/rt-n56u/trunk/user/6relayd/6relayd/Makefile.in
include /opt/rt-n56u/trunk/user/6relayd/6relayd/Project.mk

