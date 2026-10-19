#include "gitsight/error.h"

const char *gs_error_string(ErrorCode code) {
    switch (code) {
        case GS_OK:              return "Success";
        case ERR_NETWORK:        return "Network error";
        case ERR_HTTP_STATUS:    return "Unexpected HTTP status code";
        case ERR_JSON_PARSE:     return "JSON parse error";
        case ERR_CONFIG_LOAD:    return "Failed to load config file";
        case ERR_CONFIG_WRITE:   return "Failed to write config file";
        case ERR_INVALID_ARGS:   return "Invalid arguments";
        case ERR_REPO_FORMAT:    return "Invalid repository format (use owner/repo)";
        case ERR_API_ERROR:      return "API returned an error";
        case ERR_MEMORY:         return "Memory allocation failed";
        case ERR_UNSUPPORTED_FORMAT: return "Unsupported output format";
        case ERR_FILE_IO:        return "File I/O error";
        case ERR_CURL_INIT:      return "Failed to initialize libcurl";
        case ERR_LLM_CONFIG:     return "LLM configuration incomplete";
        case ERR_LLM_API:        return "LLM API error";
        case ERR_EXPORT:         return "Export failed";
        case ERR_THREAD:         return "Failed to create worker thread";
        case ERR_CANCELLED:      return "Cancelled";
        default:                 return "Unknown error";
    }
}
