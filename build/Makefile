CC := g++
ARCH := $(shell getconf LONG_BIT)
FUSE_FLAGS := $(shell pkg-config fuse3 --cflags)
FUSE_LIBS := $(shell pkg-config fuse3 --libs)
CURL_LIBS := $(shell curl-config --libs)

ifeq ($(ARCH),32)
	FUSE_FLAGS += -D_FILE_OFFSET_BITS=64
endif

.PHONY: clean fs locator all

all: fs locator
fs: format.o bridge.o Fuse.o wdfs.o wd_bridge.o
	$(CC) format.o bridge.o Fuse.o wdfs.o wd_bridge.o $(CURL_LIBS) $(FUSE_LIBS) -o ../bin/wd_bridge
locator: format.o bridge.o device_locator.o
	$(CC) format.o bridge.o device_locator.o $(CURL_LIBS) -o ../bin/device_locator
clean:
	rm *.o
device_locator.o: ../src/device_locator.cpp bridge.o
	$(CC) -c ../src/device_locator.cpp
wd_bridge.o: ../src/wd_bridge.cpp wdfs.o bridge.o
	$(CC) $(FUSE_FLAGS) -c ../src/wd_bridge.cpp
wdfs.o: ../src/wdfs.cpp ../src/wdfs.h
	$(CC) $(FUSE_FLAGS) -c ../src/wdfs.cpp
bridge.o: ../src/bridge.cpp ../src/bridge.hpp ../include/json.hpp format.o
	$(CC) -c ../src/bridge.cpp
Fuse.o: ../include/Fuse.cpp ../include/Fuse.h ../include/Fuse-impl.h
	$(CC) $(FUSE_FLAGS) -c ../include/Fuse.cpp
format.o: ../include/fmt/* ../include/format.cc
	$(CC) -c ../include/format.cc
