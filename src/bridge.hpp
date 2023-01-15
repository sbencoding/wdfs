#include <vector>
#include <string>
#include <string_view>

namespace bridge {
    struct entry_data {
        int size;
        std::string id;
        std::string name;
        std::string parent_id;
        bool is_dir;
        entry_data() {}
        entry_data(int s, bool dir, std::string _id, std::string _name) : size(s), is_dir(dir), id(_id), name(_name) {}
        entry_data(bool dir, std::string _id, std::string _name, std::string _parent_id) : is_dir(dir), id(_id), name(_name), parent_id(_parent_id) {}
    };

    enum request_result {
        REQUEST_SUCCESS,
        REQUEST_FAILED,
        REQUEST_CACHED
    };

    bool login(std::string_view username, std::string_view password, std::string &session_id, std::string *access_token);
    request_result list_entries(const std::string& path, const std::string &auth_token, std::vector<entry_data> &entries);
    request_result list_entries_multiple(const std::string& ids, const std::string &auth_token, std::vector<entry_data> &entries);
    std::string make_dir(const std::string& folder_name, const std::string& parent_id, const std::string &auth_header);
    bool remove_entry(const std::string &entry_id, const std::string &auth_token);
    bool read_file(const std::string &file_id, void *buffer, int offset, int size, int &bytes_read, const std::string &auth_token);
    request_result get_file_size(const std::string &file_id, int &file_size, const std::string &auth_token);
    bool file_write_open(const std::string &parent_id, const std::string &file_name, const std::string &auth_token, std::string &new_file_id);
    bool file_write_close(const std::string &new_file_id, const std::string &auth_token);
    bool write_file(const std::string &auth_token, const std::string &file_location, int offset, int size, const char *buffer);
    bool rename_entry(const std::string &entry_id, const std::string &new_name, const std::string &auth_token);
    bool move_entry(const std::string &entry_id, const std::string &new_parent_id, const std::string &auth_token);
    bool set_modification_time(const std::string &entry_id, const time_t &new_time, const std::string &auth_token);
    bool init_bridge();
    void release_bridge();
    bool auth0_get_userid(const std::string &auth_token, std::string &user_id);
    bool get_user_devices(const std::string &auth_token, const std::string &user_id, std::vector<std::pair<std::string, std::string>> &device_list);
    bool detect_endpoint(const std::string &auth_token, std::string_view wdhost);
    bool file_overwrite_open(const std::string &file_id, const std::string &auth_token, std::string &new_file_id);
    bool file_truncate(const std::string &file_id, const int offset, const std::string &auth_token, std::string &new_file_id);
}
