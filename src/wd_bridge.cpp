#include "wdfs.h"
#include "bridge.hpp"
#include <stdio.h>

#define WDFS_OPT(t, p, v) { t, offsetof(struct WdFsConfig, p), v }

// Filesystem config object
struct WdFsConfig {
    char* username;
    char* password;
    char* host;
};

// Configuration for fuse argument parser
static struct fuse_opt WdFsOpts[] = {
    WDFS_OPT("user=%s", username, 0),
    WDFS_OPT("pass=%s", password, 0),
    WDFS_OPT("host=%s", host, 0),
    FUSE_OPT_END
};

int main (int argc, char *argv[]) {
    fuse_args args = FUSE_ARGS_INIT(argc, argv);
    WdFsConfig conf;

    memset(&conf, 0, sizeof(conf));

    fuse_opt_parse(&args, &conf, WdFsOpts, NULL);

    if (conf.username == NULL || conf.password == NULL || conf.host == NULL) {
        fprintf(stderr, "Error: too few arguments given\n");
        fprintf(stderr, "Usage: wd_bridge -f <mount_point> -ouser=<username>,pass=<password>,host=<device_id>\n");
        return 1;
    }

    std::string authorization_header;

    // Set the host to send requests to
    set_wd_host(conf.host);

    if (!login(conf.username, conf.password, authorization_header, NULL)) {
        fprintf(stderr, "Login failed... shutting down\n");
        return 1;
    }

    WdFs fs;
    fs.set_authorization_header(authorization_header);

    int result = fs.run(3, args.argv);
    return result;
}
