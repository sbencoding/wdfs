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
    std::vector<HttpHeader> headers;
    // Status code of the request
    int status_code;
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
    const char* wdhost = "device-local-6147bab3-b7b2-4ebc-93b4-a8c337829d45";
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

