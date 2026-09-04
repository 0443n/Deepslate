#!/usr/bin/env bash
# Deploy Deepslate to a PSP memory stick.
#
#   tools/deploy.sh                  build if stale, sync EBOOT + data, keep a rollback
#   tools/deploy.sh -n "note"        record a note in version.txt
#   tools/deploy.sh --eboot-only     skip the data/ sync (the usual case)
#   tools/deploy.sh --rollback       put EBOOT.prev.PBP back
#   tools/deploy.sh --trace          pull the trace log and stop
#   tools/deploy.sh --eject          unmount when finished
#
# Finds the card by looking for PSP/GAME on a removable device, and mounts it
# through udisks if it is not mounted yet.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="PSP/GAME/MCPSP"
LOG_DIR="$ROOT/logs"

NOTE=""
EBOOT_ONLY=0
ROLLBACK=0
TRACE_ONLY=0
EJECT=0
NO_BUILD=0

die()  { printf '\033[31merror\033[0m  %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m..\033[0m %s\n' "$*"; }
ok()   { printf '\033[32mok\033[0m %s\n' "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--note)     [ $# -ge 2 ] || die "--note needs a value"; NOTE="$2"; shift 2 ;;
        --eboot-only)  EBOOT_ONLY=1; shift ;;
        --rollback)    ROLLBACK=1; shift ;;
        --trace)       TRACE_ONLY=1; shift ;;
        --eject)       EJECT=1; shift ;;
        --no-build)    NO_BUILD=1; shift ;;
        -h|--help)     sed -n '2,16p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *)             die "unknown option $1" ;;
    esac
done

# --- find the card ---------------------------------------------------------

find_card() {
    local dev mnt
    for dev in $(lsblk -rno NAME,RM,TYPE | awk '$2=="1" && $3=="part" {print $1}'); do
        mnt=$(findmnt -nfo TARGET "/dev/$dev" 2>/dev/null || true)
        if [ -z "$mnt" ]; then
            udisksctl mount -b "/dev/$dev" >/dev/null 2>&1 || continue
            mnt=$(findmnt -nfo TARGET "/dev/$dev" 2>/dev/null || true)
        fi
        [ -n "$mnt" ] && [ -d "$mnt/PSP/GAME" ] && { printf '%s\t%s' "$mnt" "/dev/$dev"; return 0; }
    done
    return 1
}

CARD_INFO=$(find_card) || die "no PSP memory stick found - is it plugged in and unlocked?"
CARD=${CARD_INFO%%$'\t'*}
CARD_DEV=${CARD_INFO##*$'\t'}
DEST="$CARD/$GAME_DIR"
ok "card at $CARD ($CARD_DEV)"

finish() {
    sync
    if [ "$EJECT" = 1 ]; then
        udisksctl unmount -b "$CARD_DEV" >/dev/null && ok "unmounted, safe to pull the card"
    fi
}

# --- pull the trace log ----------------------------------------------------

pull_trace() {
    local src="$DEST/deepslate_trace.txt"
    [ -f "$src" ] || { info "no trace log on the card"; return 0; }
    mkdir -p "$LOG_DIR"
    local stamp dst
    stamp=$(date -r "$src" +%Y%m%d-%H%M%S)
    dst="$LOG_DIR/trace-$stamp.txt"
    [ -f "$dst" ] || cp "$src" "$dst"
    ok "trace -> logs/$(basename "$dst") ($(wc -l < "$dst") lines)"
}

if [ "$TRACE_ONLY" = 1 ]; then
    pull_trace
    finish
    exit 0
fi

# --- rollback --------------------------------------------------------------

if [ "$ROLLBACK" = 1 ]; then
    [ -f "$DEST/EBOOT.prev.PBP" ] || die "no EBOOT.prev.PBP to roll back to"
    cp "$DEST/EBOOT.prev.PBP" "$DEST/EBOOT.PBP"
    ok "rolled back to the previous EBOOT"
    finish
    exit 0
fi

# --- build -----------------------------------------------------------------

cd "$ROOT"
if [ "$NO_BUILD" = 0 ]; then
    info "make"
    # A failed make leaves the old EBOOT in place, and a stale deploy is worse
    # than none, so capture the log and check the status before filtering it.
    BUILD_LOG=$(mktemp)
    if ! make -j"$(nproc)" >"$BUILD_LOG" 2>&1; then
        tail -25 "$BUILD_LOG" >&2
        rm -f "$BUILD_LOG"
        die "build failed"
    fi
    grep -E 'warning|error' "$BUILD_LOG" | tail -5 || true
    rm -f "$BUILD_LOG"
fi
[ -f EBOOT.PBP ] || die "no EBOOT.PBP - run make first"

# --- deploy ----------------------------------------------------------------

pull_trace
mkdir -p "$DEST"

if [ -f "$DEST/EBOOT.PBP" ]; then
    if cmp -s EBOOT.PBP "$DEST/EBOOT.PBP"; then
        info "EBOOT is already the one on the card"
    else
        cp "$DEST/EBOOT.PBP" "$DEST/EBOOT.prev.PBP"
    fi
fi
cp EBOOT.PBP "$DEST/EBOOT.PBP"
ok "EBOOT.PBP  $(du -h EBOOT.PBP | cut -f1)"

if [ "$EBOOT_ONLY" = 0 ]; then
    rsync -rt --delete \
          --exclude 'sound/aac' --exclude 'sound/cave' --exclude 'sound/extra' \
          data/ "$DEST/data/"
    ok "data/  $(du -sh "$DEST/data" | cut -f1)"
fi

# --- stamp -----------------------------------------------------------------

DIRTY=""
git -C "$ROOT" diff --quiet 2>/dev/null || DIRTY=" + uncommitted"
{
    echo "Deepslate"
    echo "commit:  $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)$DIRTY"
    echo "built:   $(date -u '+%Y-%m-%d %H:%M UTC')"
    [ -z "$NOTE" ] || echo "note:    $NOTE"
} > "$DEST/version.txt"

finish
ok "deployed to ms0:/$GAME_DIR"
