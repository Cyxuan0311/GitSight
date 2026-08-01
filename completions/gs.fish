# fish completion for GitSight (gs)
# Install via:  gs completion fish > ~/.config/fish/completions/gs.fish

function __gs_config_file
    if set -q GS_CONFIG
        echo $GS_CONFIG
    else if set -q XDG_CONFIG_HOME
        echo "$XDG_CONFIG_HOME/gitsight/config.json"
    else
        echo "$HOME/.config/gitsight/config.json"
    end
end

function __gs_subscriptions
    set -l cfg (__gs_config_file)
    test -f "$cfg"; or return 1
    awk '
        /"repo":/  { if (match($0, /"repo":[ \t]*"[^"]*"/)) { s = substr($0, RSTART, RLENGTH); gsub(/^"repo":[ \t]*"|"$/, "", s); print s } }
        /"alias":/ { if (match($0, /"alias":[ \t]*"[^"]*"/)) { s = substr($0, RSTART, RLENGTH); gsub(/^"alias":[ \t]*"|"$/, "", s); print s } }
    ' "$cfg"
end

function __gs_llm_names
    set -l cfg (__gs_config_file)
    test -f "$cfg"; or return 1
    awk '
        /^[ \t]*"llms"[ \t]*:/ { inllms = 1; next }
        inllms && match($0, /[^ \t]/) {
            indent = RSTART - 1;
            if (indent < 2) { inllms = 0; next; }
            if (indent == 2 && match($0, /"([^"]+)"/)) {
                print substr($0, RSTART + 1, RLENGTH - 2);
            }
        }
    ' "$cfg"
end

function __gs_config_keys
    echo platform github_token gitlab_token api_base proxy default_days max_pages color format active_llm
    echo export.show_prs export.show_commits export.show_issues export.show_summary export.show_top_labels export.show_releases export.show_contributors export.item_limit
    echo export.md.table_width export.txt.table_width
    echo export.xlsx.font_size export.xlsx.font_name export.xlsx.col_a_width export.xlsx.col_b_width export.xlsx.border export.xlsx.sheet_name
    echo export.pdf.margin export.pdf.line_height export.pdf.col2_padding export.pdf.title_color export.pdf.header_color export.pdf.label_color
end

function __gs_watch_ctx
    __fish_seen_subcommand_from watch
end

function __gs_watch_analyze_ctx
    __fish_seen_subcommand_from watch; and not __fish_seen_subcommand_from add remove rm del update list
end

function __gs_watch_add_ctx
    __fish_seen_subcommand_from watch; and __fish_seen_subcommand_from add
end

function __gs_watch_rm_ctx
    __fish_seen_subcommand_from watch; and __fish_seen_subcommand_from remove rm del
end

function __gs_watch_update_ctx
    __fish_seen_subcommand_from watch; and __fish_seen_subcommand_from update
end

function __gs_watch_list_ctx
    __fish_seen_subcommand_from watch; and __fish_seen_subcommand_from list
end

# top-level commands
complete -c gs -f -n '__fish_use_subcommand' -a analyze -d 'Analyze a repository'
complete -c gs -f -n '__fish_use_subcommand' -a watch -d 'Subscribe to repositories and view them'
complete -c gs -f -n '__fish_use_subcommand' -a sub -d 'Alias of watch'
complete -c gs -f -n '__fish_use_subcommand' -a subscribe -d 'Alias of watch'
complete -c gs -f -n '__fish_use_subcommand' -a config -d 'Show/edit configuration'
complete -c gs -f -n '__fish_use_subcommand' -a version -d 'Show version'
complete -c gs -f -n '__fish_use_subcommand' -a completion -d 'Print a shell completion script'

complete -c gs -f -n '__fish_seen_subcommand_from completion' -a 'bash zsh fish'

# analyze
complete -c gs -f -n '__fish_seen_subcommand_from analyze' -a '(__gs_subscriptions)' -d repository
complete -c gs -n '__fish_seen_subcommand_from analyze' -s d -l since -r -d 'analysis period in days'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l max-pages -r -d 'api pages to fetch'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l platform -xa 'github gitlab'
complete -c gs -n '__fish_seen_subcommand_from analyze' -s t -l token -r -d 'access token'
complete -c gs -n '__fish_seen_subcommand_from analyze' -s f -l format -xa 'json md txt xlsx pdf'
complete -c gs -n '__fish_seen_subcommand_from analyze' -s o -l output -r -F -d 'output file'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l proxy -r -d 'proxy url'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l api-base -r -d 'api base url'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l color -xa 'auto always never'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l llm -xa '(__gs_llm_names)' -d 'llm provider'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l llm-mode -xa 'health trends release'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l export -r -d 'export override key=value'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l no-cache -d 'bypass disk cache'
complete -c gs -n '__fish_seen_subcommand_from analyze' -s q -l quiet -d 'suppress progress output'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l compare -r -d 'compare periods a:b'
complete -c gs -n '__fish_seen_subcommand_from analyze' -l config -r -F -d 'config file'
complete -c gs -n '__fish_seen_subcommand_from analyze' -s h -l help -d 'show help'

# watch subcommands
complete -c gs -f -n '__gs_watch_ctx; and __fish_use_subcommand' -a 'add remove rm del update list analyze run'

# watch analyze (bare watch, analyze, run)
complete -c gs -n '__gs_watch_analyze_ctx' -s d -l since -r -d 'analysis period in days'
complete -c gs -n '__gs_watch_analyze_ctx' -l platform -xa 'github gitlab'
complete -c gs -n '__gs_watch_analyze_ctx' -s f -l format -xa 'json md txt xlsx pdf'
complete -c gs -n '__gs_watch_analyze_ctx' -s o -l output -r -F -d 'output file'
complete -c gs -n '__gs_watch_analyze_ctx' -l color -xa 'auto always never'
complete -c gs -n '__gs_watch_analyze_ctx' -l llm -xa '(__gs_llm_names)' -d 'llm provider'
complete -c gs -n '__gs_watch_analyze_ctx' -l llm-mode -xa 'health trends release'
complete -c gs -n '__gs_watch_analyze_ctx' -l no-cache -d 'bypass disk cache'
complete -c gs -n '__gs_watch_analyze_ctx' -s q -l quiet -d 'suppress progress output'
complete -c gs -n '__gs_watch_analyze_ctx' -l config -r -F -d 'config file'

# watch add
complete -c gs -f -n '__gs_watch_add_ctx' -a '(__gs_subscriptions)' -d repository
complete -c gs -n '__gs_watch_add_ctx' -l alias -r -d 'alias'
complete -c gs -n '__gs_watch_add_ctx' -s d -l days -r -d 'days'
complete -c gs -n '__gs_watch_add_ctx' -s f -l format -xa 'json md txt xlsx pdf'
complete -c gs -n '__gs_watch_add_ctx' -l platform -xa 'github gitlab'
complete -c gs -n '__gs_watch_add_ctx' -l config -r -F -d 'config file'

# watch remove/rm/del
complete -c gs -f -n '__gs_watch_rm_ctx' -a '(__gs_subscriptions)' -d subscription
complete -c gs -n '__gs_watch_rm_ctx' -l config -r -F -d 'config file'

# watch update
complete -c gs -f -n '__gs_watch_update_ctx' -a '(__gs_subscriptions)' -d subscription
complete -c gs -n '__gs_watch_update_ctx' -l repo -r -d 'new repository'
complete -c gs -n '__gs_watch_update_ctx' -l alias -r -d 'alias'
complete -c gs -n '__gs_watch_update_ctx' -s d -l days -r -d 'days'
complete -c gs -n '__gs_watch_update_ctx' -s f -l format -xa 'json md txt xlsx pdf'
complete -c gs -n '__gs_watch_update_ctx' -l platform -xa 'github gitlab'
complete -c gs -n '__gs_watch_update_ctx' -l config -r -F -d 'config file'

# watch list
complete -c gs -n '__gs_watch_list_ctx' -l json -d 'json output'
complete -c gs -n '__gs_watch_list_ctx' -l config -r -F -d 'config file'

# config
complete -c gs -n '__fish_seen_subcommand_from config' -s t -l set-token -r -d 'set token'
complete -c gs -n '__fish_seen_subcommand_from config' -l set-llm -xa '(__gs_llm_names)' -d 'set active llm'
complete -c gs -n '__fish_seen_subcommand_from config' -l list-llms -d 'list llm providers'
complete -c gs -n '__fish_seen_subcommand_from config' -l show -d 'show config'
complete -c gs -n '__fish_seen_subcommand_from config' -l edit -d 'edit config'
complete -c gs -n '__fish_seen_subcommand_from config' -l set -r -xa '(__gs_config_keys)' -d 'set key=value'
complete -c gs -n '__fish_seen_subcommand_from config' -s c -l config -r -F -d 'config file'
complete -c gs -n '__fish_seen_subcommand_from config' -s h -l help -d 'show help'
