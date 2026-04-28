#!/usr/bin/env bash
# Dependency installer for Re-BOOT Web on Debian/Ubuntu/Raspberry Pi OS
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERR]${NC}  $*"; exit 1; }

need_root() {
    [ "$EUID" -eq 0 ] || error "Run as root: sudo $0"
}

# ------------------------------------------------------------------ #
need_root

info "Updating package index..."
apt-get update -qq

info "Installing build tools..."
apt-get install -y --no-install-recommends \
    build-essential \
    curl \
    ca-certificates

info "Installing SQLite3 development library..."
apt-get install -y --no-install-recommends \
    libsqlite3-dev \
    sqlite3

info "Installing nginx..."
apt-get install -y --no-install-recommends nginx

# ------------------------------------------------------------------ #
info "All dependencies installed."
echo ""
echo "  Next steps:"
echo "  1. Build the server:       make"
echo "  2. Place re-boot binary:   cp /path/to/re-boot ."
echo "  3. Start the server:       ./reboot-web"
echo "  4. Configure nginx:        see README.md"
echo ""
