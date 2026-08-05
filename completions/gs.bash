# bash completion for GitSight (gs)
# Source this file, or install via:  gs completion bash > ~/.local/share/bash-completion/completions/gs

_gs_config_file() {
    local i cfg=""
    for ((i = 1; i < COMP_CWORD; i++)); do
        case "${COMP_WORDS[i]}" in
            --config|-c)
                if ((i + 1 < COMP_CWORD)); then
                    cfg="${COMP_WORDS[i + 1]}"
                    break
                fi
                ;;
        esac
    done
    if [[ -z $cfg ]]; then
        cfg="${GS_CONFIG:-${XDG_CONFIG_HOME:-$HOME/.config}/gitsight/config.json}"
    fi
    printf '%s\n' "$cfg"
}

_gs_subscriptions() {
    local cfg
    cfg=$(_gs_config_file)
    [[ -f $cfg ]] || return 0
    awk '
        /"repo":/  { if (match($0, /"repo":[ \t]*"[^"]*"/)) { s = substr($0, RSTART, RLENGTH); gsub(/^"repo":[ \t]*"|"$/, "", s); print s } }
        /"alias":/ { if (match($0, /"alias":[ \t]*"[^"]*"/)) { s = substr($0, RSTART, RLENGTH); gsub(/^"alias":[ \t]*"|"$/, "", s); print s } }
    ' "$cfg"
}

_gs_llm_names() {
    local cfg
    cfg=$(_gs_config_file)
    [[ -f $cfg ]] || return 0
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
}

_gs_filedir() {
    if declare -F _filedir >/dev/null; then
        _filedir
    else
        COMPREPLY=( $(compgen -f -- "${COMP_WORDS[COMP_CWORD]}") )
    fi
}

_gs_config() {
    local cur="$1" prev="$2"
    local config_opts="--set-token -t --set-llm --list-llms --show --edit --set --config -c --help -h"
    local config_keys="platform github_token gitlab_token codeberg_token api_base proxy default_days max_pages color format active_llm export.show_prs export.show_commits export.show_issues export.show_summary export.show_top_labels export.show_releases export.show_contributors export.item_limit export.md.table_width export.txt.table_width export.xlsx.font_size export.xlsx.font_name export.xlsx.col_a_width export.xlsx.col_b_width export.xlsx.border export.xlsx.sheet_name export.pdf.margin export.pdf.line_height export.pdf.col2_padding export.pdf.title_color export.pdf.header_color export.pdf.label_color"

    if [[ $cur == *=* ]]; then
        local key="${cur%%=*}"
        local val="${cur#*=}"
        case "$key" in
            platform) COMPREPLY=( $(compgen -W "github gitlab codeberg gitea forgejo" -P "$key=" -- "$val") ) ;;
            color)    COMPREPLY=( $(compgen -W "auto always never" -P "$key=" -- "$val") ) ;;
            format)   COMPREPLY=( $(compgen -W "json md txt xlsx pdf" -P "$key=" -- "$val") ) ;;
            *.protocol) COMPREPLY=( $(compgen -W "openai anthropic gemini" -P "$key=" -- "$val") ) ;;
            export.show_prs|export.show_commits|export.show_issues|export.show_summary|export.show_top_labels|export.show_releases|export.show_contributors|export.xlsx.border)
                      COMPREPLY=( $(compgen -W "true false" -P "$key=" -- "$val") ) ;;
        esac
        return 0
    fi

    case "$prev" in
        --set)
            local keys="$config_keys" name
            for name in $(_gs_llm_names); do
                keys="$keys llms.$name.api_key llms.$name.api_base llms.$name.model llms.$name.protocol"
            done
            COMPREPLY=( $(compgen -W "$keys" -S '=' -- "$cur") )
            return 0
            ;;
        --set-llm)
            COMPREPLY=( $(compgen -W "$(_gs_llm_names)" -- "$cur") )
            return 0
            ;;
    esac

    if [[ $cur == -* ]]; then
        COMPREPLY=( $(compgen -W "$config_opts" -- "$cur") )
    fi
    return 0
}

_gs_watch() {
    local cur="$1" prev="$2" i wcmd=""
    local watch_sub="add remove rm del update list analyze run"
    for ((i = 2; i < COMP_CWORD; i++)); do
        case "${COMP_WORDS[i]}" in
            --config|-c) ((i++)) ;;
            -*) ;;
            *) wcmd="${COMP_WORDS[i]}"; break ;;
        esac
    done

    if [[ -z $wcmd ]]; then
        if [[ $cur == -* ]]; then
            COMPREPLY=( $(compgen -W "--since -d --max-pages --platform --branch --token -t --format -f --output -o --proxy --api-base --color --llm --llm-mode --export --no-cache --quiet -q --compare --config --help -h" -- "$cur") )
        else
            COMPREPLY=( $(compgen -W "$watch_sub" -- "$cur") )
        fi
        return 0
    fi

    case "$wcmd" in
        add)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "--alias --days -d --format -f --platform --branch --config --help -h" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_gs_subscriptions)" -- "$cur") )
            fi
            ;;
        remove|rm|del)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "--config --help -h" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_gs_subscriptions)" -- "$cur") )
            fi
            ;;
        update)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "--repo --alias --days -d --format -f --platform --branch --config --help -h" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_gs_subscriptions)" -- "$cur") )
            fi
            ;;
        list)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "--json --config --help -h" -- "$cur") )
            fi
            ;;
        analyze|run)
            if [[ $cur == -* ]]; then
COMPREPLY=( $(compgen -W "--since -d --max-pages --platform --branch --token -t --format -f --output -o --proxy --api-base --color --llm --llm-mode --export --no-cache --quiet -q --compare --config --help -h" -- "$cur") )
            fi
            ;;
    esac
    return 0
}

_gs() {
    local cur prev cword
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"

    local commands="analyze watch sub subscribe config usage version completion"
    local formats="json md txt xlsx pdf"
    local platforms="github gitlab codeberg gitea forgejo"
    local colors="auto always never"
    local llm_modes="health trends release"
    local analyze_opts="--since -d --max-pages --platform --branch --token -t --format -f --output -o --proxy --api-base --color --llm --llm-mode --export --no-cache --quiet -q --compare --config --help -h"

    if ((COMP_CWORD == 1)); then
        if [[ $cur == */* ]]; then
            COMPREPLY=( $(compgen -W "$commands" -- "$cur") $(compgen -W "$(_gs_subscriptions)" -- "$cur") )
        else
            COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
        fi
        return 0
    fi

    local cmd="${COMP_WORDS[1]}"

    case "$prev" in
        -f|--format)   COMPREPLY=( $(compgen -W "$formats" -- "$cur") ); return 0 ;;
        --platform)    COMPREPLY=( $(compgen -W "$platforms" -- "$cur") ); return 0 ;;
        --color)       COMPREPLY=( $(compgen -W "$colors" -- "$cur") ); return 0 ;;
        --llm-mode)    COMPREPLY=( $(compgen -W "$llm_modes" -- "$cur") ); return 0 ;;
        --llm)         COMPREPLY=( $(compgen -W "$(_gs_llm_names)" -- "$cur") ); return 0 ;;
        -o|--output|--config|-c) _gs_filedir; return 0 ;;
        --export)      COMPREPLY=( $(compgen -W "pdf.margin pdf.line_height pdf.col2_padding pdf.title_color pdf.header_color pdf.label_color xlsx.font_size xlsx.font_name xlsx.col_a_width xlsx.col_b_width xlsx.border xlsx.sheet_name md.table_width txt.table_width show_prs show_commits show_issues show_summary show_top_labels show_releases show_contributors item_limit" -S '=' -- "$cur") ); return 0 ;;
        --token|-t|--since|-d|--max-pages|--compare|--proxy|--api-base|--alias|--repo|--days|--branch)
            return 0 ;;
    esac

    case "$cmd" in
        analyze)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "$analyze_opts" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_gs_subscriptions)" -- "$cur") )
            fi
            return 0
            ;;
        watch|sub|subscribe)
            _gs_watch "$cur" "$prev"
            return 0
            ;;
        config)
            _gs_config "$cur" "$prev"
            return 0
            ;;
        usage)
            if [[ $cur == -* ]]; then
                COMPREPLY=( $(compgen -W "--days --by-llm --json --clear --config --color" -- "$cur") )
            fi
            return 0
            ;;
        completion)
            if ((COMP_CWORD == 2)); then
                COMPREPLY=( $(compgen -W "bash zsh fish" -- "$cur") )
            fi
            return 0
            ;;
    esac
    return 0
}

complete -F _gs gs sub subscribe
