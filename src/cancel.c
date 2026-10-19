#include "gitsight/cancel.h"

#include <curl/curl.h>

atomic_int gs_cancel_global = 0;

/* The CLI never binds, so it shares this one flag. */
static _Thread_local const atomic_int *tls_flag = NULL;

void gs_cancel_bind(const atomic_int *flag) {
    tls_flag = flag;
}

const atomic_int *gs_cancel_current(void) {
    return tls_flag ? tls_flag : &gs_cancel_global;
}

void gs_cancel_reset(void) {
    atomic_store(gs_cancel_current(), 0);
}

void gs_cancel_request(void) {
    atomic_store(gs_cancel_current(), 1);
}

int gs_cancel_requested(void) {
    return atomic_load(gs_cancel_current());
}

static int cancel_xfer(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return gs_cancel_requested() ? 1 : 0;
}

void gs_cancel_attach(void *easy) {
    if (!easy) return;
    curl_easy_setopt((CURL *)easy, CURLOPT_XFERINFOFUNCTION, cancel_xfer);
    curl_easy_setopt((CURL *)easy, CURLOPT_NOPROGRESS, 0L);
}