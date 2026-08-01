#ifndef GITSIGHT_ERROR_H
#define GITSIGHT_ERROR_H

typedef enum {
    GS_OK = 0,
    ERR_NETWORK,
    ERR_HTTP_STATUS,
    ERR_JSON_PARSE,
    ERR_CONFIG_LOAD,
    ERR_CONFIG_WRITE,
    ERR_INVALID_ARGS,
    ERR_REPO_FORMAT,
    ERR_API_ERROR,
    ERR_MEMORY,
    ERR_UNSUPPORTED_FORMAT,
    ERR_FILE_IO,
    ERR_CURL_INIT,
    ERR_LLM_CONFIG,
    ERR_LLM_API,
    ERR_EXPORT,
    ERR_THREAD
} ErrorCode;

const char *gs_error_string(ErrorCode code);

#endif
