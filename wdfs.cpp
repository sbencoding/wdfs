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

struct id_cache_value {
    bool is_dir;
    std::string id;
};

std::string WdFs::auth_header = std::string("");
std::unordered_map<std::string, id_cache_value> remote_id_map;
std::unordered_map<std::string, int> subfolder_count_cache;
std::unordered_map<std::string, std::string> temp_file_binding;
std::unordered_map<std::string, std::string> create_opened_files;

const int MY_O_RDONLY = 32768;
//const int MY_O_WRONLY = 32769;
//const int MY_O_RDWR = 32770;
//const int MY_O_APPEND = 33792;

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
int list_entries_expand(std::string *path, std::vector<entry_data> *result, const std::string &auth_header) {
    if (remote_id_map.find(*path) != remote_id_map.end()) {
        LOG("[list_entries_expand]: Corresponding ID for %s was found in the cache\n", path->c_str());
        if (!remote_id_map[*path].is_dir) return false;
        std::string entryId = remote_id_map[*path].id;
        std::vector<entry_data> cache_results;
        list_entries(entryId.c_str(), auth_header, &cache_results);
        LOG("[list_entries_expand]: Cached entry had %d entries\n", cache_results.size());
        for (auto it = cache_results.begin(); it != cache_results.end(); ++it) {
            result->push_back(*it);
        }
        return 1;
    }
    std::vector<std::string> parts = split_string(path, '/');
    std::string currentId;
    std::vector<entry_data> currentItems;
    std::string currentFullPath;

    for (auto it = parts.begin(); it != parts.end(); ++it) {
        std::string current = *it;
        if (current.empty()) {
            currentId = "root";
        } else {
            currentFullPath.append("/" + current);
            //LOG("[list_entries_expand]: enumerating remote entries for %s (%s)\n", currentFullPath.c_str(), currentId.c_str());
            bool folderFound = false;
            bool entry_found = false;
            for (auto item = currentItems.begin(); item != currentItems.end(); ++item) {
                entry_data currentEntry = *item;

                if (currentEntry.name == current) {
                    // TODO: should we cache files we encounter or just the path parts we go through?
                    entry_found = true;
                    currentId = currentEntry.id;
                    id_cache_value cache_value;
                    cache_value.id = currentEntry.id;
                    cache_value.is_dir = currentEntry.is_dir;
                    //LOG("[list_entries_expand]: updating cache with %s => %s[%d]\n", currentFullPath.c_str(), currentEntry.name.c_str(), cache_value.is_dir);
                    remote_id_map.insert(make_pair(currentFullPath, cache_value));
                    if (currentEntry.is_dir) {
                        folderFound = true;
                    }
                    break;
                }
            }
            if (!folderFound && !entry_found) return -1;
            else if (!folderFound) return 0;
        }

        currentItems.clear();
        list_entries(currentId.c_str(), auth_header, &currentItems);
    }

    for (auto it = currentItems.begin(); it != currentItems.end(); ++it) {
        result->push_back(*it);
    }

    return 1;
}

int get_subfolder_count(std::string *path, const std::string &auth_header) {
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
        std::vector<entry_data> subfolder_entries;
        int expand_result = list_entries_expand(path, &subfolder_entries, auth_header);
        if (expand_result == 1) { // has entry and it's a directory
            int subfolder_count = 0;
            for (auto entry : subfolder_entries) {
                if (entry.is_dir) subfolder_count++;
            }
            return subfolder_count;
        } else if (expand_result == 0) return -1; // has entry but not directory
        else return -2; // remote doesn't have this entry
    }
}

std::string get_path_remote_id(std::string* path, const std::string &auth_header) {
    if (*path == "/" || *path == "") {
        return "root";
    }
    else if (remote_id_map.find(*path) != remote_id_map.end()) {
        LOG("[get_remote_id]: Path is cached in remote_id_map\n");
        return remote_id_map[*path].id;
    }
    LOG("[get_remote_id]: Path isn't cached, fetching id from server\n");
    std::vector<entry_data> tmp; // TODO: check if there's a way to skip the vector param if not needed by caller
    int expand_result = list_entries_expand(path, &tmp, auth_header);
    LOG("[get_remote_id]: expand result: %d\n", expand_result);
    if (expand_result == 1 || expand_result == 0) { // Entry exists on server
        return get_path_remote_id(path, auth_header); // Will read from cache
    }

    return std::string("");
}

// Get the size of a file on the remote system
int get_remote_file_size(std::string *file_path, const std::string &auth_header) {
    // TODO: name this method better, it's almost the same as the function in bridge.cpp
    std::string file_id = get_path_remote_id(file_path, auth_header);
    if (file_id.empty()) return -1;
    int result = 0;
    bool success = get_file_size(file_id, result, auth_header);
    if (!success) return -1;
    return result;
}

int WdFs::release(const char* file_path, struct fuse_file_info *) {
    LOG("[release]: Releasing file %s\n", file_path);
    std::string str_path(file_path);
    if (temp_file_binding.find(str_path) != temp_file_binding.end()) {
        // File to be released is an open temp file, close the write (upload) call here
        std::string file_name(str_path.substr(str_path.find_last_of('/') + 1));
        std::string remote_temp_id = temp_file_binding[str_path];
        bool close_result = file_write_close(remote_temp_id, auth_header);
        if (!close_result) LOG("[release]: Remote temp file close failed\n");
        else LOG("[release]: Remote temp file closed\n");
        temp_file_binding.erase(str_path);
        // Remove the original file
        std::string original_id = get_path_remote_id(&str_path, auth_header);
        bool remove_result = remove_entry(original_id, auth_header);
        if (!remove_result) {
            LOG("[release]: Failed to remove old file!\n");
            return -1;
        }
        // Update the ID-local cache with the new ID of the old file
        id_cache_value val;
        val.is_dir = false;
        val.id = remote_temp_id;
        remote_id_map[str_path] = val;
        // Rename the new file
        bool rename_result = rename_entry(remote_temp_id, file_name, auth_header);
        if (!rename_result) {
            LOG("[release]: Failed to rename new file to old name!\n");
            return -1;
        }
    } else if (create_opened_files.find(str_path) != create_opened_files.end()) {
        // File is has been created, but hasn't been closed yet
        std::string new_file_id = create_opened_files[str_path];
        create_opened_files.erase(str_path);
        bool close_result = file_write_close(new_file_id, auth_header);
        if (!close_result) {
            LOG("[release]: Failed to close created file!\n");
            return -1;
        }
    }
    return 1337; // return value is ignored
}

int WdFs::open(const char* file_path, struct fuse_file_info *fi) {
    LOG("[open]: Opening file %s\n", file_path);
    LOG("[open]: File opened with %d mode\n", fi->flags);
    // Ignore read only option as remote device is capable of handling offsets while reading
    if (fi->flags == MY_O_RDONLY) return 0;
    // tempfile required because remote can't write to a file after it's closed
    LOG("[open]: File wasn't in read only mode, preloading to remote temp file...\n");
    std::string str_path(file_path);
    std::string parent_path(str_path.substr(0, str_path.find_last_of('/')));
    std::string file_name(str_path.substr(str_path.find_last_of('/') + 1) + ".bridge_temp_file");
    LOG("[open]: Parent folder is: %s\n", parent_path.c_str());
    LOG("[open]: Temp file name is: %s\n", file_name.c_str());
    std::string parent_id = get_path_remote_id(&parent_path, auth_header);
    LOG("[open]: Parent folder ID is: %s\n", parent_id.c_str());
    // Load contents of remote file into the temp file
    std::string remote_id = get_path_remote_id(&str_path, auth_header);
    int remote_file_size = -1;
    if (get_file_size(remote_id, remote_file_size, auth_header)) {
        LOG("[open]: Remote file exists and has %d bytes\n", remote_file_size);
        // Create temp file on remote
        std::string temp_file_id;
        bool temp_open_res = file_write_open(parent_id, file_name, auth_header, temp_file_id);
        if (!temp_open_res) return -1;
        // Read the remote file in chunks
        int bytes_read = 0;
        const int CHUNK_SIZE = 4096;
        void *buffer = malloc(CHUNK_SIZE);
        memset(buffer, 0, CHUNK_SIZE);
        std::string location_hdr("/sdk/v2/files/" + temp_file_id);
        while (bytes_read != remote_file_size) {
            int local_read = 0;
            bool success = read_file(remote_id, buffer, bytes_read, CHUNK_SIZE, local_read, auth_header);
            LOG("[open]: Read %d bytes from remote; progress: %d/%d\n", local_read, bytes_read, remote_file_size);
            if (!success) return -1;
            bytes_read += local_read;
            // TODO: might be better to use realloc if local_read != CHUNK_SIZE
            void *real_data = malloc(local_read);
            memcpy(real_data, buffer, local_read);
            // Write file to remote temp file
            bool write_result = write_file(auth_header, location_hdr, bytes_read - local_read, local_read, (char*) real_data);
            if (!write_result) return -1;
            free(real_data);
        }
        free(buffer);
        LOG("[open]: Remote file copied to temp file on the remote filesystem\n");
        // Bind path to temp file
        temp_file_binding.insert(make_pair(str_path, temp_file_id));
        LOG("[open]: Temp file binding %s=>%s cached\n", file_path, temp_file_id.c_str());
        return 0;
    }
    return -1;
}

int WdFs::write(const char* file_path, const char* buffer, size_t size, off_t offset, struct fuse_file_info *) {
    LOG("[write]: Writing %d bytes of data at %d to %s\n", size, offset, file_path);
    std::string str_path(file_path);
    if (temp_file_binding.find(str_path) != temp_file_binding.end()) {
        // We have a temp file that's open and has the contents of the real locked file
        std::string tmp_id = temp_file_binding[str_path];
        LOG("[write]: Tmp file found with ID: %s\n", tmp_id.c_str());
        std::string location_hdr("/sdk/v2/files/" + tmp_id);
        bool result = write_file(auth_header, location_hdr, (int)offset, (int)size, buffer);
        if (!result) return -1;
        LOG("[write]: %d bytes written to %s\n", (int)size, file_path);
        return (int)size;
    } else {
        // TODO: place the write outside the if statements
        // We don't have a temp file => it's a newly created empty file that's still open for writing
        if (create_opened_files.find(str_path) == create_opened_files.end()) {
            LOG("[write]: Tried to write without tempfile and file's not in created_open map!\n");
            return -1;
        }
        std::string location_hdr("/sdk/v2/files/" + create_opened_files[str_path]);
        bool result = write_file(auth_header, location_hdr, (int)offset, (int)size, buffer);
        if (!result) return -1;
        LOG("[write]: %d bytes written to %s\n", (int)size, file_path);
        return (int)size;
    }
}

int WdFs::create(const char* file_path, mode_t mode, struct fuse_file_info *) {
    LOG("[create]: Creating file %s\n", file_path);
    std::string str_path(file_path);
    std::string parent_path(str_path.substr(0, str_path.find_last_of('/')));
    std::string file_name(str_path.substr(str_path.find_last_of('/') + 1));
    LOG("[create]: Parent folder is: %s\n", parent_path.c_str());
    LOG("[create]: File name is: %s\n", file_name.c_str());
    std::string parent_id = get_path_remote_id(&parent_path, auth_header);
    LOG("[create]: Parent folder ID is: %s\n", parent_id.c_str());

    std::string new_id;
    bool open_result = file_write_open(parent_id, file_name, auth_header, new_id);
    if (!open_result) return -1;
    // Cache new file ID with the create map
    create_opened_files.insert(make_pair(str_path, new_id));
    LOG("[create]: ID of the new file is: %s\n", new_id.c_str());

    return 0;
}

// Remove a directory from the remote system
int WdFs::rmdir(const char* dir_path) {
    LOG("[rmdir]: Removing directory%s\n", dir_path);
    std::string str_path(dir_path);
    std::string remote_entry_id = get_path_remote_id(&str_path, auth_header);
    printf("[rmdir]: ID for remote entry is: %s\n", remote_entry_id.c_str());
    bool result = remove_entry(remote_entry_id, auth_header);
    if (result) {
        LOG("[rmdir]: Directory remove successful\n");
        // Remove folder from the ID cache
        remote_id_map.erase(str_path);
    } else {
        LOG("[rmdir]: Directory remove failed\n");
    }
    return 0;
}

// Remove a file from the remote system
int WdFs::unlink(const char* file_path) {
    LOG("[unlink]: Removing file %s\n", file_path);
    std::string str_path(file_path);
    std::string remote_entry_id = get_path_remote_id(&str_path, auth_header);
    printf("[unlink]: ID for remote entry is: %s\n", remote_entry_id.c_str());
    bool result = remove_entry(remote_entry_id, auth_header);
    if (result) {
        LOG("[unlink]: File remove successful\n");
        // Remove file from the ID cache
        remote_id_map.erase(str_path);
    } else {
        LOG("[unlink]: File remove failed\n");
    }
    return 0;
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
    std::string prefix_id = get_path_remote_id(&path_prefix, auth_header);
    LOG("[mkdir]: ID for path prefix is %s\n", prefix_id.c_str());
    std::string new_id = make_dir(folder_name.c_str(), prefix_id.c_str(), auth_header);
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

    std::vector<entry_data> subfolder_result;
    std::string str_path(path);
    int subfolder_count = get_subfolder_count(&str_path, auth_header);
    if (subfolder_count > -1) { // entry is a folder
        st->st_mode = S_IFDIR | 0755;
        LOG("[getattr] Path %s has %d subfolders\n", path, subfolder_count);
        st->st_nlink = 2 + subfolder_count;
    } else if (subfolder_count == -1) { // entry is a file
        LOG("[getattr] Path %s is a file\n", path);
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        int file_size = get_remote_file_size(&str_path, auth_header);
        LOG("[getattr]: Size of %s is %d bytes\n", path, file_size);
        if (file_size == -1) return -ENOENT; // ID of the file is invalid or size can't be requested
        st->st_size = file_size;
    } else { // entry doesn't exist
            if (create_opened_files.find(str_path) != create_opened_files.end()) {
                // This hack is required here, because the remote device doesn't list the file unless the write to it has been ended with file_write_close
                // The file is kept open after the create operation, because a write call might be the next and it's not possible to write to a remote file if it's been closed
                st->st_mode = S_IFREG | 0644;
                st->st_nlink = 1;
                st->st_size = 0;
            }
            else return -ENOENT;
    }
    return 0;
}

int WdFs::readdir(const char *path , void *buffer, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    LOG("[readdir] Listing folder: %s\n", path);
    filler(buffer, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buffer, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
    std::vector<entry_data> entries;
    std::string str_path(path);
    std::string subfolder_id_param;
    int expand_result = list_entries_expand(&str_path, &entries, auth_header);
    if (expand_result == 1) { // has entry and it's a directory
        LOG("[readdir] Given path is a folder\n");
        for (int i = 0; i < entries.size(); i++) {
            entry_data current = entries[i];
            std::string cache_key(str_path + (str_path == "/" ? "" : "/") + current.name);
            id_cache_value cache_value;
            cache_value.id = current.id;
            cache_value.is_dir = current.is_dir;
            remote_id_map.insert(make_pair(cache_key, cache_value));
            filler(buffer, current.name.c_str(), NULL, 0, FUSE_FILL_DIR_PLUS);
            if (current.is_dir) {
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
        std::vector<entry_data> subfolders;
        list_entries_multiple(subfolder_id_param.c_str(), auth_header, &subfolders);

        for (auto entry : subfolders) {
            if (entry.is_dir) subfolder_count_cache[entry.parent_id]++;
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
    LOG("[read]: Requesting content for file %s [%d:%d]\n", path, offset, offset + size);
    std::string str_path(path);
    std::string file_id = get_path_remote_id(&str_path, auth_header);
    LOG("[read]: File ID on the remote is: %s\n", file_id.c_str());
    if (file_id.empty()) return -1;

    int bytes_read = 0;
    bool success = read_file(file_id, buffer, (int)offset, (int)size, bytes_read, auth_header);
    LOG("[read]: Actual bytes read from file: %d\n", bytes_read);
    if (!success) return -1;
    return bytes_read;
}
