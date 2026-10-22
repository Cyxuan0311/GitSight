#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/error.h"
#include "gitsight/completions.h"
#include "gitsight/perf.h"
#include "gitsight/version.h"
#include <stdio.h>
#include <string.h>

static int dispatch(int argc, char **argv, Options *opts);

int main(int argc, char **argv) {
    Options opts;
    perf_start();
    ErrorCode err = cli_parse(argc, argv, &opts);
    if (err != GS_OK) {
        if (err == ERR_INVALID_ARGS) return 1;
        return 1;
    }

    int rc = dispatch(argc, argv, &opts);
    perf_report();
    return rc;
}

/* The command the arguments selected. Each branch returns the process exit
 * code; the timing is reported once, by main, so a failure still shows how
 * far the run got. */
static int dispatch(int argc, char **argv, Options *opts) {
    if (opts->command == 2) {
        printf("GitSight version %s\n", GS_VERSION);
        return 0;
    }

    if (opts->command == 5) {
        return cmd_usage(opts);
    }

    if (opts->command == 6) {
        /* argv+2 skips argv[0] and the "serve" verb; cmd_serve parses the
         * rest itself because the server has its own flag set. */
        return cmd_serve(argc - 2, argv + 2);
    }

    if (opts->command == 4) {
        const char *s = opts->completion_shell;
        if (s[0] && strcmp(s, "bash") == 0) { fputs(gs_completion_bash, stdout); return 0; }
        if (s[0] && strcmp(s, "zsh") == 0)  { fputs(gs_completion_zsh, stdout);  return 0; }
        if (s[0] && strcmp(s, "fish") == 0) { fputs(gs_completion_fish, stdout); return 0; }
        fprintf(stderr, "Usage: gs completion <shell>\n");
        fprintf(stderr, "Available shells: bash, zsh, fish\n\n");
        fprintf(stderr, "  bash: gs completion bash > ~/.local/share/bash-completion/completions/gs\n");
        fprintf(stderr, "  zsh:  gs completion zsh > ~/.zfunc/_gs   (then add ~/.zfunc to fpath)\n");
        fprintf(stderr, "  fish: gs completion fish > ~/.config/fish/completions/gs.fish\n");
        return 1;
    }

    if (opts->command == 1) {
        return cmd_config(opts);
    }

    if (opts->command == 3) {
        return cmd_watch(opts);
    }

    return cmd_analyze(opts);
}
