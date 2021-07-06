#include "wdfs.h"
#include "bridge.hpp"
#include <stdio.h>
#include <stddef.h>
#include <string_view>

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

    // Initialize the network bridge
    if (!init_bridge()) {
        fprintf(stderr, "Network bridge initialization failed... shutting down\n");
        release_bridge();
        return 1;
    }

    // Login to WD
    std::string access_token;
    std::string_view user(conf.username);
    std::string_view pass(conf.password);
    bool login_result = login(user, pass, authorization_header, &access_token);

    free(conf.username);
    free(conf.password);

    if (!login_result) {
        fprintf(stderr, "Login failed... shutting down\n");
        release_bridge();
        return 1;
    }

    // Select device endpoint to use
    std::string_view device_id(conf.host);
    bool endpoint_result = detect_endpoint(authorization_header, device_id);
    if (!endpoint_result) {
        fprintf(stderr, "Failed to detect the endpoint... shutting down\n");
        release_bridge();
        return 1;
    }

    WdFs fs;
    fs.set_authorization_header(authorization_header);

    int result = fs.run(3, args.argv);
    release_bridge();
    free(conf.host);
    return result;
}
