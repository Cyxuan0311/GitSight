#include "gitsight/cli.h"
#include "gitsight/error.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

void cli_print_usage(void) {
    printf("Usage: gs <command> [options]\n\n");
    printf("Commands:\n");
    printf("  analyze <owner/repo>    Analyze a repository (multiple repos allowed)\n");
    printf("  watch                   Subscribe to repositories and view them\n");
    printf("                          (bare 'gs watch' analyzes all subscriptions)\n");
    printf("  config                  Show/edit configuration\n");
    printf("  version                 Show version\n");
    printf("  completion <shell>      Print a shell completion script (bash, zsh, fish)\n\n");
    printf("Analyze options:\n");
    printf("  -d, --since <days>      Analysis period in days (default: 7)\n");
    printf("  --max-pages <n>         Max API result pages to fetch (default: 20)\n");
    printf("  --platform <name>       Platform: github or gitlab (default: github)\n");
    printf("  -t, --token <token>     Platform access token (GitHub or GitLab)\n");
    printf("  -f, --format <fmt>      Output format: json, md, txt, xlsx, pdf (default: md)\n");
    printf("  -o, --output <path>     Output file path (default: stdout)\n");
    printf("  --proxy <url>           HTTP proxy URL\n");
    printf("  --api-base <url>        API base URL (mirror support)\n");
    printf("  --color <mode>          Color mode: auto, always, never\n");
    printf("  --llm <name>            LLM provider name to use\n");
    printf("  --llm-mode <mode>       LLM focus: health, trends, release (default: health)\n");
    printf("  --export <key=val>      Override export config (repeatable, e.g.\n");
    printf("                          --export pdf.margin=40 --export show_prs=false)\n");
    printf("  --no-cache               Bypass the disk response cache\n");
    printf("  -q, --quiet              Suppress progress output\n");
    printf("  --compare <a>:<b>        Compare two analysis periods (e.g. --compare 7:30)\n");
    printf("  --config <path>         Config file path\n");
    printf("  -h, --help              Show this help\n\n");
    printf("Config options:\n");
    printf("  --set-token <token>     Save GitHub token to config file\n");
    printf("  --set-llm <name>        Set active LLM provider\n");
    printf("  --list-llms             List available LLM providers\n");
    printf("  --show                  Print the config file contents\n");
    printf("  --edit                  Open the config file in $EDITOR\n");
    printf("  --set <key=value>       Set a config value (e.g. --set default_days=30\n");
    printf("                          --set export.item_limit=5 --set llms.local.model=qwen)\n\n");
    printf("Examples:\n");
    printf("  gs analyze rust-lang/rust\n");
    printf("  gs analyze rust-lang/rust -d 30 -f json -o report.json --token ghp_xxx\n");
    printf("  gs analyze owner/repo --proxy http://127.0.0.1:7890\n");
    printf("  gs analyze owner/repo --llm local\n");
    printf("  gs config --set-token ghp_xxx\n");
    printf("  gs config --set-llm local\n");
    printf("  gs config --list-llms\n");
    printf("  gs watch add owner/repo --alias core --days 14 --format json\n");
    printf("  gs watch update core --alias backend --days 30\n");
    printf("  gs watch remove core\n");
    printf("  gs watch list\n");
    printf("  gs watch\n");
    printf("  gs completion bash > ~/.local/share/bash-completion/completions/gs\n");
}

static void cli_print_watch_usage(void) {
    printf("Usage: gs watch <add|remove|update|list|analyze> [options]\n\n");
    printf("  add <owner/repo> [--alias <name>] [--days <n>] [--format <fmt>]\n");
    printf("      [--platform <github|gitlab>]   Subscribe to a repository\n");
    printf("  remove <ref>                       Unsubscribe (by repo or alias)\n");
    printf("  update <ref> [--repo <owner/repo>] [--alias <name>|-] [--days <n>]\n");
    printf("      [--format <fmt>] [--platform <p>]\n");
    printf("  list [--json]                      List subscriptions\n");
    printf("  analyze                            Analyze all subscriptions (default)\n");
    printf("  -d, --since <days>  -f, --format <fmt>  -q, --quiet  --no-cache  --llm <name>\n");
    printf("  -o, --output <path>  --color <mode>\n");
}

static int parse_color(const char *s) {
    if (strcmp(s, "always") == 0) return 1;
    if (strcmp(s, "never") == 0)  return 2;
    return 0;
}

ErrorCode cli_parse(int argc, char **argv, Options *opts) {
    memset(opts, 0, sizeof(Options));
    opts->since_days = -1;
    opts->color_mode = -1;

    if (argc < 2) {
        cli_print_usage();
        return ERR_INVALID_ARGS;
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        opts->command = 2;
        return GS_OK;
    }

    if (strcmp(argv[1], "completion") == 0 || strcmp(argv[1], "completions") == 0) {
        opts->command = 4;
        if (argc > 2)
            snprintf(opts->completion_shell, sizeof(opts->completion_shell), "%s", argv[2]);
        return GS_OK;
    }

    if (strcmp(argv[1], "watch") == 0 || strcmp(argv[1], "sub") == 0 ||
        strcmp(argv[1], "subscribe") == 0)
    {
        opts->command = 3;
        opts->watch_days = 0;
        opts->watch_days_set = 0;

        int i = 2;
        while (i < argc && strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            snprintf(opts->config_path, sizeof(opts->config_path), "%s", argv[i + 1]);
            i += 2;
        }

        opts->watch_cmd = 4; /* default: analyze all subscriptions */
        if (i < argc && argv[i][0] != '-') {
            const char *sub = argv[i];
            if (strcmp(sub, "add") == 0)              opts->watch_cmd = 0;
            else if (strcmp(sub, "remove") == 0 ||
                     strcmp(sub, "rm") == 0 ||
                     strcmp(sub, "del") == 0)         opts->watch_cmd = 1;
            else if (strcmp(sub, "update") == 0)      opts->watch_cmd = 2;
            else if (strcmp(sub, "list") == 0)        opts->watch_cmd = 3;
            else if (strcmp(sub, "analyze") == 0 ||
                     strcmp(sub, "run") == 0)         opts->watch_cmd = 4;
            else {
                cli_print_watch_usage();
                return ERR_INVALID_ARGS;
            }
            i++;
        }

        for (; i < argc; i++) {
            if (strcmp(argv[i], "--alias") == 0 && i + 1 < argc) {
                snprintf(opts->watch_alias, sizeof(opts->watch_alias), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--repo") == 0 && i + 1 < argc) {
                snprintf(opts->watch_new_repo, sizeof(opts->watch_new_repo), "%s", argv[++i]);
            } else if ((strcmp(argv[i], "--days") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
                opts->watch_days = atoi(argv[++i]);
                opts->watch_days_set = 1;
            } else if ((strcmp(argv[i], "--format") == 0 || strcmp(argv[i], "-f") == 0) && i + 1 < argc) {
                snprintf(opts->watch_format, sizeof(opts->watch_format), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--platform") == 0 && i + 1 < argc) {
                snprintf(opts->watch_platform, sizeof(opts->watch_platform), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--json") == 0) {
                opts->watch_json = 1;
            } else if (strcmp(argv[i], "--no-cache") == 0) {
                opts->no_cache = 1;
            } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
                opts->quiet = 1;
            } else if (strcmp(argv[i], "--llm") == 0 && i + 1 < argc) {
                snprintf(opts->llm_name, sizeof(opts->llm_name), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) snprintf(opts->output, sizeof(opts->output), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
                opts->color_mode = parse_color(argv[++i]);
            } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                snprintf(opts->config_path, sizeof(opts->config_path), "%s", argv[++i]);
            } else if (argv[i][0] != '-' && !opts->watch_ref[0]) {
                snprintf(opts->watch_ref, sizeof(opts->watch_ref), "%s", argv[i]);
            }
        }

        if (opts->watch_cmd <= 2 && !opts->watch_ref[0]) {
            printf("Error: %s requires a repository or alias argument.\n\n",
                   opts->watch_cmd == 0 ? "watch add" : "watch remove/update");
            cli_print_watch_usage();
            return ERR_INVALID_ARGS;
        }
        return GS_OK;
    }

    if (strcmp(argv[1], "config") == 0) {
        opts->command = 1;
        for (int i = 2; i < argc; i++) {            if (strcmp(argv[i], "--set-token") == 0 || strcmp(argv[i], "-t") == 0) {
                if (i + 1 < argc) {
                    opts->set_token = 1;
                    snprintf(opts->set_token_value, sizeof(opts->set_token_value), "%s", argv[++i]);
                }
            } else if (strcmp(argv[i], "--set-llm") == 0) {
                if (i + 1 < argc) {
                    opts->set_llm = 1;
                    snprintf(opts->set_llm_value, sizeof(opts->set_llm_value), "%s", argv[++i]);
                }
            } else if (strcmp(argv[i], "--list-llms") == 0) {
                opts->list_llms = 1;
            } else if (strcmp(argv[i], "--show") == 0) {
                opts->show_config = 1;
            } else if (strcmp(argv[i], "--edit") == 0) {
                opts->edit_config = 1;
            } else if (strcmp(argv[i], "--set") == 0) {
                if (i + 1 < argc) {
                    const char *kv = argv[++i];
                    const char *eq = strchr(kv, '=');
                    if (eq && (size_t)(eq - kv) < sizeof(opts->set_key)) {
                        opts->set_config = 1;
                        memcpy(opts->set_key, kv, (size_t)(eq - kv));
                        opts->set_key[eq - kv] = '\0';
                        snprintf(opts->set_value, sizeof(opts->set_value), "%s", eq + 1);
                    }
                }
            } else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
                if (i + 1 < argc)
                    snprintf(opts->config_path, sizeof(opts->config_path), "%s", argv[++i]);
            }
        }
        return GS_OK;
    }

    if (strcmp(argv[1], "analyze") != 0) {
        if (argv[1][0] != '-') {
            cli_print_usage();
            return ERR_INVALID_ARGS;
        }
    }

    int analyze_idx = (strcmp(argv[1], "analyze") == 0) ? 1 : 0;
    int next_arg = analyze_idx ? 2 : 1;

    /* Collect leading positional args: `analyze repo1 repo2 [opts]` */
    int nrepos = 0;
    while (next_arg < argc && argv[next_arg][0] != '-') {
        if (nrepos < 16)
            snprintf(opts->repos[nrepos], sizeof(opts->repos[0]), "%s", argv[next_arg]);
        nrepos++;
        next_arg++;
    }
    if (nrepos > 16) nrepos = 16;

    static struct option long_opts[] = {
        {"since",    required_argument, 0, 'd'},
        {"token",    required_argument, 0, 't'},
        {"format",   required_argument, 0, 'f'},
        {"output",   required_argument, 0, 'o'},
        {"help",     no_argument,       0, 'h'},
        {"color",    required_argument, 0, 256},
        {"proxy",    required_argument, 0, 257},
        {"api-base", required_argument, 0, 258},
        {"config",   required_argument, 0, 259},
        {"llm",      required_argument, 0, 260},
        {"export",   required_argument, 0, 261},
        {"platform", required_argument, 0, 262},
        {"max-pages", required_argument, 0, 263},
        {"no-cache",  no_argument,       0, 264},
        {"quiet",     no_argument,       0, 'q'},
        {"compare",   required_argument, 0, 265},
        {"llm-mode",  required_argument, 0, 266},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_idx = 0;
    optind = next_arg;
    while ((opt = getopt_long(argc, argv, "d:t:f:o:hq", long_opts, &opt_idx)) != -1) {
        switch (opt) {
            case 'd': opts->since_days = atoi(optarg); break;
            case 't': snprintf(opts->token, sizeof(opts->token), "%s", optarg); break;
            case 'f': snprintf(opts->format, sizeof(opts->format), "%s", optarg); break;
            case 'o': snprintf(opts->output, sizeof(opts->output), "%s", optarg); break;
            case 'q': opts->quiet = 1; break;
            case 'h': cli_print_usage(); return 1;
            case 256: opts->color_mode = parse_color(optarg); break;
            case 257: snprintf(opts->proxy, sizeof(opts->proxy), "%s", optarg); break;
            case 258: snprintf(opts->api_base, sizeof(opts->api_base), "%s", optarg); break;
            case 259: snprintf(opts->config_path, sizeof(opts->config_path), "%s", optarg); break;
            case 260: snprintf(opts->llm_name, sizeof(opts->llm_name), "%s", optarg); break;
            case 261:
                if (opts->export_extra[0])
                    strncat(opts->export_extra, "\n", sizeof(opts->export_extra) - strlen(opts->export_extra) - 1);
                strncat(opts->export_extra, optarg, sizeof(opts->export_extra) - strlen(opts->export_extra) - 1);
                break;
            case 262: snprintf(opts->platform, sizeof(opts->platform), "%s", optarg); break;
            case 263: opts->max_pages = atoi(optarg); break;
            case 264: opts->no_cache = 1; break;
            case 265: snprintf(opts->compare, sizeof(opts->compare), "%s", optarg); break;
            case 266: snprintf(opts->llm_mode, sizeof(opts->llm_mode), "%s", optarg); break;
            default: return ERR_INVALID_ARGS;
        }
    }

    /* Collect repos permuted to the end: `analyze [opts] repo1 repo2` */
    for (int i = optind; i < argc && nrepos < 16; i++) {
        if (argv[i][0] != '-')
            snprintf(opts->repos[nrepos], sizeof(opts->repos[0]), "%s", argv[i]);
        nrepos++;
    }

    opts->repo_count = nrepos;
    if (opts->repo_count > 0)
        snprintf(opts->owner_repo, sizeof(opts->owner_repo), "%s", opts->repos[0]);

    if (opts->repo_count == 0) {
        printf("Error: missing <owner/repo> argument\n\n");
        cli_print_usage();
        return ERR_INVALID_ARGS;
    }

    return GS_OK;
}
