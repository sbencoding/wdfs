#include <vector>
#include <string>

struct EntryData {
    std::string id;
    std::string name;
    bool isDir;
    std::string parent_id;
};

bool login(const char* username, const char* password, std::string* session_id);
bool list_entries(const char* path, std::string authToken, std::vector<EntryData>* entries);
bool list_entries_multiple(const char* ids, std::string authToken, std::vector<EntryData>* entries);
std::string make_dir(const char* folder_name, const char* parent_id, std::string* auth_header);
bool remove_entry(std::string *entry_id, std::string *auth_token);
bool read_file(std::string *file_id, void* buffer, int offset, int size, int *bytes_read, std::string *auth_header);
bool get_file_size(std::string *file_id, int *file_size, std::string *auth_token);
bool file_write_open(std::string *parent_id, std::string *file_name, std::string *auth_token, std::string *new_file_id);
bool file_write_close(std::string *new_file_id, std::string *auth_token);
bool write_file(std::string *auth_token, std::string *file_location, int offset, int size, const char *buffer);
bool rename_entry(std::string *entry_id, std::string *new_name, std::string *auth_token);
