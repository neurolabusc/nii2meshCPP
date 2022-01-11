# Regular use, set compiler name, compiler flags, openmp flags zlib flags
CNAME=g++
#linker flags
LFLAGS=-lz
#c flags
CFLAGS=-O3 -DHAVE_ZLIB -DUSE_RADIX
NOTES=
ifeq ($(CXX),g++)
	#not valid for c++
	# CFLAGS += -std=gnu99
	NOTES=Clang faster than gcc (try 'make CXX=clang')
endif

#run "OMP=1 make -j" for both OpenMP and CloudFlare
ifeq "$(OMP)" "1"
	CFLAGS+=-fopenmp -flto
	#to get full file path:
	#gcc-9 --print-file-name=libgomp.a
	#LFLAGS+= /usr/local/Cellar/gcc/9.2.0_3/lib/gcc/9/gcc/x86_64-apple-darwin18/9.2.0/../../../libgomp.a
	LFLAGS+= -static-libgcc -static-libstdc++ -L.

endif


all:
	$(info $(NOTES))
	$(CNAME) $(CFLAGS) nii2mesh.cpp meshify.cpp base64.cpp bwlabel.cpp radixsort.cpp -o nii2mesh $(LFLAGS)
	
