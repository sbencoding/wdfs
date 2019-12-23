#g++ wd_bridge.cpp wdfs.cpp bridge.cpp -o sfs2 `pkg-config fuse3 --cflags --libs && curl-config --libs`
clang++ wd_bridge.cpp wdfs.cpp bridge.cpp -o wd_bridge `pkg-config fuse3 --cflags --libs && curl-config --libs`
