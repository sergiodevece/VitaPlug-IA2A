#!/bin/bash
set -euo pipefail
set +x

cd "$(dirname "$0")"
if /usr/bin/pgrep -x 'Logic Pro' >/dev/null 2>&1; then
    printf '%s\n' 'Guarda la sesión, sal de Logic Pro y vuelve a abrir este archivo.'
    exit 1
fi

if ! command -v node >/dev/null; then
    printf '%s\n' 'El servicio necesita Node 22.18 o posterior en Terminal.'
    exit 1
fi

if [[ -z "${ANTHROPIC_API_KEY:-}" ]]; then
    printf 'Pega tu clave API de Claude y pulsa Intro (entrada oculta): '
    IFS= read -rs ANTHROPIC_API_KEY
    printf '\n'
fi
if [[ -z "$ANTHROPIC_API_KEY" ]]; then
    printf '%s\n' 'Falta la clave API de Claude.'
    exit 1
fi

export ANTHROPIC_API_KEY
export IA2A_PROVIDER=claude
printf '%s\n' 'Deja Terminal abierta mientras uses Claude en IA2A. Ctrl+C detiene el servicio.'
exec node src/server.ts --open-logic
