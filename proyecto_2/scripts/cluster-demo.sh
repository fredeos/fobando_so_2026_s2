#!/usr/bin/env bash
set -euo pipefail
: "${DIPS_CLUSTER_SERVER:?Run make cluster-demo}"
: "${DIPS_CLUSTER_CLIENT:?Run make cluster-demo}"
if [[ ! -f "$DIPS_TCP_IMAGE" || ! -r "$DIPS_TCP_IMAGE" ]]; then
    printf 'Imagen inexistente/no legible: %s\n' "$DIPS_TCP_IMAGE" >&2
    exit 1
fi
if [[ ! -v DIPS_AES_KEY ]]; then
    DIPS_AES_KEY="$(openssl rand -hex 32)"
    export DIPS_AES_KEY
fi
mkdir -p -- "$(dirname -- "$DIPS_CLUSTER_RESULT")"
args=(--oversubscribe -np "$DIPS_CLUSTER_NP" --map-by slot -x DIPS_AES_KEY)
if [[ -n "$DIPS_CLUSTER_HOSTS" ]]; then args+=(--host "$DIPS_CLUSTER_HOSTS"); fi
coproc CLUSTER { exec "$DIPS_CLUSTER_MPIRUN" "${args[@]}" "$DIPS_CLUSTER_SERVER" "$DIPS_TCP_PORT" --once --verify; }
pid=${CLUSTER_PID:-}
if [[ -z "$pid" ]]; then exit 1; fi
drain_pid=''
cleanup() {
    kill "$pid" 2>/dev/null || :
    wait "$pid" 2>/dev/null || :
    if [[ -n "$drain_pid" ]]; then wait "$drain_pid" 2>/dev/null || :; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
exec {server_output}<&"${CLUSTER[0]}"
ready=false
while IFS= read -r line <&"$server_output"; do
    printf '%s\n' "$line"
    if [[ "$line" == 'Servidor TCP IPv4 escuchando'* ]]; then ready=true; break; fi
done
if [[ "$ready" != true ]]; then printf 'El cluster no pudo iniciar.\n' >&2; exit 1; fi
# Drain while the client runs so verbose MPI jobs cannot fill the output pipe.
cat <&"$server_output" &
drain_pid=$!
"$DIPS_CLUSTER_CLIENT" 127.0.0.1 "$DIPS_TCP_PORT" "$DIPS_TCP_IMAGE" \
    --filter "$DIPS_CLUSTER_FILTER" --resize "$DIPS_CLUSTER_RESIZE" \
    --block-rows "$DIPS_CLUSTER_ROWS" --output "$DIPS_CLUSTER_RESULT"
wait "$pid"
wait "$drain_pid"
drain_pid=''
exec {server_output}<&-
trap - EXIT
