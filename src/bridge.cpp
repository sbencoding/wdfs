#include <stdio.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <time.h>
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
    std::unordered_map<std::string, std::string> headers;
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

// Map request urls to etags
std::unordered_map<std::string, std::string> etag_mapping;

// Store the URL start with the given remote device endpoint
char *request_start = NULL;

// Set the remote host for the requests
void set_wd_host(const char *wdhost) {
    // Size of the URL start + 1 for zero termination
    int str_size = 23 + strlen(wdhost);
    // Allocate / reallocated the size of the buffer
    if (request_start != NULL) {
        request_start = (char *) realloc(request_start, str_size);
    } else request_start = (char *) malloc(str_size);
    // Construct the URL start
    sprintf(request_start, "https://%s.remotewd.com/", wdhost);
}

std::string get_formatted_time() {
    // Get current time
    time_t t;
    struct tm *tmp;
    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        fprintf(stderr, "[get_formatted_time]: Failed to get local time\n");
        return std::string("");
    }

    // Get date & time and UTC offset of current time
    char formatted_result[100];
    char offset_result[6];
    if (strftime(formatted_result, sizeof(formatted_result), "%Y-%m-%dT%H:%M:%S", tmp) == 0) {
        fprintf(stderr, "[get_formatted_time]: strftime call failed for date nad time\n");
    }
    if (strftime(offset_result, sizeof(offset_result), "%z", tmp) == 0) {
        fprintf(stderr, "[get_formatted_time]: strftime call failed for offset\n");
    }

    // Reformat the offset to RFC3339
    std::string str_offset(offset_result);
    str_offset.insert(3, ":");
    return std::string(formatted_result + str_offset);
}

// Get status code and collect headers
static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    std::string buf(buffer);
    int colon_index = buf.find(":");
    int http_index = buf.find("HTTP/");
    response_data *data = (response_data *)userdata;
    if (colon_index != -1) {
        std::string name = buf.substr(0, colon_index);
        std::string value = buf.substr(colon_index + 2);
        // remove the trailing \r\n
        value = value.substr(0, value.size() - 2);
        //http_header current(name, value);
        data->headers.insert(make_pair(name, value));
    } else if (http_index == 0) {
        // HTTP/1.1 200 OK => status code is between the 2 spaces
        int first_space = buf.find_first_of(" ");
        int last_space = buf.find_last_of(" ");
        std::string status_code = buf.substr(first_space + 1, last_space - first_space - 1);
        data->status_code = std::stoi(status_code);
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
static CURL* request_base(const char* method, const char* url, const std::vector<std::string> &headers, const char *request_body, long size, response_data &rd, struct curl_slist *chunk) {
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

        // Set request headers
        for (std::string header : headers) {
            chunk = curl_slist_append(chunk, header.c_str());
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

static std::string encode_url_part(const char* url_part) {
    // Init curl
    CURL *curl;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    // Escape URL part
    char *escaped = curl_easy_escape(curl, url_part, strlen(url_part));
    std::string str_escaped(escaped);
    // Free char pointer and curl
    curl_free(escaped);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return str_escaped;
}

// Perform a single request and get string response
static response_data make_request(const char* method, const char* url, const std::vector<std::string> &headers, const char *request_body, long size) {
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
bool login(const char* username, const char* password, std::string &session_id, std::string *access_token) {
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
    std::vector<std::string> hdrs {
        "Content-Type: application/json"
    };
    response_data resp = make_request("POST", auth_url,  hdrs, rbody.c_str(), (long)rbody.size());
    if (generic_handler(resp.status_code, resp.response_body)) { 
        auto json_response = json::parse(resp.response_body);
        std::string id_token = json_response["id_token"];
        session_id.assign("Authorization: Bearer " + id_token);
        if (access_token != NULL) {
            std::string acc_token = json_response["access_token"];
            access_token->assign("Authorization: Bearer " + acc_token);
        }
        return true;
    }
    return false;
}

// List entries on the remote device
request_result list_entries(const char* path, const std::string &auth_token, std::vector<entry_data> &entries) {
    char request_url[98 + strlen(path) + strlen(request_start)];
    sprintf(request_url, "%ssdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name,size&pretty=false&orderBy=name&order=asc;", request_start, path);
    std::vector<std::string> headers {
        auth_token
    };

    std::string str_url(request_url);
    if (etag_mapping.find(str_url) != etag_mapping.end()) {
        // We have a previous ETag, add if-none-match to the headers
        headers.push_back(std::string("If-None-Match: " + etag_mapping[str_url]));
    }

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (rd.status_code == 304) {
        // Entries haven't changed since last run
        return REQUEST_CACHED;
    } else if (generic_handler(rd.status_code, rd.response_body)) {
        // Update ETag mapping
        if (rd.headers.find("etag") != rd.headers.end()) {
            etag_mapping[str_url] = rd.headers["etag"];
        }
        auto json_response = json::parse(rd.response_body);
        auto folder_contents = json_response["files"];
        for (int i = 0; i < folder_contents.size(); i++) {
            std::string id = folder_contents[i]["id"];
            std::string name = folder_contents[i]["name"];
            bool is_dir = folder_contents[i]["mimeType"] == "application/x.wd.dir";
            int size = 0;
            if (folder_contents[i]["size"] != nullptr) size = folder_contents[i]["size"];
            entry_data entry;
            entry.id = id;
            entry.name = name;
            entry.is_dir = is_dir;
            entry.size = size;
            entries.push_back(entry);
        }
        return REQUEST_SUCCESS;
    }
    return REQUEST_FAILED;
}

// List entries on the remote system for multiple entries
request_result list_entries_multiple(const char* ids, const std::string &auth_token, std::vector<entry_data> &entries) {
    char request_url[102 + strlen(ids) + strlen(request_start)];
    sprintf(request_url, "%ssdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name,parentID&pretty=false&orderBy=name&order=asc;", request_start, ids);

    std::vector<std::string> headers {
        auth_token
    };

    std::string str_url(request_url);
    if (etag_mapping.find(str_url) != etag_mapping.end()) {
        // We have a previous ETag, add if-none-match to the headers
        headers.push_back(std::string("If-None-Match: " + etag_mapping[str_url]));
    }

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (rd.status_code == 304) {
        return REQUEST_CACHED;
    } else if (generic_handler(rd.status_code, rd.response_body)) {
        // Update ETag mapping
        if (rd.headers.find("etag") != rd.headers.end()) {
            etag_mapping[str_url] = rd.headers["etag"];
        }

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
            entries.push_back(entry);
        }
        return REQUEST_SUCCESS;
    }
    return REQUEST_FAILED;
}

// Create a new folder on the remote system
std::string make_dir(const char* folder_name, const char* parent_folder_id, const std::string &auth_token) {
    char request_url[38 + strlen(request_start)];
    sprintf(request_url, "%ssdk/v2/files?resolveNameConflict=true", request_start);

    std::vector<std::string> headers {
        auth_token,
        "Content-Type: multipart/related; boundary=287032381131322"
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
        if (rd.headers.find("location") != rd.headers.end()) {
            std::string location_header = rd.headers["location"];
            int lastPathPart = location_header.find_last_of('/');
            printf("mkdir Found location header\n");
            return location_header.substr(lastPathPart + 1, location_header.size() - lastPathPart - 1);
        }
    }
    return std::string("");
}

// Remove an entry from the remote system
bool remove_entry(const std::string &entry_id, const std::string &auth_token) {
    char request_url[14 + strlen(request_start) + entry_id.size()];
    sprintf(request_url, "%ssdk/v2/files/%s", request_start, entry_id.c_str());
    std::vector<std::string> headers {
        auth_token
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
    char request_url[36 + strlen(request_start) + file_id.size()];
    sprintf(request_url, "%ssdk/v2/files/%s/content?download=true", request_start, file_id.c_str());

    char range_header[67];
    sprintf(range_header, "Range: bytes=%d-%d", offset, offset + size - 1);

    std::vector<std::string> headers {
        auth_token,
        std::string(range_header)
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
request_result get_file_size(const std::string &file_id, int &file_size, const std::string &auth_token) {
    char request_url[39 + strlen(request_start) + file_id.size()];
    sprintf(request_url, "%ssdk/v2/files/%s?pretty=false&fields=size", request_start, file_id.c_str());

    std::vector<std::string> headers {
        auth_token
    };

    std::string str_url(request_url);

    if (etag_mapping.find(str_url) != etag_mapping.end()) {
        // We have a previous ETag, add if-none-match to the headers
        headers.push_back(std::string("If-None-Match: " + etag_mapping[str_url]));
    }

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (rd.status_code == 304) {
        return REQUEST_CACHED;
    } else if (generic_handler(rd.status_code, rd.response_body)) {
        // Update ETag mapping
        if (rd.headers.find("etag") != rd.headers.end()) {
            etag_mapping[str_url] = rd.headers["etag"];
        }
        printf("get_size request finished with status code 200\n");
        auto json_response = json::parse(rd.response_body);
        int size = json_response["size"];
        file_size = size;
        return REQUEST_SUCCESS;
    }

    return REQUEST_FAILED;
}

// Close an open file on the remote system
bool file_write_close(const std::string &new_file_id, const std::string &auth_token) {
    char request_url[42 + strlen(request_start) + new_file_id.size()];
    sprintf(request_url, "%ssdk/v2/files/%s/resumable/content?done=true", request_start, new_file_id.c_str());
    printf("file_write_close request URL is: %s\n", request_url);

    std::vector<std::string> headers {
        auth_token
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
    char request_url[54 + strlen(request_start)];
    sprintf(request_url, "%ssdk/v2/files/resumable?resolveNameConflict=0&done=false", request_start);

    std::vector<std::string> headers {
        auth_token,
        "Content-Type: multipart/related; boundary=287032381131322"
    };

    // Write request body
    std::string current_time = get_formatted_time();
    json req = {
        {"name", file_name.c_str()},
        {"parentID", parent_id.c_str()},
        {"mTime", current_time.c_str()},
    };

    std::string body_header("--287032381131322\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
    std::string body_content = req.dump();
    std::string body_footer("\r\n--287032381131322--");
    std::string rbody(body_header + body_content + body_footer);

    response_data rd = make_request("POST", request_url, headers, rbody.c_str(), (long)rbody.size());
    if (generic_handler(rd.status_code, rd.response_body)) {
        if (rd.headers.find("location") != rd.headers.end()) { 
            std::string location_header = rd.headers["location"];
            int last_path_part_idx = location_header.find_last_of('/');
            printf("file_write_open found location header\n");
            new_file_id = location_header.substr(last_path_part_idx + 1, location_header.size() - last_path_part_idx - 1);
        }
        return true;
    }

    return false;
}

// Write bytes to a file on the remote system
bool write_file(const std::string &auth_token, const std::string &file_location, int offset, int size, const char *buffer) {
    char request_url[50 + strlen(request_start) + file_location.size()];
    sprintf(request_url, "%s%s/resumable/content?offset=%d&done=false", request_start, file_location.c_str(), offset);

    std::vector<std::string> headers {
        auth_token
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
    char request_url[20 + strlen(request_start) + entry_id.size()];
    sprintf(request_url, "%ssdk/v2/files/%s/patch", request_start, entry_id.c_str());

    std::vector<std::string> headers {
        auth_token,
        "Content-Type: text/plain;charset=UTF-8"
    };

    // Write request body
    std::string current_time = get_formatted_time();
    json req = {
        {"name", new_name.c_str()},
        {"mTime", current_time.c_str()}
    };

    std::string rbody = req.dump();

    response_data rd = make_request("POST", request_url, headers, rbody.c_str(), (long)rbody.size());
    if (generic_handler(rd.status_code, rd.response_body)) {
        printf("rename_entry request finished with status code 204\n");
        return true;
    }

    return false;
}

// Get the auth0 userid of the user
bool auth0_get_userid(const std::string &auth_token, std::string &user_id) {
    const char *request_url = "https://wdc.auth0.com/userinfo";
    std::vector<std::string> headers {
        auth_token
    };

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        auto json_response = json::parse(rd.response_body);
        std::string id_token = json_response["user_id"];
        user_id = id_token;
        return true;
    }
    return false;
}

// Get user devices with their names and their IDs
bool get_user_devices(const std::string &auth_token, const std::string &user_id, std::vector<std::pair<std::string, std::string>> &device_list) {
    //https://device.mycloud.com/device/v1/user/{auth0_user_id}

    char request_url[43 + user_id.size()];
    std::string escaped_user_id = encode_url_part(user_id.c_str());
    sprintf(request_url, "https://device.mycloud.com/device/v1/user/%s", escaped_user_id.c_str());

    std::vector<std::string> headers {
        auth_token
    };

    response_data rd = make_request("GET", request_url, headers, NULL, 0L);
    if (generic_handler(rd.status_code, rd.response_body)) {
        auto json_response = json::parse(rd.response_body);
        auto data_obj = json_response["data"];
        for (int i = 0; i < data_obj.size(); i++) {
            std::string device_id = data_obj[i]["deviceId"];
            std::string device_name = data_obj[i]["name"];
            device_list.push_back(make_pair(device_id, device_name));
        }
        return true;
    }
    return false;
}