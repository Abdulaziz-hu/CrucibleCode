#!/usr/bin/env bash
# =============================================================================
#  CrucibleCode - one-shot setup for Linux and macOS
#  Installs the dependencies, builds the IDE and starts it.
#
#    ./scripts/setup.sh                 install + build (Release) + run
#    ./scripts/setup.sh --debug         Debug build
#    ./scripts/setup.sh --no-run        only install + build
#    ./scripts/setup.sh --clean         wipe the build folder first
#    ./scripts/setup.sh --with-dotnet   also install the .NET SDK (for C# projects)
#    ./scripts/setup.sh --use-aqt       get Qt from the official Qt servers (aqtinstall)
#    ./scripts/setup.sh --repo URL      clone this repository first (fork URL)
#    ./scripts/setup.sh -y              never ask questions
#
#  Run it as a NORMAL user: it uses sudo only for installing system packages.
#  Straight from a fork:
#    curl -fsSL https://raw.githubusercontent.com/<you>/cruciblecode/main/scripts/setup.sh | \
#         bash -s -- --repo https://github.com/<you>/cruciblecode.git
# =============================================================================
set -euo pipefail

REPO_URL="${CRUCIBLE_REPO:-}"
CONFIG="Release"
RUN=1; CLEAN=0; WITH_DOTNET=0; FORCE_AQT=0; ASSUME_YES=0
QT_VERSION="${CRUCIBLE_QT_VERSION:-6.7.3}"
MIN_QT="6.4.0"

c_red=$'\033[31m'; c_grn=$'\033[32m'; c_ylw=$'\033[33m'; c_blu=$'\033[36m'; c_off=$'\033[0m'
log()  { printf '%s==>%s %s\n' "$c_blu" "$c_off" "$*"; }
ok()   { printf '%s ok%s %s\n' "$c_grn" "$c_off" "$*"; }
warn() { printf '%s !!%s %s\n' "$c_ylw" "$c_off" "$*" >&2; }
die()  { printf '%s xx%s %s\n' "$c_red" "$c_off" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --debug) CONFIG=Debug ;;
    --release) CONFIG=Release ;;
    --no-run) RUN=0 ;;
    --clean) CLEAN=1 ;;
    --with-dotnet) WITH_DOTNET=1 ;;
    --use-aqt) FORCE_AQT=1 ;;
    --repo) shift; REPO_URL="${1:?--repo needs a URL}" ;;
    --qt-version) shift; QT_VERSION="${1:?--qt-version needs a value}" ;;
    -y|--yes) ASSUME_YES=1 ;;
    -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "Unknown option: $1 (try --help)" ;;
  esac
  shift
done

OS="$(uname -s)"
case "$OS" in Linux|Darwin) ;; *) die "Unsupported OS '$OS'. On Windows use scripts/setup.ps1 (or setup.cmd)." ;; esac

# --- privileges ---------------------------------------------------------------
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  have sudo && SUDO="sudo"
fi
if [ "$OS" = "Darwin" ] && [ "$(id -u)" -eq 0 ]; then
  die "On macOS do not run this script with sudo (Homebrew refuses to run as root). Run it as your normal user."
fi
# when started with sudo, build and run as the invoking user so files are not owned by root
as_user() {
  if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
    sudo -u "$SUDO_USER" -H "$@"
  else
    "$@"
  fi
}
need_root_cmd() {   # run a package-manager command with root rights
  if [ "$(id -u)" -eq 0 ]; then "$@"
  elif [ -n "$SUDO" ]; then $SUDO "$@"
  else die "Installing packages needs root, but 'sudo' is not available. Re-run as root."; fi
}

# --- locate / fetch the sources -----------------------------------------------
SCRIPT_PATH="${BASH_SOURCE[0]:-$0}"
if [ -f "$SCRIPT_PATH" ]; then SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"; else SCRIPT_DIR="$PWD"; fi
ROOT=""
if   [ -f "$SCRIPT_DIR/../CMakeLists.txt" ] && grep -q "CrucibleCode" "$SCRIPT_DIR/../CMakeLists.txt" 2>/dev/null; then ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [ -f "$PWD/CMakeLists.txt" ] && grep -q "CrucibleCode" "$PWD/CMakeLists.txt" 2>/dev/null; then ROOT="$PWD"
fi
if [ -z "$ROOT" ]; then
  [ -n "$REPO_URL" ] || die "Sources not found. Run this from a clone of the repository, or pass --repo <git url>."
  ROOT="$PWD/cruciblecode"
fi

# --- package installation -----------------------------------------------------
ver_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]; }   # ver_ge A B  => A >= B

linux_family() {
  [ -r /etc/os-release ] || { echo unknown; return; }
  # shellcheck disable=SC1091
  . /etc/os-release
  local ids=" ${ID:-} ${ID_LIKE:-} "
  case "$ids" in
    *" debian "*|*" ubuntu "*) echo debian ;;
    *" fedora "*|*" rhel "*|*" centos "*) echo fedora ;;
    *" arch "*) echo arch ;;
    *" suse "*|*" opensuse "*) echo suse ;;
    *" alpine "*) echo alpine ;;
    *) echo unknown ;;
  esac
}

system_qt_version() {
  if have pkg-config && pkg-config --exists Qt6Widgets 2>/dev/null; then pkg-config --modversion Qt6Widgets; return; fi
  for q in qmake6 qmake-qt6; do have "$q" && { "$q" -query QT_VERSION; return; }; done
  echo ""
}

install_linux_packages() {
  local fam; fam="$(linux_family)"
  log "Installing build dependencies (package family: $fam)"
  case "$fam" in
    debian)
      need_root_cmd apt-get update
      need_root_cmd apt-get install -y build-essential cmake ninja-build git pkg-config curl ca-certificates \
        qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev libxkbcommon-dev libvulkan-dev qt6-wayland
      need_root_cmd apt-get install -y libkf6syntaxhighlighting-dev || warn "KSyntaxHighlighting not available (optional)."
      if [ "$WITH_DOTNET" = 1 ]; then need_root_cmd apt-get install -y dotnet-sdk-8.0 || warn "Could not install dotnet-sdk-8.0 (see https://dot.net)."; fi
      ;;
    fedora)
      need_root_cmd dnf install -y gcc-c++ cmake ninja-build git pkgconf-pkg-config curl \
        qt6-qtbase-devel qt6-qtwayland mesa-libGL-devel libxkbcommon-devel vulkan-loader-devel
      need_root_cmd dnf install -y kf6-syntax-highlighting-devel || warn "KSyntaxHighlighting not available (optional)."
      if [ "$WITH_DOTNET" = 1 ]; then need_root_cmd dnf install -y dotnet-sdk-8.0 || warn "Could not install dotnet-sdk-8.0."; fi
      ;;
    arch)
      need_root_cmd pacman -Sy --needed --noconfirm base-devel cmake ninja git curl qt6-base qt6-wayland \
        mesa libxkbcommon vulkan-icd-loader
      need_root_cmd pacman -S --needed --noconfirm syntax-highlighting || warn "KSyntaxHighlighting not available (optional)."
      if [ "$WITH_DOTNET" = 1 ]; then need_root_cmd pacman -S --needed --noconfirm dotnet-sdk || warn "Could not install dotnet-sdk."; fi
      ;;
    suse)
      need_root_cmd zypper --non-interactive install -y gcc-c++ cmake ninja git curl pkg-config \
        qt6-base-devel qt6-wayland libxkbcommon-devel Mesa-libGL-devel vulkan-devel
      if [ "$WITH_DOTNET" = 1 ]; then need_root_cmd zypper --non-interactive install -y dotnet-sdk-8.0 || warn "Could not install dotnet-sdk-8.0."; fi
      ;;
    alpine)
      need_root_cmd apk add build-base cmake ninja git curl pkgconf qt6-qtbase-dev mesa-dev libxkbcommon-dev
      ;;
    *)
      warn "Unknown Linux distribution: install manually - a C++20 compiler, cmake (3.21+), ninja, git, Qt 6.4+ development packages (Core/Gui/Widgets), OpenGL and xkbcommon headers."
      ;;
  esac
}

install_macos_packages() {
  if ! xcode-select -p >/dev/null 2>&1; then
    log "Installing the Xcode command line tools (a system dialog will appear)"
    xcode-select --install || true
    until xcode-select -p >/dev/null 2>&1; do sleep 10; done
  fi
  if ! have brew; then
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$("$b" shellenv)"; done
  fi
  if ! have brew; then
    log "Installing Homebrew"
    NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$("$b" shellenv)"; done
  fi
  have brew || die "Homebrew is not available. Install it from https://brew.sh and re-run."
  log "Installing build dependencies with Homebrew"
  for pkg in cmake ninja git qt; do brew list --formula "$pkg" >/dev/null 2>&1 || brew install "$pkg"; done
  if [ "$WITH_DOTNET" = 1 ]; then brew list --cask dotnet-sdk >/dev/null 2>&1 || brew install --cask dotnet-sdk || warn "Could not install the .NET SDK."; fi
}

QT_PREFIX=""
install_qt_with_aqt() {
  log "Installing Qt $QT_VERSION with aqtinstall (official Qt servers, no account needed)"
  have python3 || die "python3 is required for aqtinstall."
  local venv="$HOME/.cache/cruciblecode/venv"
  as_user python3 -m venv "$venv" || die "python3 -m venv failed (install python3-venv)."
  as_user "$venv/bin/pip" install --quiet --upgrade pip aqtinstall
  local out="$HOME/Qt" dir=""
  if [ "$OS" = "Darwin" ]; then
    as_user "$venv/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 -O "$out" ; dir="$out/$QT_VERSION/macos"
    [ -d "$dir" ] || dir="$out/$QT_VERSION/clang_64"
  else
    as_user "$venv/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$out" \
      || as_user "$venv/bin/aqt" install-qt linux desktop "$QT_VERSION" gcc_64 -O "$out"
    dir="$out/$QT_VERSION/gcc_64"
    if [ "$(linux_family)" = "debian" ]; then   # runtime libraries the official Qt binaries expect
      need_root_cmd apt-get install -y libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-render-util0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 libegl1 libfontconfig1 || true
    fi
  fi
  [ -d "$dir" ] || die "aqtinstall finished but $dir does not exist."
  QT_PREFIX="$dir"
}

# --- 1. sources -----------------------------------------------------------------
if [ ! -f "$ROOT/CMakeLists.txt" ]; then
  if ! have git; then
    if [ "$OS" = Linux ]; then install_linux_packages; else install_macos_packages; fi
  fi
  log "Cloning $REPO_URL"
  as_user git clone --depth 1 "$REPO_URL" "$ROOT"
fi
cd "$ROOT"
ok "Sources: $ROOT"

# --- 2. dependencies -------------------------------------------------------------
missing=0
for t in cmake ninja git; do have "$t" || missing=1; done
have c++ || have g++ || have clang++ || missing=1
QTV="$(system_qt_version)"
if [ "$OS" = "Darwin" ]; then
  have brew && QTV="$(brew list --versions qt 2>/dev/null | awk '{print $2}' | head -n1)"
  [ -z "$QTV" ] && missing=1
fi
{ [ -z "$QTV" ] || ! ver_ge "$QTV" "$MIN_QT"; } && [ "$FORCE_AQT" = 0 ] && [ "$OS" = Linux ] && missing=1
[ "$WITH_DOTNET" = 1 ] && ! have dotnet && missing=1

if [ "$missing" = 1 ]; then
  if [ "$OS" = "Darwin" ]; then install_macos_packages; else install_linux_packages; fi
else
  ok "Build tools already installed"
fi

# Qt: distro/Homebrew Qt when recent enough, otherwise (or with --use-aqt) the official binaries
if [ "$OS" = "Darwin" ]; then
  if [ "$FORCE_AQT" = 1 ]; then install_qt_with_aqt; else QT_PREFIX="$(brew --prefix qt)"; fi
else
  QTV="$(system_qt_version)"
  if [ "$FORCE_AQT" = 1 ] || [ -z "$QTV" ] || ! ver_ge "$QTV" "$MIN_QT"; then
    [ -n "$QTV" ] && warn "System Qt is $QTV (< $MIN_QT): installing Qt $QT_VERSION from the official servers."
    install_qt_with_aqt
  else
    ok "System Qt $QTV"
  fi
fi

# --- 3. build ----------------------------------------------------------------------
BUILD_DIR="$ROOT/build"
[ "$CLEAN" = 1 ] && { log "Cleaning $BUILD_DIR"; as_user rm -rf "$BUILD_DIR"; }
log "Configuring ($CONFIG)"
args=(-S "$ROOT" -B "$BUILD_DIR" -G Ninja "-DCMAKE_BUILD_TYPE=$CONFIG")
[ -n "$QT_PREFIX" ] && args+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
as_user cmake "${args[@]}"
log "Building"
as_user cmake --build "$BUILD_DIR" --parallel
ok "Build finished"

# --- 4. run ----------------------------------------------------------------------------
if [ "$RUN" = 1 ]; then
  log "Starting CrucibleCode"
  if [ "$OS" = "Darwin" ]; then
    as_user open "$BUILD_DIR/CrucibleCode.app" --args "$ROOT"
  else
    as_user env ${QT_PREFIX:+LD_LIBRARY_PATH="$QT_PREFIX/lib:${LD_LIBRARY_PATH:-}"} "$BUILD_DIR/CrucibleCode" "$ROOT" &
    disown || true
  fi
else
  if [ "$OS" = "Darwin" ]; then ok "Run it with:  open \"$BUILD_DIR/CrucibleCode.app\""
  else ok "Run it with:  \"$BUILD_DIR/CrucibleCode\""; fi
fi
