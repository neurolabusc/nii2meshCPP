# Regular use, set compiler name, compiler flags, openmp flags zlib flags
CNAME=g++
#linker flags
LFLAGS=-lz
#c flags
CFLAGS=-O3 -DHAVE_ZLIB

ifeq ($(CXX),g++)
	CFLAGS += -std=gnu99
endif

all:
	$(CNAME) $(CFLAGS) nii2mesh.cpp meshify.cpp base64.cpp bwlabel.cpp -o nii2mesh $(LFLAGS)
	
