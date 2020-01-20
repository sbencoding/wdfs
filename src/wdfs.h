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
        static int mkdir(const char* path, mode_t mode);
        static int unlink(const char* file_path);
        static int rmdir(const char* dir_path);
        static int write(const char* file_path, const char* buffer, size_t size, off_t offset, struct fuse_file_info *);
        static int create(const char* file_path, mode_t mode, struct fuse_file_info *);
        static int open(const char* file_path, struct fuse_file_info *);
        static int release(const char* file_path, struct fuse_file_info *);
        static int rename(const char* oldname, const char* newname, unsigned int flags);
        static int utimens(const char* path, const struct timespec tv[2], struct fuse_file_info *fi);
        static int truncate(const char* path, off_t offset, struct fuse_file_info *fi);
        static void set_authorization_header(std::string authorization_header);
};

#endif
