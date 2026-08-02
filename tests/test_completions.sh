#!/usr/bin/env bash
# Functional tests for the bash tab-completion embedded in the gs binary.
# Usage: bash tests/test_completions.sh /path/to/gs [source_dir]

set -u

GS="$1"
SRC="${2:-$(cd "$(dirname "$0")/.." && pwd)}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FAIL=0
fail() { echo "FAIL: $1" >&2; FAIL=1; }

bash_script="$TMP/gs.bash"
"$GS" completion bash > "$bash_script" || { echo "FAIL: gs completion bash"; exit 1; }
bash -n "$bash_script" || { echo "FAIL: bash -n"; exit 1; }

# Config with one subscription (repo + alias) and one LLM provider.
cat > "$TMP/config.json" <<EOF
{
	"llms":	{
		"local":	{
			"model":	"qwen"
		}
	},
	"subscriptions":	[{
			"repo":	"acme/gadget",
			"alias":	"core",
			"days":	14,
			"format":	"json"
		}]
}
EOF
export GS_CONFIG="$TMP/config.json"

source "$bash_script"

eval_gs() {
    local -a COMP_WORDS
    local COMP_CWORD COMPREPLY
    COMP_WORDS=("$@")
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    COMPREPLY=()
    _gs
    printf '%s\n' "${COMPREPLY[@]}"
}

check() {
    local desc="$1" expect="$2"
    shift 2
    local got
    got="$(eval_gs "$@")"
    if [[ "$got" != "$expect" ]]; then
        echo "FAIL: $desc" >&2
        echo "  expect: $expect" >&2
        echo "  got:    $got" >&2
        FAIL=1
    fi
}

check_contains() {
    local desc="$1" needle="$2"
    shift 2
    local got
    got="$(eval_gs "$@")"
    if ! grep -qF -- "$needle" <<<"$got"; then
        echo "FAIL: $desc (missing '$needle')" >&2
        echo "  got: $got" >&2
        FAIL=1
    fi
}

check "top-level commands" $'analyze\nwatch\nsub\nsubscribe\nconfig\nusage\nversion\ncompletion' gs ""
check "analyze format" $'json\nmd\ntxt\nxlsx\npdf' gs analyze -f ""
check "analyze platform" $'github\ngitlab\ncodeberg\ngitea\nforgejo' gs analyze --platform ""
check "analyze color" $'auto\nalways\nnever' gs analyze --color ""
check "analyze llm" $'local' gs analyze --llm ""
check "analyze repos" $'acme/gadget\ncore' gs analyze ""
check "watch subcommands" $'add\nremove\nrm\ndel\nupdate\nlist\nanalyze\nrun' gs watch ""
check "watch remove refs" $'acme/gadget\ncore' gs watch remove ""
check "watch update refs" $'acme/gadget\ncore' gs watch update ""
check "watch add repo" $'acme/gadget\ncore' gs watch add ""
check "watch list json flag" $'--json\n--config\n--help' gs watch list --
check_contains "config --set keys" "export.item_limit=" gs config --set ""
check_contains "config --set keys" "platform=" gs config --set ""
check "config --set platform value" $'platform=github\nplatform=gitlab\nplatform=codeberg\nplatform=gitea\nplatform=forgejo' gs config --set platform=
check "config --set-llm names" "local" gs config --set-llm ""
check_contains "config --set llms keys" "llms.local.protocol=" gs config --set ""
check "config --set llms.protocol values" $'llms.local.protocol=openai\nllms.local.protocol=anthropic\nllms.local.protocol=gemini' gs config --set llms.local.protocol=
check "completion shells" $'bash\nzsh\nfish' gs completion ""
check_contains "usage options" "--days" gs usage --

# Inline --config must take precedence over the default config path
cat > "$TMP/config2.json" <<EOF
{
	"subscriptions":	[{
			"repo":	"other/thing",
			"alias":	"ot"
		}]
}
EOF
check "--config inline refs" $'other/thing\not' gs watch --config "$TMP/config2.json" remove ""

if [[ $FAIL -eq 0 ]]; then
    echo "COMPLETIONS TEST OK"
    exit 0
fi
exit 1
