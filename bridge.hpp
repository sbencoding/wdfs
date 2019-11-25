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
