#include "wdfs.h"
#include "Fuse-impl.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "bridge.hpp"

#define DEBUG_LOGGING

#ifdef DEBUG_LOGGING
    #define LOG(fmt,...) printf(fmt, ##__VA_ARGS__)
#else
    #define LOG
#endif

// Define constant file contents
static char test1text[] = "Hello from the test 1 file with fuse xdfs\0";
static char test2text[] = "Hello from the test 2 file with fuse xdfs\0";

struct id_cache_value {
    bool is_dir;
    std::string id;
};

std::string WdFs::auth_header = std::string("");
std::unordered_map<std::string, id_cache_value> remote_id_map;
std::unordered_map<std::string, int> subfolder_count_cache;

// Set the auth header of the application
void WdFs::set_authorization_header(std::string authorization_header) {
    auth_header = authorization_header;
    LOG("Setting auth header to: %s\n", authorization_header.c_str());
}

std::vector<std::string> split_string(std::string *input, char delimiter) {
    std::vector<std::string> parts;
    int prev = 0;
    while (prev < input->size()) {
        int next_cut = input->find_first_of(delimiter, prev);
        if (next_cut == std::string::npos) { // Delimiter not in string
            parts.push_back(input->substr(prev, input->size() - prev));
            break;
        } else {
            parts.push_back(input->substr(prev, next_cut - prev));
            prev = next_cut + 1;
        }
    }
    return parts;
}

// Returns: 1 => folder found; 0 => folder not found, entry exists; -1 => entry doesn't exist
int list_entries_expand(std::string *path, std::vector<EntryData> *result, std::string *auth_header) {
    if (remote_id_map.find(*path) != remote_id_map.end()) {
        LOG("[list_entries_expand]: Corresponding ID for %s was found in the cache\n", path->c_str());
        if (!remote_id_map[*path].is_dir) return false;
        std::string entryId = remote_id_map[*path].id;
        std::vector<EntryData> cache_results;
        list_entries(entryId.c_str(), *auth_header, &cache_results);
        LOG("[list_entries_expand]: Cached entry had %d entries\n", cache_results.size());
        for (auto it = cache_results.begin(); it != cache_results.end(); ++it) {
            result->push_back(*it);
        }
        return 1;
    }
    std::vector<std::string> parts = split_string(path, '/');
    std::string currentId;
    std::vector<EntryData> currentItems;
    std::string currentFullPath;

    for (auto it = parts.begin(); it != parts.end(); ++it) {
        std::string current = *it;
        if (current.empty()) {
            currentId = "root";
        } else {
            currentFullPath.append("/" + current);
            bool folderFound = false;
            bool entry_found = false;
            for (auto item = currentItems.begin(); item != currentItems.end(); ++item) {
                EntryData currentEntry = *item;
                if (currentEntry.name == current) {
                    entry_found = true;
                    if (currentEntry.isDir) {
                        currentId = currentEntry.id;
                        id_cache_value cache_value;
                        cache_value.id = currentId;
                        cache_value.is_dir = true;
                        // TODO: we could cache files and other things here as well instead of just the touched folders in the path
                        remote_id_map.insert(make_pair(currentFullPath, cache_value));
                        folderFound = true;
                    }
                    break;
                }
            }
            if (!folderFound && !entry_found) return -1;
            else if (!folderFound) return 0;
        }

        currentItems.clear();
        list_entries(currentId.c_str(), *auth_header, &currentItems);
    }

    for (auto it = currentItems.begin(); it != currentItems.end(); ++it) {
        result->push_back(*it);
    }

    return 1;
}

int get_subfolder_count(std::string *path, std::string *auth_header) {
    if (remote_id_map.find(*path) != remote_id_map.end() &&
            subfolder_count_cache.find(remote_id_map[*path].id) != subfolder_count_cache.end()) {
        LOG("[get_subfolder_count]: Corresponding ID for path %s found, ID had cached subfolder count\n", path->c_str());
        int last_known_subfolder_count = subfolder_count_cache[remote_id_map[*path].id];
        // Remove element from cache, this is required to ensure that the subfolder count is always up to date
        // The caching mechanism only caches it between a [readdir] and a [getattr] call for the path
        // This speeds up the process a little-bit in 1 [readdir]-[getattr] call cycle
        subfolder_count_cache.erase(remote_id_map[*path].id);
        return last_known_subfolder_count; 
    } else {
        LOG("[get_subfolder_count]: Corresponding ID for path %s not found or ID didn't have cache subfolder count\n", path->c_str());
        std::vector<EntryData> subfolder_entries;
        int expand_result = list_entries_expand(path, &subfolder_entries, auth_header);
        if (expand_result == 1) { // has entry and it's a directory
            int subfolder_count = 0;
            for (auto entry : subfolder_entries) {
                if (entry.isDir) subfolder_count++;
            }
            return subfolder_count;
        } else if (expand_result == 0) return -1; // has entry but not directory
        else return -2; // remote doesn't have this entry
    }
}

std::string get_path_remote_id(std::string* path, std::string* auth_header) {
    if (*path == "/") {
        return "root";
    }
    else if (remote_id_map.find(*path) != remote_id_map.end()) {
        LOG("[get_remote_id]: Path is cached in remote_id_map");
        return remote_id_map[*path].id;
    }
    LOG("[get_remote_id]: Path isn't cached, fetching id from server");
    std::vector<EntryData> tmp; // TODO: check if there's a way to skip the vector param if not needed by caller
    int expand_result = list_entries_expand(path, &tmp, auth_header);
    if (expand_result == 1 || expand_result == 0) { // Entry exists on server
        // TODO: this won't return IDs for files, since they are not cached!!!
        return get_path_remote_id(path, auth_header); // Will read from cache
    }

    return std::string("");
}

// Create a new directory
int WdFs::mkdir(const char* path, mode_t mode) {
    LOG("[mkdir]: Creating new directory for path %s\n", path);
    std::string str_path(path);
    int folder_name_index = str_path.find_last_of('/');
    std::string folder_name(str_path.substr(folder_name_index + 1, str_path.size() - folder_name_index - 1));
    std::string path_prefix(str_path.substr(0, folder_name_index));
    LOG("[mkdir]: Folder name is %s\n", folder_name.c_str());
    LOG("[mkdir]: Path prefix is %s\n", path_prefix.c_str());
    std::string prefix_id = get_path_remote_id(&path_prefix, &auth_header);
    LOG("[mkdir]: ID for path prefix is %s\n", prefix_id.c_str());
    std::string new_id = make_dir(folder_name.c_str(), prefix_id.c_str(), &auth_header);
    LOG("[mkdir]: Finished with new folder ID: %s\n", new_id.c_str());
    return 0;
}

// Get the attributes of the file
int WdFs::getattr(const char *path, struct stat *st, struct fuse_file_info *) {
    LOG ("[getattr] called for path: %s\n", path);
    LOG("in get attr with path, %s\n", path);

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time(NULL);
    st->st_mtime = time(NULL);

    std::vector<EntryData> subfolder_result;
    std::string str_path(path);
    int subfolder_count = get_subfolder_count(&str_path, &auth_header);
    if (subfolder_count > -1) { // entry is a folder
        st->st_mode = S_IFDIR | 0755;
        LOG("[getattr] Path %s has %d subfolders\n", path, subfolder_count);
        st->st_nlink = 2 + subfolder_count;
    } else if (subfolder_count == -1) { // entry is a file
        LOG("[getattr] Path %s is a file\n", path);
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 1024;
    } else { // entry doesn't exist
        return -ENOENT;
    }
    return 0;
}

int WdFs::readdir(const char *path , void *buffer, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    LOG("[readdir] Listing folder: %s\n", path);
    filler(buffer, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buffer, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
    std::vector<EntryData> entries;
    std::string str_path(path);
    std::string subfolder_id_param;
    int expand_result = list_entries_expand(&str_path, &entries, &auth_header);
    if (expand_result == 1) { // has entry and it's a directory
        LOG("[readdir] Given path is a folder\n");
        for (int i = 0; i < entries.size(); i++) {
            EntryData current = entries[i];
            std::string cache_key(str_path + (str_path == "/" ? "" : "/") + current.name);
            id_cache_value cache_value;
            cache_value.id = current.id;
            cache_value.is_dir = current.isDir;
            remote_id_map.insert(make_pair(cache_key, cache_value));
            filler(buffer, current.name.c_str(), NULL, 0, FUSE_FILL_DIR_PLUS);
            if (current.isDir) {
                subfolder_id_param.append(current.id);
                subfolder_id_param.append(",");
                subfolder_count_cache[current.id] = 0;
            }
        }

        LOG("[readdir.cache_remote_id] Listing remote id cache:\n");
        for (auto item : remote_id_map) {
            LOG("%s => %s\n", item.first.c_str(), item.second.id.c_str());
        }

        subfolder_id_param = subfolder_id_param.substr(0, subfolder_id_param.size() - 1);
        std::vector<EntryData> subfolders;
        list_entries_multiple(subfolder_id_param.c_str(), auth_header, &subfolders);

        for (auto entry : subfolders) {
            if (entry.isDir) subfolder_count_cache[entry.parent_id]++;
        }

        LOG("[readdir.cache_subfolder_count] Listing subfolder count cache:\n");
        for (auto item : subfolder_count_cache) {
            LOG("%s => %d\n", item.first.c_str(), item.second);
        }
    } else if (expand_result == 0) {  // has entry but it's a file
        return 0;
    } else { // remote doesn't have the entry
        return -ENOENT;
    }

    if (strcmp(path, "/") == 0) {
        // filler(buffer, "test_file_1", NULL, 0, FUSE_FILL_DIR_PLUS);
        // filler(buffer, "test_file_2", NULL, 0, FUSE_FILL_DIR_PLUS);
    }
    return 0;
}

int WdFs::read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *) {
    char *selectedFile = NULL;
    if (strcmp(path, "/test_file_1") == 0) {
        LOG("First file selected\n");
        selectedFile = test1text;
    } else if (strcmp(path, "/test_file_2") == 0) {
        selectedFile = test2text;
        LOG("Second file selected\n");
    } else return -1;


    LOG("Reading data from offset: %d, %d bytes\n", offset, size);
    LOG("File content size: %d\n", strlen(selectedFile));

    int actualSize = 0;
    if (size + offset > strlen(selectedFile)) {
        actualSize = strlen(selectedFile) - offset;
    } else actualSize = size;

    LOG("Actual size to be read from file is: %d\n", actualSize);

    memcpy(buffer, selectedFile + offset, actualSize);

    return actualSize;

    if (size + offset > strlen(selectedFile)) {
        LOG("Returning length - offset\n");
        return strlen(selectedFile) - offset;
    } else {
        LOG("Returning size\n");
        return size;
    }
}
