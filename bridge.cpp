#include <stdio.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include "json.hpp"
#include "bridge.hpp"

using json = nlohmann::json;

// Simple key value pair
struct HttpHeader {
    std::string name;
    std::string value;
};

// Data to pass around in CURL header callback
struct ResponseData {
    // Header key value pairs
    // TODO: better performance with an unordered_map probably
    std::vector<HttpHeader> headers;
    // Status code of the request
    int status_code;
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
    ResponseData *rData = (ResponseData *)userdata;
    // printf("Size: %d, nitems: %d, separator index: %d\n", size, nitems, colonIndex);
    if (colonIndex != -1) {
        //std::vector<HttpHeader> *headers = (std::vector<HttpHeader> *)userdata;
        std::string name = buf.substr(0, colonIndex);
        std::string value = buf.substr(colonIndex + 2);
        // printf("Header name: %s; value: %s\n", name.c_str(), value.c_str());
        HttpHeader current;
        current.name = name;
        current.value = value;
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

// Start an activity on the remote server and return the activity tag
bool login(const char* username, const char* password, std::string *sessionId) {
    /*        const authUrl = 'https://wdc.auth0.com/oauth/ro';
        const wdcAuth0ClientID = '56pjpE1J4c6ZyATz3sYP8cMT47CZd6rk';
        request.post(authUrl, {
            body: JSON.stringify({ // Auth0 specific request, copied from the wdc login request to the authUrl endpoint
                client_id: wdcAuth0ClientID,
                connection: 'Username-Password-Authentication',
                device: '123456789',
                grant_type: 'password',
                password: password,
                username: username,
                scope: 'openid offline_access',
            }),
            headers: {
                'content-type': 'application/json',
            }
        }, (error, response, body) => {
            if (response.status_code === 401) {
                resolve(false);
                return;
            }
            if (error) {
                log.fatal('Error occurred while authenticating to server');
                log.error(error);
                resolve(false);
                return;
            }
            tokens.auth = 'Bearer ' + JSON.parse(body).id_token; // Save the Bearer authorization token
            resolve(true);
        });*/

    const char* authUrl = "https://wdc.auth0.com/oauth/ro";
    const char* wdcAuth0ClientID = "56pjpE1J4c6ZyATz3sYP8cMT47CZd6rk";
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    std::string responseBody;
    if (curl) {
        // Set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, authUrl);
        // Set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // Set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
       
        // Set request method
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");

        // Set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Set the request body
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
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) rbody.size());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rbody.c_str());

        // Collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "The specified username or password is wrong");
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Username or password is not set");
    } else if (rd.status_code == 200) {
        auto jsonResponse = json::parse(responseBody);
        std::string idToken = jsonResponse["id_token"];
        sessionId->assign("Authorization: Bearer " + idToken);
        return true;
    } else {
        fprintf(stderr, "Unkown error: HTTP(%d): \n%s\n", rd.status_code, responseBody.c_str());
    }
    return false;
}

bool list_entries(const char* path, std::string authtoken, std::vector<EntryData> *entries) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    // TODO: device ID is probably not the same for everyone
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    // TODO: is there better way to concat strings?
    char request_url[300];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name&pretty=false&orderBy=name&order=asc;", wdhost, path);
    std::string response_body;
    if (curl) {
        // curl_easy_setopt(curl, curlopt_verbose, 1l);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
       
        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, authtoken.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", authtoken.c_str());
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
    } else if (rd.status_code == 200) {
        auto json_response = json::parse(response_body);
        auto folder_contents = json_response["files"];
        for (int i = 0; i < folder_contents.size(); i++) {
            std::string id = folder_contents[i]["id"];
            std::string name = folder_contents[i]["name"];
            bool isDir = folder_contents[i]["mimeType"] == "application/x.wd.dir";
            EntryData entry;
            entry.id = id;
            entry.name = name;
            entry.isDir = isDir;
            entries->push_back(entry);
            //printf("+ entry %s[%s] folder: %d\n", name.c_str(), id.c_str(), isDir);
        }
        // printf("\nresponse body: %s\n", response_body.c_str());
        return true;
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
    }
    return false;
}

bool list_entries_multiple(const char* ids, std::string authtoken, std::vector<EntryData> *entries) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[300 + strlen(ids)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/filesSearch/parents?ids=%s&fields=id,mimeType,name,parentID&pretty=false&orderBy=name&order=asc;", wdhost, ids);
    std::string response_body;
    if (curl) {
        // curl_easy_setopt(curl, curlopt_verbose, 1l);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
       
        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, authtoken.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", authtoken.c_str());
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
    } else if (rd.status_code == 200) {
        auto json_response = json::parse(response_body);
        auto folder_contents = json_response["files"];
        for (int i = 0; i < folder_contents.size(); i++) {
            std::string id = folder_contents[i]["id"];
            std::string name = folder_contents[i]["name"];
            bool isDir = folder_contents[i]["mimeType"] == "application/x.wd.dir";
            std::string parent_id = folder_contents[i]["parentID"];
            EntryData entry;
            entry.id = id;
            entry.name = name;
            entry.isDir = isDir;
            entry.parent_id = parent_id;
            entries->push_back(entry);
            //printf("+ entry %s[%s] folder: %d\n", name.c_str(), id.c_str(), isDir);
        }
        // printf("\nresponse body: %s\n", response_body.c_str());
        return true;
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
    }
    return false;
}

std::string make_dir(const char* folder_name, const char* parent_folder_id, std::string* auth_token) {
/*
     return new Promise((resolve) => {
        const mkdirUrl = `https://${wdHost}.remotewd.com/sdk/v2/files?resolveNameConflict=true`;
        request.post(mkdirUrl, {
            headers: { 'authorization': authToken }, multipart: [
                {
                    body: JSON.stringify({ // Directory creation parameters copied from wdc request to mkdirUrl endpoint
                        'name': folderName,
                        'parentID': subPath,
                        'mimeType': 'application/x.wd.dir',
                    })
                }
            ]
        }, (error, response) => {
            if (response.statusCode === 401) {
                resolve({ success: false, error: undefined, session: false });
                return;
            }
            if (error) {
                log.fatal('Something went wrong');
                log.debug('Status code: ' + response.statusCode);
                log.error(error);
                resolve({ success: false, error: error, session: true });
                return;
            }

            const locationParts = response.headers['location'].split('/'); // ID of the new folder gets sent in the location header
            resolve({ success: true, error: undefined, session: true, result: locationParts[locationParts.length - 1] });
        });
    });
}
 */

    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files?resolveNameConflict=true", wdhost);
    std::string response_body;
    if (curl) {
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
       
        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        chunk = curl_slist_append(chunk, "Content-Type: multipart/related; boundary=287032381131322");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Write request body
        
        json req = {
            {"name", folder_name},
            {"parentID", parent_folder_id},
            {"mimeType", "application/x.wd.dir"},
        };

        std::string body_header("--287032381131322\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
        std::string body_content = req.dump();
        std::string body_footer("\r\n--287032381131322--");
        std::string rbody(body_header + body_content + body_footer);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) rbody.size());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rbody.c_str());

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
    } else if (rd.status_code == 201) {
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
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
    }
    return std::string("");

}

bool remove_entry(std::string *entry_id, std::string *auth_token) {
    // We're not doing the below js request here
    // We're sending a single request to the /sdk/v2/files endpoint directly instead of the batch request
//// Request body copied from a folder delete request
        //const postBody = `Content-Id: 0\r\n\r\nDELETE /sdk/v2/files/${entryID} HTTP/1.1\r\nHost: ${wdHost}.remotewd.com\r\nAuthorization: ${authToken}\r\n\r\n`;
        //// Send multipart/mixed to the server (since request module doesn't support the /mixed multipart MIME)
        //const result = await multipartMixed(postBody, authToken);
        //if (!result.success) resolve(result);
        //else resolve({ success: result.status == 200, error: undefined, session: true, result: true });
        //const options = {
            //hostname: `${wdHost}.remotewd.com`,
            //port: 443,
            //path: '/sdk/v1/batch',
            //method: 'POST',
            //headers: {
                //'authorizaiton': auth,
                //'content-type': 'multipart/mixed; boundary=' + boundary,
                //'content-length': Buffer.byteLength(data),
            //},
            //// agent: proxyAgent,
        //};

    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + entry_id->size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s", wdhost, entry_id->c_str());
    std::string response_body;
    if (curl) {
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
        // set request method to delete
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
       
        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
        return false;
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
        return false;
    } else if (rd.status_code == 204) {
        printf("rm request finished with status code 204\n");
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
        return false;
    }
    return true;
}

bool read_file(std::string *file_id, void* buffer, int offset, int size, int *bytes_read, std::string *auth_token) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + file_id->size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/content?download=true", wdhost, file_id->c_str());
    char range_header[60];
    sprintf(range_header, "Range: bytes=%d-%d", offset, offset + size - 1);
    std::string response_body;
    if (curl) {
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);

        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        chunk = curl_slist_append(chunk, range_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        buffer_result current_result;
        current_result.buffer = buffer;
        current_result.bytes_read = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_bytes);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &current_result);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        *bytes_read = current_result.bytes_read;
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
        return false;
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
        return false;
    } else if (rd.status_code == 206) {
        printf("read request finished with status code 206\n");
    } else if (rd.status_code == 416) {
        // Requested range is wrong
        *bytes_read = 0;
        printf("read request was for an empty file\n");
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
        return false;
    }
    return true;
}

bool get_file_size(std::string *file_id, int *file_size, std::string *auth_token) { 
        //const dataUrl = `https://${wdHost}.remotewd.com/sdk/v2/files/${fileID}?pretty=false&fields=size`; // Endpoint to get the size of the file
        //request.get(dataUrl, { headers: { 'authorization': authToken } }, (error, response, body) => {
            //if (response.statusCode === 401) {
                //resolve({ sucess: false, error: undefined, session: false });
                //return;
            //}
            //if (error) {
                //log.fatal('Something went wrong');
                //log.debug('Status code: ' + response.statusCode);
                //log.error(error);
                //resolve({ success: false, error: error, session: true });
                //return;
            //}
            //resolve({ success: true, error: undefined, session: true, result: JSON.parse(body).size }); // Get the size of the file from the response
        //});
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[59 + strlen(wdhost) + file_id->size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s?pretty=false&fields=size", wdhost, file_id->c_str());
    std::string response_body;
    if (curl) {
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);

        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
        return false;
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
        return false;
    } else if (rd.status_code == 200) {
        printf("get_size request finished with status code 200\n");
        auto json_response = json::parse(response_body);
        int size = json_response["size"];
        *file_size = size;
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
        return false;
    }
    return true;
}

bool file_write_close(std::string *new_file_id, std::string *auth_token) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[300 + new_file_id->size()];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/resumable/content?done=true", wdhost, new_file_id->c_str());
    printf("file_write_close request URL is: %s\n", request_url);
    std::string response_body;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
        // set request method to delete
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
        return false;
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
        return false;
    } else if (rd.status_code == 204) {
        printf("file_write_close request finished with status code 204\n");
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
        return false;
    }
    return true;
}

bool file_write_open(std::string *parent_id, std::string *file_name, std::string *auth_token, std::string *new_file_id) {
   //const startUpload = (activityID) => {
        //const initUploadUrl = `https://${wdHost}.remotewd.com/sdk/v2/files/resumable?resolveNameConflict=1&done=false`;
        //request.post(initUploadUrl, {
            //headers: {
                //'authorization': authToken,
                //'x-activity-tag': activityID,
            //},
            //multipart: [
                //{
                    //body: JSON.stringify({ // Request copied from a file upload request to the initUploadUrl endpoint
                        //name: path.basename(pathToFile),
                        //parentID: subPath,
                        //mTime: getFormattedTime(),
                    //})
                //},
                //{ body: '' }
            //]
        //}, (error, response) => {
            //if (response.statusCode === 401) {
                //reportDone({ success: false, error: undefined, session: false });
                //return;
            //}
            //if (error) {
                //log.fatal('Something went wrong');
                //log.debug('Status code: ' + response.statusCode);
                //log.error(error);
                //reportDone({ success: false, error: error, session: true });
                //return;
            //}
            //const fileUrl = `https://${wdHost}.remotewd.com${response.headers['location']}/resumable/content`;
            //uploadManual({ authorization: authToken, xActivityTag: activityID, url: fileUrl }, reportCompleted, reportDone, pathToFile); // Upload the file to the server
        //});
    //};

    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[200];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/resumable?resolveNameConflict=0&done=false", wdhost);
    std::string response_body;
    if (curl) {
        // curl_easy_setopt(curl, curlopt_verbose, 1l);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);

        // set request headers
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        chunk = curl_slist_append(chunk, "Content-Type: multipart/related; boundary=287032381131322");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Write request body
        // TODO: add legit creation + modification time
        json req = {
            {"name", file_name->c_str()},
            {"parentID", parent_id->c_str()},
            {"mTime", "2019-12-12T12:12:12+02:00"},
        };

        std::string body_header("--287032381131322\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n");
        std::string body_content = req.dump();
        std::string body_footer("\r\n--287032381131322--");
        std::string rbody(body_header + body_content + body_footer);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) rbody.size());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rbody.c_str());
        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
    } else if (rd.status_code == 201) {
        for (HttpHeader header : rd.headers) {
            if (header.name == "location") {
                // -2 to trim \r\n at the end of the header value
                // TODO: remove header EOL when parsing it first
                std::string location_header = header.value.substr(0, header.value.size() - 2);
                int last_path_part_idx = location_header.find_last_of('/');
                printf("file_write_open found location header\n");
                *new_file_id = location_header.substr(last_path_part_idx + 1, location_header.size() - last_path_part_idx - 1);
            }
        }
        return true;
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
    }
    return false;
}

bool write_file(std::string *auth_token, std::string *file_location, int offset, int size, const char *buffer) {
    //const fileUrl = `https://${wdHost}.remotewd.com${response.headers['location']}/resumable/content?offset=444&done=false`;
    // use PUT request

    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[100 + file_location->size()];
    sprintf(request_url, "https://%s.remotewd.com%s/resumable/content?offset=%d&done=false", wdhost, file_location->c_str(), offset);
    printf("write_file request URL is: %s\n", request_url);
    std::string response_body;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
        // set request method to delete
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Write request body
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) size);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer);

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
        return false;
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
        return false;
    } else if (rd.status_code == 204) {
        printf("write_file request finished with status code 204\n");
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
        return false;
    }
    return true;
}

bool rename_entry(std::string *entry_id, std::string *new_name, std::string *auth_token) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    ResponseData rd;
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
    char request_url[70 + strlen(wdhost)];
    sprintf(request_url, "https://%s.remotewd.com/sdk/v2/files/%s/patch", wdhost, entry_id->c_str());
    std::string response_body;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        // set url of the request
        curl_easy_setopt(curl, CURLOPT_URL, request_url);
        // set header callback
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        // set object to pass to header callback
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);

        // set request header
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, auth_token->c_str());
        chunk = curl_slist_append(chunk, "Content-Type: text/plain;charset=UTF-8");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // Write request body
        // TODO: add modification and creation time too
        json req = {
            {"name", new_name->c_str()},
        };

        std::string rbody = req.dump();

        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) rbody.size());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rbody.c_str());

        // collect response body
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    if (rd.status_code == 401) {
        fprintf(stderr, "the specified username or password is wrong\n%s\n", response_body.c_str());
        fprintf(stderr, "authentication was done with token: %s\n", auth_token->c_str());
    } else if (rd.status_code == 400) {
        fprintf(stderr, "Invalid parameters in the request");
        fprintf(stderr, "Response body: \n%s\n", response_body.c_str());
    } else if (rd.status_code == 204) {
        printf("rename_entry request finished with status code 204\n");
        return true;
    } else {
        fprintf(stderr, "unkown error: http(%d): \n%s\n", rd.status_code, response_body.c_str());
    }
    return false;
}

// Make a get request to the specified url
ResponseData make_get(const char* url) {
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    curl = curl_easy_init();
    ResponseData rd;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rd);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) fprintf(stderr, "request failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return rd;
}

int _main (int argc, char* argv[]) {
    // ResponseData rd = make_get("https://home.mycloud.com");
    if (argc < 3) {
        fprintf(stderr, "Username or password is not specified\n");
        return 1;
    }
    std::string sessionId;
    if (!login(argv[1], argv[2], &sessionId)) {
        fprintf(stderr, "Authentication failed... Read output above for more details!\n");
    }
    printf("Auth header token is: %s\n", sessionId.c_str());
    std::vector<EntryData> results;
    list_entries("root", sessionId, &results);
    for (int i = 0; i < results.size(); i++) {
        printf("+ Entry %s[%s] folder: %d\n", results[i].name.c_str(), results[i].id.c_str(), results[i].isDir);
    }

    /*ResponseData rd = login(argv[1], argv[2]);
    printf("Total headers: %d\n", rd.headers.size());
    printf("Status Code: %d\n", rd.status_code);
    for (int i = 0; i < rd.headers.size(); i++) {
        HttpHeader hdr = rd.headers.at(i);
        printf("name: %s, value: %s\n", hdr.name.c_str(), hdr.value.c_str());
    }*/

    return 0;
}

