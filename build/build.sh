COMPILER="clang++"
FLAGS="../src/wd_bridge.cpp ../src/wdfs.cpp ../src/bridge.cpp -o ../bin/wd_bridge `pkg-config fuse3 --cflags --libs && curl-config --libs`"
ARCH_FLAGS=""
if [ "$1" = "gcc" ]
then
    echo "building using gcc"
    COMPILER="g++ -Wno-psabi"
else
    echo "building using clang"
fi
if [ "$2" = "32" ]
then
    echo "using 32bit arch flags"
    ARCH_FLAGS="-D_FILE_OFFSET_BITS=64"
fi
$COMPILER $FLAGS $ARCH_FLAGS
