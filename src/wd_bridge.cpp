#include "wdfs.h"
#include "bridge.hpp"
#include <stdio.h>
#include <stddef.h>

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
    bool is_remote = true;

    // Initialize the network bridge
    if (!init_bridge(conf.host)) {
        fprintf(stderr, "Network bridge initialization failed... shutting down\n");
        release_bridge();
        return 1;
    }

    // Login to device and handle local/remote connection
    // TODO: figure out if we need local or remote connection automatically
    if (is_remote) {
        std::string access_token;
        bool login_result = login(conf.username, conf.password, authorization_header, &access_token);
        if (!login_result) {
            fprintf(stderr, "Login failed... shutting down\n");
            release_bridge();
            return 1;
        }
        std::string auth0_user_id;
        bool userid_result = auth0_get_userid(access_token, auth0_user_id);
        if (!userid_result) {
            fprintf(stderr, "Failed to get user id... shutting down\n");
            release_bridge();
            return 1;
        }
        bool remote_result = set_remote_mode(authorization_header, auth0_user_id, conf.host);
        if (!remote_result) {
            fprintf(stderr, "Failed to start in remote mode... shutting down\n");
            release_bridge();
            return 1;
        }
    } else {
        bool login_result = login(conf.username, conf.password, authorization_header, NULL);
        if (!login_result) {
            fprintf(stderr, "Login failed... shutting down\n");
            release_bridge();
            return 1;
        }
    }

    WdFs fs;
    fs.set_authorization_header(authorization_header);

    int result = fs.run(3, args.argv);
    release_bridge();
    return result;
}
