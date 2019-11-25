#include "wdfs.h"
#include "bridge.hpp"
#include <stdio.h>

int main (int argc, char *argv[]) {
    WdFs fs;
    if (argc != 4) {
        fprintf(stderr, "Error: too few arguments given\n");
        fprintf(stderr, "Usage: wd_bridge [user] [pass] [mount point]\n");
        return 1;
    }

    char* username = argv[1];
    char* password = argv[2];
    std::string authorizationHeader;
    if (!login(username, password, &authorizationHeader)) {
        fprintf(stderr, "Login failed... shutting down\n");
        return 1;
    }
    fs.set_authorization_header(authorizationHeader);
    char* mount_path = argv[3];
    char *fakeArgs[3] = {argv[0], "-f", mount_path};
    int result = fs.run(3, fakeArgs);
    return result;
}
