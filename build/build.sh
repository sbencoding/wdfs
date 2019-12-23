#g++ wd_bridge.cpp wdfs.cpp bridge.cpp -o sfs2 `pkg-config fuse3 --cflags --libs && curl-config --libs`
clang++ ../src/wd_bridge.cpp ../src/wdfs.cpp ../src/bridge.cpp -o ../bin/wd_bridge `pkg-config fuse3 --cflags --libs && curl-config --libs`
