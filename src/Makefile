# Regular use, set compiler name, compiler flags, openmp flags zlib flags
CNAME=g++
#linker flags
LFLAGS=-lz
#c flags
CFLAGS=-O3 -DHAVE_ZLIB -DUSE_RADIX

#run "USE_TIMERS=1 make" for timing information
ifeq "$(USE_TIMERS)" "1"
	CFLAGS+= -DUSE_TIMERS
endif

ifeq ($(CXX),g++)
	CFLAGS += -std=gnu99
endif

all:
	$(CNAME) $(CFLAGS) nii2mesh.cpp meshify.cpp base64.cpp bwlabel.cpp radixsort.cpp -o nii2mesh $(LFLAGS)
	
