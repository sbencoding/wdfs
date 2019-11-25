#ifndef __WDFS_H_
#define __WDFS_H_

#include "Fuse.h"
#include "Fuse-impl.h"
#include <string>

class WdFs : public Fusepp::Fuse<WdFs> {
    private:
        static std::string auth_header;
    public: 
        WdFs() {}
        ~WdFs() {}
        static int getattr(const char*, struct stat*, struct fuse_file_info *);
        static int readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags);
        static int read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);
        static void set_authorization_header(std::string authorization_header);
};

#endif
