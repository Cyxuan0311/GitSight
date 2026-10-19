#include "server.h"

void api_register_meta(Router *r);
void api_register_config(Router *r);
void api_register_subscriptions(Router *r);
void api_register_llms(Router *r);
void api_register_usage(Router *r);
void api_register_jobs(Router *r);

/* Registered in the order a reader of the API would expect them: status,
 * capability metadata, then the resources, then the asynchronous analysis
 * surface. */
void api_register(Router *r) {
    api_register_meta(r);
    api_register_config(r);
    api_register_subscriptions(r);
    api_register_llms(r);
    api_register_usage(r);
    api_register_jobs(r);
}