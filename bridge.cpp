#include <stdio.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include "json.hpp"
#include "bridge.hpp"

using json = nlohmann::json;

// Simple key value pair
struct http_header {
    std::string name;
    std::string value;
    http_header(const char* _name, const char* _value) : name(std::string(_name)), value(std::string(_value)) {}
    http_header(std::string _name, std::string _value) : name(_name), value(_value) {}
};

// Data to pass around in CURL header callback
struct response_data {
    // Header key value pairs
    // TODO: better performance with an unordered_map probably
    std::vector<http_header> headers;
    // Status code of the request
    int status_code;
    // Response body
    std::string response_body;
};

// Data for reading response as a buffer
struct buffer_result {
    int bytes_read;
    void *buffer;
};

// Get status code and collect headers
static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    // printf("headers: %s\n", buffer);
    std::string buf(buffer);
    int colonIndex = buf.find(":");
    int httpIndex = buf.find("HTTP/");
    response_data *rData = (response_data *)userdata;
    // printf("Size: %d, nitems: %d, separator index: %d\n", size, nitems, colonIndex);
    if (colonIndex != -1) {
        //std::vector<http_header> *headers = (std::vector<http_header> *)userdata;
        std::string name = buf.substr(0, colonIndex);
        std::string value = buf.substr(colonIndex + 2);
        // printf("Header name: %s; value: %s\n", name.c_str(), value.c_str());
        http_header current(name, value);
        rData->headers.push_back(current);
    } else if (httpIndex == 0) {
        // HTTP/1.1 200 OK => status code is between the 2 spaces
        int firstSpace = buf.find_first_of(" ");
        int lastSpace = buf.find_last_of(" ");
        std::string status_code = buf.substr(firstSpace + 1, lastSpace - firstSpace - 1);
        rData->status_code = std::stoi(status_code);
    }
    return nitems * size;
}

static size_t collect_response_string(void *content, size_t size, size_t nmemb, std::string *data) {
    char *strContent = (char *)content;
    data->append(strContent);
    return size * nmemb;
}

static size_t collect_response_bytes(void *content, size_t size, size_t nmemb, buffer_result *data) {
    memcpy(data->buffer, content, size * nmemb);
    data->bytes_read += (int)size * (int)nmemb;
    return size * nmemb;
}

// Initialize a basic request
static CURL* request_base(const char* method, const char* url, const std::vector<http_header> &headers, const char *request_body, long size, response_data &rd, struct curl_slist *chunk) {
    CURL *curl;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    if (curl) {
        // Set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, url);
        // Set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // Set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
       
        // Set request method
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

        // Set request header
        for (http_header header : headers) {
            std::string header_string(header.name + ":" + header.value);
            chunk = curl_slist_append(chunk, header_string.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Set the request body
        if (request_body != NULL) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, size);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
        }
        return curl;
    }
    return NULL;
}

// Free request resources
static void request_free(CURL *curl, struct curl_slist *chunk, CURLcode res) {
    if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

// Perform a single request and get string response
static response_data make_request(const char* method, const char* url, const std::vector<http_header> &headers, const char *request_body, long size) {
    CURLcode res;
    response_data rd;
    std::string response_body;
    struct curl_slist *chunk = NULL;

    // Get base request
    CURL *curl = request_base(method, url, headers, request_body, size, rd, chunk);
    if (curl) {
        // Collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        rd.response_body = response_body;
        request_free(curl, chunk, res);
    } else curl_global_cleanup(); // Make sure to release curl even if init failed
    return rd;
}

// Generic handler for responses from remote
bool generic_handler(int status_code, std::string &response_body) {
    if (status_code == 401) {
        fprintf(stderr, "The specified username or password is wrong\n");
    } else if (status_code == 400) {
        fprintf(stderr, "The request had bad parameters\n");
        fprintf(stderr, "Response Body:\n%s\n", response_body.c_str());
    } else if (status_code >= 200 && status_code <= 299) {
        return true;
    } else {
        fprintf(stderr, "Unkown error: HTTP(%d): \n%s\n", status_code, response_body.c_str());
    }
    return false;
}

// Login to the remote device
bool login(const char* username, const char* password, std::string &session_id) {
    const char* auth_url = "https://wdc.auth0.com/oauth/ro";
    const char* wdcAuth0ClientID = "56pjpE1J4c6ZyATz3sYP8cMT47CZd6rk";
    json req = {
        {"client_id", wdcAuth0ClientID},
        {"connection", "Username-Password-Authentication"},
        {"device", "123456789"},
        {"grant_type", "password"},
        {"password", password},
        {"username", username},
        {"scope", "openid offline_access"},
    };

    std::string rbody = req.dump();
    std::vector<http_header> hdrs {
        http_header("Content-Type", "application/json")
    };
    response_data resp = make_request("POST", auth_url,  hdrs, rbody.c_str(), (long)rbody.size());
    if (generic_handler(resp.status_code, resp.response_body)) { 
        auto json_response = json::parse(resp.response_body);
        std::string id_token = json_response["id_token"];
        session_id.assign("Bearer " + id_token);
        return true;
    }
    return false;
}

// List entries on the remote device
bool list_entries(const char* path, const std::string &auth_token, std::vector<entry_data> *entries) {
    // TODO: device ID is probably not the same for everyone
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    // TODO: is there better way to concat strings?
    char request_url[300];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name&pretty=false&orderBy=name&order=asc;", wdhost, path);
    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        auto json_response = json::parse(rd.response_body);
        auto folder_contents = json_response["files"];
        for (int i = 0; i < folder_contents.size(); i++) {
            std::string id = folder_contents[i]["id"];
            std::string name = folder_contents[i]["name"];
            bool is_dir = folder_contents[i]["mimeType"] == "application/x.wd.dir";
            entry_data entry;
            entry.id = id;
            entry.name = name;
            entry.is_dir = is_dir;
            entries->push_back(entry);
        }
        return true;
    }
    return false;
}

// List entries on the remote system for multiple entries
bool list_entries_multiple(const char* ids, const std::string &auth_token, std::vector<entry_data> *entries) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[300 + strlen(ids)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name,parentID&pretty=false&orderBy=name&order=asc;", wdhost, ids);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        auto json_response = json::parse(rd.response_body);
        auto folder_contents = json_response["files"];
        for (int i = 0; i < folder_contents.size(); i++) {
            std::string id = folder_contents[i]["id"];
            std::string name = folder_contents[i]["name"];
            bool is_dir = folder_contents[i]["mimeType"] == "application/x.wd.dir";
            std::string parent_id = folder_contents[i]["parentID"];
            entry_data entry;
            entry.id = id;
            entry.name = name;
            entry.is_dir = is_dir;
            entry.parent_id = parent_id;
            entries->push_back(entry);
        }
        return true;
    }
    return false;
}

// Create a new folder on the remote system
std::string make_dir(const char* folder_name, const char* parent_folder_id, const std::string &auth_token) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files?resolveNameConflict=true", wdhost);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token),
        http_header("Content-Type", "multipart/related; boundary=287032381131322")
    };

    json req = {
        {"name", folder_name},
        {"parentID", parent_folder_id},
        {"mimeType", "application/x.wd.dir"},
    };

    std::string body_header("--287032381131322\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
    std::string body_content = req.dump();
    std::string body_footer("\r\n--287032381131322--");
    std::string rbody(body_header + body_content + body_footer);

    response_data rd = make_request("POST", request_url, headers, rbody.c_str(), (long)rbody.size());
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("mkdir request finished with status code 201\n");
        for (auto header : rd.headers) {
            // printf("%s: %s\n", header.name.c_str(), header.value.c_str());
            if (header.name == "location") {
                std::string location_header = header.value;
                int lastPathPart = location_header.find_last_of('/');
                printf("mkdir Found location header\n");
                return location_header.substr(lastPathPart + 1, location_header.size() - lastPathPart - 1);
            }
        }
    }
    return std::string("");
}

// Remove an entry from the remote system
bool remove_entry(const std::string &entry_id, const std::string &auth_token) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + entry_id.size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s", wdhost, entry_id.c_str());
    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };
    response_data rd = make_request("DELETE", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("rm request finished with status code 204\n");
        return true;
    }

    return false;
}

// Read the contents of a file on the remote system
bool read_file(const std::string &file_id, void *buffer, int offset, int size, int &bytes_read, const std::string &auth_token) {
    CURLcode res;
    response_data rd;
    struct curl_slist *chunk = NULL;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + file_id.size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/content?download=true", wdhost, file_id.c_str());

    char range_header[60];
    sprintf(range_header, "bytes=%d-%d", offset, offset + size - 1);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token),
        http_header("Range", range_header),
    };

    CURL *curl = request_base("GET", request_url, headers, NULL, 0, rd, chunk);
    if (curl) {
        buffer_result current_result;
        current_result.buffer = buffer;
        current_result.bytes_read = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_bytes);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &current_result);
        res = curl_easy_perform(curl);
        request_free(curl, chunk, res);
        if (rd.status_code == 416) {
            // Requested range is wrong
            bytes_read = 0;
            printf("read request was for an empty file\n");
            return true;
        } else if (generic_handler(rd.status_code, rd.response_body)) {
            bytes_read = current_result.bytes_read;
            printf("read request finished with status code 206\n");
            return true;
        }
    } else curl_global_cleanup();
    return false;
}

// Get the size of a file on the remote system
bool get_file_size(const std::string &file_id, int &file_size, const std::string &auth_token) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + file_id.size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s?pretty=false&fields=size", wdhost, file_id.c_str());

    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("get_size request finished with status code 200\n");
        auto json_response = json::parse(rd.response_body);
        int size = json_response["size"];
        file_size = size;
        return true;
    }

    return false;
}

// Close an open file on the remote system
bool file_write_close(const std::string &new_file_id, const std::string &auth_token) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[300 + new_file_id.size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/resumable/content?done=true", wdhost, new_file_id.c_str());
    printf("file_write_close request URL is: %s\n", request_url);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };

    response_data rd = make_request("PUT", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("file_write_close request finished with status code 204\n");
        return true;
    }

    return false;
}

// Open a file on the remote system
bool file_write_open(const std::string &parent_id, const std::string &file_name, const std::string &auth_token, std::string &new_file_id) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[200];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/resumable?resolveNameConflict=0&done=false", wdhost);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token),
        http_header("Content-Type", "multipart/related; boundary=287032381131322")
    };

    // Write request body
    // TODO: add legit creation + modification time
    json req = {
        {"name", file_name.c_str()},
        {"parentID", parent_id.c_str()},
        {"mTime", "2019-12-12T12:12:12+02:00"},
    };

    std::string body_header("--287032381131322\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
    std::string body_content = req.dump();
    std::string body_footer("\r\n--287032381131322--");
    std::string rbody(body_header + body_content + body_footer);

    response_data rd = make_request("POST", request_url, headers, rbody.c_str(), (long)rbody.size());
    if (generic_handler(rd.status_code, rd.response_body)) {
        for (http_header header : rd.headers) {
            if (header.name == "location") {
                // -2 to trim \r\n at the end of the header value
                // TODO: remove header EOL when parsing it first
                std::string location_header = header.value.substr(0, header.value.size() - 2);
                int last_path_part_idx = location_header.find_last_of('/');
                printf("file_write_open found location header\n");
                new_file_id = location_header.substr(last_path_part_idx + 1, location_header.size() - last_path_part_idx - 1);
            }
        }
        return true;
    }

    return false;
}

// Write bytes to a file on the remote system
bool write_file(const std::string &auth_token, const std::string &file_location, int offset, int size, const char *buffer) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[100 + file_location.size()];
    sprintf(request_url, "https://%s.remotewd.com%s/resumable/content?offset=%d&done=false", wdhost, file_location.c_str(), offset);

    std::vector<http_header> headers {
        http_header("Authorization", auth_token)
    };

    response_data rd = make_request("PUT", request_url, headers, buffer, (long) size);
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("write_file request finished with status code 204\n");
        return true;
    }

    return false;
}

// Rename a file on the remote system
bool rename_entry(const std::string &entry_id, const std::string &new_name, const std::string &auth_token) {
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[70 + strlen(wdhost)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/patch", wdhost, entry_id.c_str());

    std::vector<http_header> headers {
        http_header("Authorization", auth_token),
        http_header("Content-Type", "text/plain;charset=UTF-8")
    };

    // Write request body
    // TODO: add modification and creation time too
    json req = {
        {"name", new_name.c_str()},
    };

    std::string rbody = req.dump();

    response_data rd = make_request("POST", request_url, headers, rbody.c_str(), (long)rbody.size());
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("rename_entry request finished with status code 204\n");
        return true;
    }

    return false;
}

