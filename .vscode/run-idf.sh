#!/usr/bin/env bash
set -euo pipefail

idf_path="${IDF_PATH:-}"

if [[ -z "$idf_path" || ! -f "$idf_path/export.sh" ]]; then
    for candidate in "${HOME:-}"/.espressif/*/esp-idf; do
        if [[ -f "$candidate/export.sh" ]]; then
            idf_path="$candidate"
            break
        fi
    done
fi

if [[ -z "$idf_path" || ! -f "$idf_path/export.sh" ]]; then
    printf '%s\n' "ESP-IDF not found. Set IDF_PATH or install ESP-IDF in ~/.espressif." >&2
    exit 1
fi

if [[ -d "${HOME:-}/.espressif/python_env" ]]; then
    for python_bin in "${HOME}"/.espressif/python_env/*/bin; do
        [[ -d "$python_bin" ]] && PATH="$python_bin:$PATH"
    done
    export PATH
fi

export IDF_PATH="$idf_path"
source "$IDF_PATH/export.sh"
exec idf.py "$@"
