#include "cmd.h"
#include "gitsight/cli.h"
#include "gitsight/config.h"
#include "gitsight/color.h"
#include "gitsight/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_config(const Options *opts) {
    ErrorCode err;
    char path[1024];
    get_config_path(path, sizeof(path), opts);

    if (opts->list_llms) {
        Config cfg;
        config_init(&cfg);
        config_load(&cfg, path);
        if (cfg.llm_count == 0) {
            fprintf(stderr, "No LLM providers configured.\n");
        } else {
            fprintf(stderr, "Available LLM providers:\n");
            color_init(opts->color_mode >= 0 ? opts->color_mode : cfg.color_mode);
            config_list_llms(&cfg);
        }
        return 0;
    }

    if (opts->set_llm) {
        err = config_set_active_llm(path, opts->set_llm_value);
        if (err == GS_OK) {
            fprintf(stderr, "Active LLM set to '%s'\n", opts->set_llm_value);
        } else {
            fprintf(stderr, "Error: LLM provider '%s' not found.\n", opts->set_llm_value);
            return 1;
        }
        return 0;
    }

    if (opts->set_token) {
        err = config_set_token(path, opts->set_token_value);
        if (err == GS_OK) {
            fprintf(stderr, "GitHub token saved to %s\n", path);
        } else {
            fprintf(stderr, "Failed to save token: %s\n", gs_error_string(err));
            return 1;
        }
        return 0;
    }

    if (opts->set_config) {
        err = config_set(path, opts->set_key, opts->set_value);
        if (err == GS_OK) {
            fprintf(stderr, "Set %s = %s in %s\n", opts->set_key, opts->set_value, path);
        } else {
            fprintf(stderr, "Failed to set %s: %s\n", opts->set_key, gs_error_string(err));
            return 1;
        }
        return 0;
    }

    if (opts->show_config) {
        FILE *fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "Config file does not exist: %s\n", path);
            return 1;
        }
        char line[1024];
        while (fgets(line, sizeof(line), fp))
            printf("%s", line);
        fclose(fp);
        return 0;
    }

    if (opts->edit_config) {
        FILE *fp = fopen(path, "r");
        if (fp) {
            fclose(fp);
        } else {
            err = config_save_default(path);
            if (err != GS_OK) {
                fprintf(stderr, "Failed to create default config: %s\n", gs_error_string(err));
                return 1;
            }
            fprintf(stderr, "Created default config at %s\n", path);
        }
        const char *editor = getenv("VISUAL");
        if (!editor || !editor[0]) editor = getenv("EDITOR");
        if (!editor || !editor[0]) editor = "vi";
        char cmd[1400];
        snprintf(cmd, sizeof(cmd), "%s \"%s\"", editor, path);
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr, "Editor exited with status %d\n", rc);
            return 1;
        }
        return 0;
    }

    fprintf(stderr, "Config path: %s\n", path);

    FILE *fp = fopen(path, "r");
    if (fp) {
        fprintf(stderr, "Config file exists.\n");
        fclose(fp);
    } else {
        fprintf(stderr, "Config file does not exist. Creating default...\n");
        err = config_save_default(path);
        if (err == GS_OK) {
            fprintf(stderr, "Default config created at %s\n", path);
        } else {
            fprintf(stderr, "Failed to create config: %s\n", gs_error_string(err));
            return 1;
        }
    }
    return 0;
}
