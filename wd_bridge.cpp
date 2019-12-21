#include "wdfs.h"
#include "bridge.hpp"
#include <stdio.h>

int main (int argc, char *argv[]) {
    WdFs fs;
    if (argc != 5) {
        fprintf(stderr, "Error: too few arguments given\n");
        fprintf(stderr, "Usage: wd_bridge [user] [pass] [mount point] [remote device ID]\n");
        return 1;
    }

    char* username = argv[1];
    char* password = argv[2];
    char* wdhost = argv[4];
    std::string authorization_header;
    // Set the host to send requests to
    set_wd_host(wdhost);

    if (!login(username, password, authorization_header)) {
        fprintf(stderr, "Login failed... shutting down\n");
        return 1;
    }
    fs.set_authorization_header(authorization_header);
    char* mount_path = argv[3];
    char *fakeArgs[3] = {argv[0], "-f", mount_path};
    int result = fs.run(3, fakeArgs);
    return result;
}
