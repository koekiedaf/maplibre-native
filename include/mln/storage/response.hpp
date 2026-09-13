#pragma once

#include <mln/util/chrono.hpp>

#include <optional>
#include <string>
#include <memory>

namespace mln {

class Response {
public:
    Response() = default;
    Response(const Response&);
    Response& operator=(const Response&);

public:
    class Error;
    // When this object is empty, the response was successful.
    std::unique_ptr<const Error> error;

    // This is set to true for 204 Not Modified responses, and, for backward
    // compatibility, for 404 Not Found responses for tiles.
    bool noContent = false;

    // This is set to true for 304 Not Modified responses.
    bool notModified = false;

    // This is set to true when the server requested that no expired resources
    // be used by specifying "Cache-Control: must-revalidate".
    bool mustRevalidate = false;

    // The actual data of the response. Present only for non-error, non-notModified responses.
    std::shared_ptr<const std::string> data;

    std::optional<Timestamp> modified;
    std::optional<Timestamp> expires;
    std::optional<std::string> etag;

    // DuckMaps fork only. Set from the "X-Terrain-Cache" response header on our own terrain
    // endpoint (container/server/app/map/terrain.py): true for "sea-level" and "above-maxzoom",
    // the two reasons that endpoint answers with the flat sea-level filler tile rather than real
    // archive data. False for "archive"/"archive-ancestor" (real relief, even where it happens to
    // be flat) and false when the header is absent (any other resource, or an offline pack with
    // no header to read) - the safe default for everything that isn't this one endpoint's tiles.
    // This is the one honest, cheap signal for "this ground is not built yet": the server already
    // computes it for exactly this reason, it costs one more header parse alongside the existing
    // ETag/Cache-Control ones, and it never requires the app to know the sea-level tile's bytes.
    bool unbuiltGround = false;

    bool isFresh() const { return expires ? *expires > util::now() : !error; }

    // Indicates whether we are allowed to use this response according to HTTP
    // caching rules. It may or may not be stale.
    bool isUsable() const { return !mustRevalidate || (expires && *expires > util::now()); }
};

class Response::Error {
public:
    enum class Reason : uint8_t {
        Success = 1,
        NotFound = 2,
        Server = 3,
        Connection = 4,
        RateLimit = 5,
        Other = 6,
    } reason = Reason::Other;

    // An error message from the request handler, e.g. a server message or a
    // system message informing the user about the reason for the failure.
    std::string message;

    std::optional<Timestamp> retryAfter;

public:
    Error(Reason, std::string = "", std::optional<Timestamp> = std::nullopt);
};

} // namespace mln
