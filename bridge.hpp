#include <vector>
#include <string>

struct entry_data {
    std::string id;
    std::string name;
    bool is_dir;
    std::string parent_id;
};

bool login(const char* username, const char* password, std::string &session_id);
bool list_entries(const char* path, const std::string &auth_token, std::vector<entry_data> &entries);
bool list_entries_multiple(const char* ids, const std::string &auth_token, std::vector<entry_data> &entries);
std::string make_dir(const char* folder_name, const char* parent_id, const std::string &auth_header);
bool remove_entry(const std::string &entry_id, const std::string &auth_token);
bool read_file(const std::string &file_id, void *buffer, int offset, int size, int &bytes_read, const std::string &auth_token);
bool get_file_size(const std::string &file_id, int &file_size, const std::string &auth_token);
bool file_write_open(const std::string &parent_id, const std::string &file_name, const std::string &auth_token, std::string &new_file_id);
bool file_write_close(const std::string &new_file_id, const std::string &auth_token);
bool write_file(const std::string &auth_token, const std::string &file_location, int offset, int size, const char *buffer);
bool rename_entry(const std::string &entry_id, const std::string &new_name, const std::string &auth_token);
void set_wd_host(const char* host);
