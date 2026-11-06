#include "HttpClient.h"

#include <cctype>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#else
#include <curl/curl.h>
#endif

bool HttpClient::isSecure(const std::string& url) {
    // Case-insensitive check for the https:// scheme
    static const char* scheme = "https://";
    if (url.size() < 8) return false;
    for (size_t i = 0; i < 8; i++) {
        if (std::tolower((unsigned char)url[i]) != scheme[i]) return false;
    }
    return true;
}

#ifdef _WIN32

namespace {

struct ParsedUrl {
    std::string host;
    std::string path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

// Splits https://host[:port]/path. The caller has already checked the scheme.
ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    const std::size_t start = url.find("://");
    const std::size_t hostStart = start == std::string::npos ? 0 : start + 3;
    std::size_t hostEnd = url.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos) hostEnd = url.size();

    std::string authority = url.substr(hostStart, hostEnd - hostStart);
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        const std::string portText = authority.substr(colon + 1);
        if (!portText.empty() &&
            portText.find_first_not_of("0123456789") == std::string::npos) {
            parsed.port = static_cast<INTERNET_PORT>(
                std::strtoul(portText.c_str(), nullptr, 10));
            authority = authority.substr(0, colon);
        }
    }
    parsed.host = authority;
    parsed.path = hostEnd == url.size() ? "/" : url.substr(hostEnd);
    return parsed;
}

std::string headerBlock(const std::vector<HttpClient::Header>& headers) {
    std::string block;
    for (const HttpClient::Header& header : headers) {
        // A header value with a line break would let a caller splice in extra
        // headers, so those characters are refused rather than sanitised.
        if (header.name.empty() ||
            header.name.find_first_of("\r\n:") != std::string::npos ||
            header.value.find_first_of("\r\n") != std::string::npos) {
            continue;
        }
        block += header.name;
        block += ": ";
        block += header.value;
        block += "\r\n";
    }
    return block;
}

} // namespace

HttpClient::Response
HttpClient::get(const std::string& url, const std::string& userAgent,
                int timeoutSeconds, const StopCheck& shouldStop,
                const ProgressCallback& onProgress, int connectTimeoutSeconds,
                const std::vector<Header>& headers, bool followRedirects) {
    Response res;

    if (!isSecure(url)) {
        res.error = "Only https:// URLs are allowed";
        return res;
    }

    // Checked before opening, so a request that was cancelled while queued
    // never leaves the machine.
    if (shouldStop && shouldStop()) {
        res.error = "Cancelled";
        return res;
    }

    HINTERNET hInternet = InternetOpenA(
        userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        res.error = "Could not initialise the network";
        return res;
    }

    // Timeouts, so a hanging site cannot freeze the worker thread forever.
    // NOTE: WinInet expects milliseconds here, not seconds. Passing the raw
    // seconds value (15) used to mean a 15 ms timeout, which killed almost
    // every connection on Windows.
    DWORD connectMs = static_cast<DWORD>(connectTimeoutSeconds * 1000);
    DWORD totalMs = static_cast<DWORD>(timeoutSeconds * 1000);
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectMs,
                       sizeof(connectMs));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &totalMs,
                       sizeof(totalMs));
    // Kept short enough for the Cancel button to feel immediate, but nowhere
    // near the 1 s it used to be: WinInet applies this to every read, so a
    // value that low cut the body short on any site that pauses mid-transfer
    // and the truncated page was then reported as a successful empty answer.
    DWORD receiveTimeout = 5000;
    if (shouldStop) {
        receiveTimeout = totalMs < receiveTimeout ? totalMs : receiveTimeout;
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT,
                           &receiveTimeout, sizeof(receiveTimeout));
    } else {
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &totalMs,
                           sizeof(totalMs));
    }

    // The request is opened by hand rather than with InternetOpenUrlA so that
    // caller headers can be attached: an API key has no other way in.
    const ParsedUrl parsed = parseUrl(url);
    HINTERNET hConnect =
        InternetConnectA(hInternet, parsed.host.c_str(), parsed.port, nullptr,
                         nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        res.error = "Could not reach the site (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hInternet);
        return res;
    }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE;
    if (!followRedirects) flags |= INTERNET_FLAG_NO_AUTO_REDIRECT;

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", parsed.path.c_str(),
                                          nullptr, nullptr, nullptr, flags, 0);
    if (!hRequest) {
        res.error = "Could not open the request (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return res;
    }

    const std::string extra = headerBlock(headers);
    if (!HttpSendRequestA(hRequest, extra.empty() ? nullptr : extra.c_str(),
                          extra.empty() ? 0 : static_cast<DWORD>(extra.size()),
                          nullptr, 0)) {
        res.error = "Could not send the request (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return res;
    }

    DWORD statusCode = 0;
    DWORD len = sizeof(statusCode);
    if (HttpQueryInfoA(hRequest,
                       HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &len, NULL)) {
        res.httpStatus = statusCode;
    }

    // Announced size, when the server bothers to send one; -1 keeps the bar in
    // indeterminate mode instead of guessing.
    int64_t totalSize = -1;
    {
        char lengthBuf[32] = {0};
        DWORD lengthLen = sizeof(lengthBuf);
        if (HttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH, lengthBuf,
                           &lengthLen, NULL)) {
            char* endPtr = nullptr;
            long long parsedLength = std::strtoll(lengthBuf, &endPtr, 10);
            if (endPtr != lengthBuf && parsedLength >= 0)
                totalSize = parsedLength;
        }
    }
    auto lastReport = std::chrono::steady_clock::now();
    if (onProgress) onProgress(0, totalSize);

    char buffer[8192];
    DWORD bytesRead = 0;
    auto started = std::chrono::steady_clock::now();
    while (true) {
        bytesRead = 0;
        if (!InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead)) {
            DWORD err = GetLastError();
            // A read that is merely still in flight must not be mistaken for
            // the end of the page: cutting here is what produced "HTTP 200"
            // with half a document and no results. The overall deadline stops
            // this from spinning forever on a dead socket.
            if (err == ERROR_IO_PENDING &&
                std::chrono::steady_clock::now() - started <
                    std::chrono::seconds(timeoutSeconds)) {
                continue;
            }
            // Anything else is a real failure, and it is reported as such
            // instead of being passed off as an empty answer.
            res.body.clear();
            res.error =
                "The connection broke while reading the answer (error " +
                std::to_string(static_cast<unsigned long>(err)) + ")";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        // A successful read with no bytes is the genuine end of the stream.
        if (bytesRead == 0) break;

        // Abort between chunks. The receive timeout above is what delays this
        // check when a site goes silent mid-transfer.
        if (shouldStop && shouldStop()) {
            res.body.clear();
            res.error = "Cancelled";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        if (res.body.size() + bytesRead > MAX_RESPONSE_BYTES) {
            res.body.clear();
            res.error = "Response too large";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        res.body.append(buffer, bytesRead);
        if (onProgress) {
            auto now = std::chrono::steady_clock::now();
            if (now - lastReport > std::chrono::milliseconds(120)) {
                lastReport = now;
                onProgress(static_cast<int64_t>(res.body.size()), totalSize);
            }
        }
    }
    if (onProgress)
        onProgress(static_cast<int64_t>(res.body.size()), totalSize);

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (statusCode >= 300 && statusCode < 400 && !followRedirects) {
        res.body.clear();
        res.error = "The site tried to redirect the request (HTTP " +
                    std::to_string(statusCode) + ")";
        return res;
    }
    if (statusCode >= 400) {
        res.body.clear();
        res.error = "Site answered with HTTP " + std::to_string(statusCode);
        return res;
    }

    res.ok = true;
    return res;
}

HttpClient::Response
HttpClient::postForm(const std::string& url, const std::string& userAgent,
                     const std::string& body, int timeoutSeconds,
                     const StopCheck& shouldStop, int connectTimeoutSeconds,
                     const std::vector<Header>& headers, bool followRedirects) {
    Response res;

    if (!isSecure(url)) {
        res.error = "Only https:// URLs are allowed";
        return res;
    }
    if (shouldStop && shouldStop()) {
        res.error = "Cancelled";
        return res;
    }

    HINTERNET hInternet = InternetOpenA(
        userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        res.error = "Could not initialise the network";
        return res;
    }

    DWORD connectMs = static_cast<DWORD>(connectTimeoutSeconds * 1000);
    DWORD totalMs = static_cast<DWORD>(timeoutSeconds * 1000);
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectMs,
                       sizeof(connectMs));
    InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &totalMs,
                       sizeof(totalMs));
    DWORD receiveTimeout = 5000;
    if (shouldStop) {
        receiveTimeout = totalMs < receiveTimeout ? totalMs : receiveTimeout;
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT,
                           &receiveTimeout, sizeof(receiveTimeout));
    } else {
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &totalMs,
                           sizeof(totalMs));
    }

    const ParsedUrl parsed = parseUrl(url);
    HINTERNET hConnect =
        InternetConnectA(hInternet, parsed.host.c_str(), parsed.port, nullptr,
                         nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        res.error = "Could not reach the site (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hInternet);
        return res;
    }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE;
    if (!followRedirects) flags |= INTERNET_FLAG_NO_AUTO_REDIRECT;

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", parsed.path.c_str(),
                                          nullptr, nullptr, nullptr, flags, 0);
    if (!hRequest) {
        res.error = "Could not open the request (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return res;
    }

    // The form body is sent by HttpSendRequest, the extra headers with it.
    const std::string extra = headerBlock(headers);
    if (!HttpSendRequestA(hRequest, extra.empty() ? nullptr : extra.c_str(),
                          extra.empty() ? 0 : static_cast<DWORD>(extra.size()),
                          const_cast<char*>(body.data()),
                          static_cast<DWORD>(body.size()))) {
        res.error = "Could not send the request (error " +
                    std::to_string(GetLastError()) + ")";
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return res;
    }

    DWORD statusCode = 0;
    DWORD len = sizeof(statusCode);
    if (HttpQueryInfoA(hRequest,
                       HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &len, NULL)) {
        res.httpStatus = statusCode;
    }

    char buffer[8192];
    DWORD bytesRead = 0;
    auto started = std::chrono::steady_clock::now();
    while (true) {
        bytesRead = 0;
        if (!InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING &&
                std::chrono::steady_clock::now() - started <
                    std::chrono::seconds(timeoutSeconds)) {
                continue;
            }
            res.body.clear();
            res.error =
                "The connection broke while reading the answer (error " +
                std::to_string(static_cast<unsigned long>(err)) + ")";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        if (bytesRead == 0) break;
        if (shouldStop && shouldStop()) {
            res.body.clear();
            res.error = "Cancelled";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        if (res.body.size() + bytesRead > MAX_RESPONSE_BYTES) {
            res.body.clear();
            res.error = "Response too large";
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return res;
        }
        res.body.append(buffer, bytesRead);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (statusCode >= 300 && statusCode < 400 && !followRedirects) {
        res.body.clear();
        res.error = "The site tried to redirect the request (HTTP " +
                    std::to_string(statusCode) + ")";
        return res;
    }
    if (statusCode >= 400) {
        res.body.clear();
        res.error = "Site answered with HTTP " + std::to_string(statusCode);
        return res;
    }

    res.ok = true;
    return res;
}

#else

namespace {

size_t writeCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    std::string* out = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;

    if (out->size() + total > HttpClient::MAX_RESPONSE_BYTES) {
        return 0; // Aborts the transfer with CURLE_WRITE_ERROR
    }
    out->append(data, total);
    return total;
}

// Returning anything but 0 makes curl_easy_perform give up straight away, which
// is how the Cancel button interrupts a request that is already in flight.
// When a progress listener is attached it is also fed here, throttled so the UI
// is not flooded with one callback per packet.
struct TransferState {
    const HttpClient::StopCheck* stop = nullptr;
    const HttpClient::ProgressCallback* progress = nullptr;
    std::chrono::steady_clock::time_point lastReport =
        std::chrono::steady_clock::now();
};

int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t, curl_off_t) {
    TransferState* state = static_cast<TransferState*>(clientp);
    if (!state) return 0;
    if (state->stop && *state->stop && (*state->stop)()) return 1;
    if (state->progress && *state->progress) {
        auto now = std::chrono::steady_clock::now();
        if (now - state->lastReport > std::chrono::milliseconds(120)) {
            state->lastReport = now;
            (*state->progress)(static_cast<int64_t>(dlnow),
                               dltotal > 0 ? static_cast<int64_t>(dltotal)
                                           : -1);
        }
    }
    return 0;
}

// A curl header list owns strdup'd copies, so the caller's strings never have
// to outlive the request. Freed by the caller through curl_slist_free_all().
curl_slist* headerList(const std::vector<HttpClient::Header>& headers) {
    curl_slist* list = nullptr;
    for (const HttpClient::Header& header : headers) {
        if (header.name.empty() ||
            header.name.find_first_of("\r\n:") != std::string::npos ||
            header.value.find_first_of("\r\n") != std::string::npos) {
            continue;
        }
        const std::string line = header.name + ": " + header.value;
        list = curl_slist_append(list, line.c_str());
    }
    return list;
}

} // namespace

HttpClient::Response
HttpClient::get(const std::string& url, const std::string& userAgent,
                int timeoutSeconds, const StopCheck& shouldStop,
                const ProgressCallback& onProgress, int connectTimeoutSeconds,
                const std::vector<Header>& headers, bool followRedirects) {
    Response res;

    if (!isSecure(url)) {
        res.error = "Only https:// URLs are allowed";
        return res;
    }

    if (shouldStop && shouldStop()) {
        res.error = "Cancelled";
        return res;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "Could not initialise the network";
        return res;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     static_cast<long>(connectTimeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Some sites only answer search queries to a browser-like request.
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

    curl_slist* list = headerList(headers);
    if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    // Checked several times per second while the transfer runs, so cancelling
    // takes effect immediately even on a large or slow response. The same hook
    // feeds the progress bar, so it shares one state struct.
    TransferState state;
    state.stop = &shouldStop;
    state.progress = &onProgress;
    if (shouldStop || onProgress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    }
    if (onProgress) onProgress(0, -1);

    CURLcode code = curl_easy_perform(curl);

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    res.httpStatus = statusCode;

    if (list) curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    if (code == CURLE_ABORTED_BY_CALLBACK) {
        // Cancelled by the user rather than a genuine failure.
        res.body.clear();
        res.error = "Cancelled";
        return res;
    }
    if (code != CURLE_OK) {
        res.body.clear();
        res.error = std::string("Network error: ") + curl_easy_strerror(code);
        return res;
    }
    if (onProgress) {
        // Pin the bar at full: the transfer is done and the size is exact.
        int64_t done = static_cast<int64_t>(res.body.size());
        onProgress(done, done);
    }
    if (statusCode >= 300 && statusCode < 400 && !followRedirects) {
        res.body.clear();
        res.error = "The site tried to redirect the request (HTTP " +
                    std::to_string(statusCode) + ")";
        return res;
    }
    if (statusCode >= 400) {
        res.body.clear();
        res.error = "Site answered with HTTP " + std::to_string(statusCode);
        return res;
    }

    res.ok = true;
    return res;
}

HttpClient::Response
HttpClient::postForm(const std::string& url, const std::string& userAgent,
                     const std::string& body, int timeoutSeconds,
                     const StopCheck& shouldStop, int connectTimeoutSeconds,
                     const std::vector<Header>& headers, bool followRedirects) {
    Response res;

    if (!isSecure(url)) {
        res.error = "Only https:// URLs are allowed";
        return res;
    }
    if (shouldStop && shouldStop()) {
        res.error = "Cancelled";
        return res;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "Could not initialise the network";
        return res;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     static_cast<long>(connectTimeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body.c_str());

    curl_slist* list = headerList(headers);
    if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    TransferState state;
    state.stop = &shouldStop;
    state.progress = nullptr;
    if (shouldStop) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    }

    CURLcode code = curl_easy_perform(curl);

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    res.httpStatus = statusCode;

    if (list) curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    if (code == CURLE_ABORTED_BY_CALLBACK) {
        res.body.clear();
        res.error = "Cancelled";
        return res;
    }
    if (code != CURLE_OK) {
        res.body.clear();
        res.error = std::string("Network error: ") + curl_easy_strerror(code);
        return res;
    }
    if (statusCode >= 300 && statusCode < 400 && !followRedirects) {
        res.body.clear();
        res.error = "The site tried to redirect the request (HTTP " +
                    std::to_string(statusCode) + ")";
        return res;
    }
    if (statusCode >= 400) {
        res.body.clear();
        res.error = "Site answered with HTTP " + std::to_string(statusCode);
        return res;
    }

    res.ok = true;
    return res;
}

#endif
