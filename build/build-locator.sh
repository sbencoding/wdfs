COMPILER="clang++"
FLAGS="../src/device_locator.cpp ../src/bridge.cpp -o ../bin/device_locator `curl-config --libs`"
if [ "$1" = "gcc" ]
then
    echo "building using gcc"
    COMPILER="g++ -Wno-psabi"
else
    echo "building using clang"
fi
$COMPILER $FLAGS
