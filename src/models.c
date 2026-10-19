#include "gitsight/models.h"
#include <stdlib.h>
#include <string.h>

PR *pr_create(void) {
    PR *pr = calloc(1, sizeof(PR));
    return pr;
}

void pr_free(void *p) {
    free(p);
}

Commit *commit_create(void) {
    Commit *c = calloc(1, sizeof(Commit));
    return c;
}

void commit_free(void *c) {
    free(c);
}

Issue *issue_create(void) {
    Issue *iss = calloc(1, sizeof(Issue));
    return iss;
}

void issue_free(void *i) {
    free(i);
}

Release *release_create(void) {
    Release *r = calloc(1, sizeof(Release));
    return r;
}

void release_free(void *r) {
    free(r);
}

Contributor *contributor_create(void) {
    Contributor *c = calloc(1, sizeof(Contributor));
    return c;
}

void contributor_free(void *c) {
    free(c);
}

LangEntry *lang_create(void) {
    LangEntry *l = calloc(1, sizeof(LangEntry));
    return l;
}

void lang_free(void *l) {
    free(l);
}
