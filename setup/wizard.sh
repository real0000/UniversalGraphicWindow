#!/usr/bin/env bash
#
# Project wizard for UniversalGraphicWindow (Linux / macOS / *BSD).
#
# Creates a new cross-platform CMake project that consumes UGW either by
# referencing this checkout in-place or by adding it as a git submodule
# under <project>/UGW.
#
# Usage:
#   ./setup/wizard.sh
#   ./setup/wizard.sh --name MyApp --path ~/src/MyApp --mode submodule
#

set -euo pipefail

PROJECT_NAME=""
PROJECT_PATH=""
MODE=""
REPO_URL=""
FORCE=0

usage() {
    cat <<EOF
Usage: $0 [options]
  --name <name>          project name (C identifier)
  --path <dir>           where to create the project
  --mode <reference|submodule>
                         how to consume UGW
  --repo-url <url>       git URL when --mode submodule (auto-detected from origin if omitted)
  --force                allow non-empty target directory and overwrite UGW/
  -h, --help             show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name)     PROJECT_NAME="$2"; shift 2 ;;
        --path)     PROJECT_PATH="$2"; shift 2 ;;
        --mode)     MODE="$2"; shift 2 ;;
        --repo-url) REPO_URL="$2"; shift 2 ;;
        --force)    FORCE=1; shift ;;
        -h|--help)  usage; exit 0 ;;
        *)          echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UGW_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATES="$SCRIPT_DIR/templates"

ask_default() {
    # ask_default <prompt> [default]
    local prompt="$1"; local default="${2-}"; local reply
    if [[ -n "$default" ]]; then
        read -r -p "$prompt [$default]: " reply || true
    else
        read -r -p "$prompt: " reply || true
    fi
    if [[ -z "${reply:-}" ]]; then
        printf '%s' "$default"
    else
        printf '%s' "$reply"
    fi
}

ask_choice() {
    # ask_choice <prompt> <default> <opt1> <opt2> ...
    local prompt="$1"; shift
    local default="$1"; shift
    local options=("$@")
    local joined; joined="$(IFS=/; echo "${options[*]}")"
    while true; do
        local reply; reply="$(ask_default "$prompt ($joined)" "$default")"
        for o in "${options[@]}"; do
            [[ "$reply" == "$o" ]] && { printf '%s' "$reply"; return; }
        done
        echo "Please choose one of: ${options[*]}" >&2
    done
}

expand_template() {
    # expand_template <src> <dst> <var1=val1> <var2=val2> ...
    local src="$1"; local dst="$2"; shift 2
    local dir; dir="$(dirname "$dst")"
    mkdir -p "$dir"
    local content; content="$(<"$src")"
    local pair key val
    for pair in "$@"; do
        key="${pair%%=*}"
        val="${pair#*=}"
        # Literal substring replacement — no regex, no metachar surprises.
        content="${content//@${key}@/${val}}"
    done
    printf '%s\n' "$content" > "$dst"
}

detect_origin() {
    git -C "$UGW_ROOT" remote get-url origin 2>/dev/null || true
}

is_c_identifier() {
    [[ "$1" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]
}

# ---------------------------------------------------------------------------
echo "UniversalGraphicWindow project wizard"
echo "  UGW source: $UGW_ROOT"
echo

[[ -z "$PROJECT_NAME" ]] && PROJECT_NAME="$(ask_default 'Project name' 'MyApp')"
if ! is_c_identifier "$PROJECT_NAME"; then
    echo "Project name '$PROJECT_NAME' is not a valid C identifier." >&2
    exit 1
fi

if [[ -z "$PROJECT_PATH" ]]; then
    PROJECT_PATH="$(ask_default 'Project path' "$PWD/$PROJECT_NAME")"
fi
# Normalise path (expand ~, then make absolute).
PROJECT_PATH="${PROJECT_PATH/#\~/$HOME}"
case "$PROJECT_PATH" in
    /*) ;;
    *)  PROJECT_PATH="$PWD/$PROJECT_PATH" ;;
esac

[[ -z "$MODE" ]] && MODE="$(ask_choice 'Integrate UGW as' 'reference' 'reference' 'submodule')"

if [[ "$MODE" == "submodule" && -z "$REPO_URL" ]]; then
    detected="$(detect_origin)"
    REPO_URL="$(ask_default 'UGW git URL' "$detected")"
    [[ -z "$REPO_URL" ]] && { echo 'A git URL is required for submodule mode.' >&2; exit 1; }
fi

# ---------------------------------------------------------------------------
if [[ -e "$PROJECT_PATH" ]]; then
    if [[ -d "$PROJECT_PATH" ]]; then
        if [[ "$FORCE" -eq 0 ]] && [[ -n "$(ls -A "$PROJECT_PATH" 2>/dev/null)" ]]; then
            echo "Target path '$PROJECT_PATH' is not empty (use --force to overwrite)." >&2
            exit 1
        fi
    else
        echo "Target path '$PROJECT_PATH' exists and is not a directory." >&2
        exit 1
    fi
else
    mkdir -p "$PROJECT_PATH"
fi

echo
echo "Creating project '$PROJECT_NAME' at $PROJECT_PATH ($MODE mode)..."

(
    cd "$PROJECT_PATH"
    if [[ ! -d .git ]]; then
        git init --quiet
    fi

    if [[ "$MODE" == "submodule" ]]; then
        if [[ -e UGW ]]; then
            if [[ "$FORCE" -eq 1 ]]; then rm -rf UGW
            else echo "Submodule directory '$PROJECT_PATH/UGW' already exists." >&2; exit 1
            fi
        fi
        git submodule add "$REPO_URL" UGW
        git submodule update --init --recursive
        UGW_PATH_FOR_CMAKE='${CMAKE_SOURCE_DIR}/UGW'
    else
        UGW_PATH_FOR_CMAKE="$UGW_ROOT"
    fi

    expand_template "$TEMPLATES/CMakeLists.txt.in" "$PROJECT_PATH/CMakeLists.txt" \
        "PROJECT_NAME=$PROJECT_NAME" \
        "UGW_PATH=$UGW_PATH_FOR_CMAKE" \
        "UGW_MODE=$MODE"

    expand_template "$TEMPLATES/main.cpp.in" "$PROJECT_PATH/src/main.cpp" \
        "PROJECT_NAME=$PROJECT_NAME" \
        "UGW_PATH=$UGW_PATH_FOR_CMAKE" \
        "UGW_MODE=$MODE"

    cat > "$PROJECT_PATH/.gitignore" <<'EOF'
build/
out/
.vs/
.vscode/
*.user
EOF
)

echo
echo "Done."
echo "  cd \"$PROJECT_PATH\""
echo "  cmake -S . -B build"
echo "  cmake --build build --config Release"
