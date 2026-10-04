#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <functional>
#include <string>

/**
 * @brief Minimal blocking HTTP/HTTPS client used by the search feature.
 *
 * Two backends, chosen at compile time:
 *   - Windows: WinInet (already linked into the project).
 *   - Unix:    libcurl.
 *
 * Both paths enforce HTTPS-only, a connection/total timeout and a cap on the
 * response size, so a misbehaving or hostile site cannot hang the app or
 * exhaust its memory. This client never builds a shell command, so nothing the
 * user types can be interpreted by a shell.
 */
class HttpClient {
public:
    struct Response {
        bool ok = false;
        std::string body;
        std::string error;     // Human readable reason when ok == false
        long httpStatus = 0;
    };

    // Called while the transfer runs; returning true abandons it at once.
    // This is what lets the Cancel button take effect immediately instead of
    // after the current request finishes.
    using StopCheck = std::function<bool()>;

    // Reports download progress as (bytesSoFar, totalOrMinusOne). Called at most
    // a few times per second; total is -1 when the server did not announce it.
    // This is what drives progress bars while a plugin or torrent is fetched.
    using ProgressCallback = std::function<void(int64_t, int64_t)>;

    // Performs a GET. Redirects are followed. Returns false and fills
    // Response::error on any failure; body is empty in that case. When
    // 'shouldStop' is given and starts returning true, the transfer is aborted
    // and Response::error reads "Cancelled". The connection phase has its own
    // shorter timeout so that cancelling (or a dead host) never waits for the
    // full transfer timeout: progress callbacks do not fire while curl is
    // still connecting, so without this Cancel would look frozen.
    static Response get(const std::string& url,
                        const std::string& userAgent,
                        int timeoutSeconds = 15,
                        const StopCheck& shouldStop = nullptr,
                        const ProgressCallback& onProgress = nullptr,
                        int connectTimeoutSeconds = 10);
    // True if the URL uses https:// (search engines must never be queried in
    // the clear, otherwise the query leaks to the network).
    static bool isSecure(const std::string& url);

private:
    // Guards against a site sending more than the cap.
public:
    static const size_t MAX_RESPONSE_BYTES = 5 * 1024 * 1024;
};

#endif // HTTPCLIENT_H