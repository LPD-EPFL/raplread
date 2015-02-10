ifeq ($(VERSION),DEBUG)
  DEBUG_FLAGS=-Wall -ggdb -DDEBUG
  COMPILE_FLAGS=-O0 -fno-inline
else
  DEBUG_FLAGS=-Wall
  COMPILE_FLAGS=-O3 
endif

ifndef PLATFORM
# PLATFORM=-DSPARC
# PLATFORM=-DTILERA
#PLATFORM=-DXEON
# PLATFORM=-DOPTERON
#PLATFORM=-DDEFAULT
PLATFORM=-DXEON2
endif

UNAME := $(shell uname)

ifeq ($(PLATFORM),-DTILERA)
	GCC:=tile-gcc
	LIBS:=-lrt -lpthread -ltmc
else
ifeq ($(UNAME), Linux)
	GCC:=gcc
	LIBS := -L. -lrt -lpthread -lnuma -lraplread -lm
endif
endif
ifeq ($(UNAME), SunOS)
	GCC:=/opt/csw/bin/gcc
	LIBS := -lrt -lpthread
	COMPILE_FLAGS+= -m64 -mcpu=v9 -mtune=v9
endif

ifeq ($(PLATFORM),-DDEFAULT)
CORE_NUM ?= $(shell nproc)
CORE_SPEED_KHz := $(shell cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq)
FREQ_GHZ := $(shell echo "scale=3; ${CORE_SPEED_KHz}/1000000" | bc -l)
$(info ********************************** Using as a default number of cores: $(CORE_NUM) on 1 socket)
$(info ********************************** Using as a default frequency      : $(FREQ_GHZ) GHz)
COMPILE_FLAGS += -DCORE_NUM=${CORE_NUM} 
COMPILE_FLAGS += -DFREQ_GHZ=${FREQ_GHZ}
endif

COMPILE_FLAGS += $(PLATFORM)

LIB_FILES := libraplread.a

all:  libraplread.a

libraplread.a: rapl_read.c rapl_read.o rapl_read.h
	ar -r libraplread.a rapl_read.o rapl_read.h

rapl_read.o: rapl_read.c rapl_read.h
	$(GCC) -D_GNU_SOURCE $(COMPILE_FLAGS) $(DEBUG_FLAGS) $(INCLUDES) -c rapl_read.c $(LIBS)


clean:
	rm -f *.o *.a






