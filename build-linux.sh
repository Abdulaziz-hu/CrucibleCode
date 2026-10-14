#!/usr/bin/env bash
# =============================================================================
#  CrucibleCode - build + package for Linux
#
#  Builds a Release binary and packages it as
#      .deb       (dpkg-deb)                         -> dist/cruciblecode_<ver>_<arch>.deb
#      .rpm       (rpmbuild)                         -> dist/cruciblecode-<ver>-1.<arch>.rpm
#      .AppImage  (linuxdeploy + Qt plugin)          -> dist/CrucibleCode-<ver>-<arch>.AppImage
#  plus a .sha256 file next to every package (the in-app updater verifies it).
#
#  Run it on Linux. Windows packages are built by build-windows.ps1 on Windows.
#
#    ./build-linux.sh                       every format whose tools are available
#    ./build-linux.sh --formats deb,rpm     only these formats
#    ./build-linux.sh --version 0.8.0       override the version from CMakeLists.txt
#    ./build-linux.sh --no-ksyntax          skip KSyntaxHighlighting (faster, fewer languages)
#    ./build-linux.sh --qt-prefix ~/Qt/6.7.3/gcc_64   Qt that is not installed system-wide
#    ./build-linux.sh --skip-build          re-package the existing build
#    ./build-linux.sh --clean               wipe the build folder first
#    ./build-linux.sh --jobs 8 --build-dir build/pkg
#
#  Needed: cmake, a C++20 compiler, Qt 6 development files (see scripts/setup.sh to install them).
#  deb needs dpkg-deb, rpm needs rpmbuild (package "rpm" / "rpm-build"), AppImage needs curl or wget.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

FORMATS=""; VERSION=""; USE_KSYNTAX=ON; QT_PREFIX=""; SKIP_BUILD=0; CLEAN=0
JOBS="$(nproc 2>/dev/null || echo 4)"; BUILD_DIR="build/release-linux"; DIST="dist"

c_red=$'\033[31m'; c_grn=$'\033[32m'; c_ylw=$'\033[33m'; c_blu=$'\033[36m'; c_off=$'\033[0m'
log()  { printf '%s==>%s %s\n' "$c_blu" "$c_off" "$*"; }
ok()   { printf '%s ok%s %s\n' "$c_grn" "$c_off" "$*"; }
warn() { printf '%s !!%s %s\n' "$c_ylw" "$c_off" "$*" >&2; }
die()  { printf '%s xx%s %s\n' "$c_red" "$c_off" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

usage() { sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
  case "$1" in
    --formats) FORMATS="$2"; shift ;;
    --version) VERSION="$2"; shift ;;
    --no-ksyntax) USE_KSYNTAX=OFF ;;
    --qt-prefix) QT_PREFIX="$2"; shift ;;
    --skip-build) SKIP_BUILD=1 ;;
    --clean) CLEAN=1 ;;
    --jobs) JOBS="$2"; shift ;;
    --build-dir) BUILD_DIR="$2"; shift ;;
    -h|--help) usage ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
  shift
done

[ "$(uname -s)" = "Linux" ] || die "build-linux.sh must be run on Linux (this is $(uname -s)). On Windows use build-windows.ps1."

# ---- version / arch ---------------------------------------------------------
if [ -z "$VERSION" ]; then
  VERSION="$(sed -n '/^project(/,/)/{s/.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p}' CMakeLists.txt | head -n1)"
fi
[ -n "$VERSION" ] || die "could not read the version from CMakeLists.txt (use --version)"
MACHINE="$(uname -m)"                              # x86_64 / aarch64
case "$MACHINE" in
  x86_64) DEB_ARCH=amd64 ;;
  aarch64|arm64) DEB_ARCH=arm64; MACHINE=aarch64 ;;
  *) DEB_ARCH="$MACHINE" ;;
esac
log "CrucibleCode $VERSION for Linux $MACHINE"

# ---- formats ----------------------------------------------------------------
want() { [ -z "$FORMATS" ] || [[ ",$FORMATS," == *",$1,"* ]]; }
explicit() { [ -n "$FORMATS" ]; }
DO_DEB=0; DO_RPM=0; DO_APPIMAGE=0
if want deb; then
  if have dpkg-deb; then DO_DEB=1; else explicit && die "dpkg-deb not found (needed for --formats deb)" || warn "dpkg-deb not found: skipping .deb"; fi
fi
if want rpm; then
  if have rpmbuild; then DO_RPM=1; else explicit && die "rpmbuild not found (install 'rpm' or 'rpm-build')" || warn "rpmbuild not found: skipping .rpm"; fi
fi
if want appimage; then
  if have curl || have wget; then DO_APPIMAGE=1; else explicit && die "curl or wget is needed to fetch linuxdeploy" || warn "no curl/wget: skipping AppImage"; fi
fi
[ $((DO_DEB + DO_RPM + DO_APPIMAGE)) -gt 0 ] || die "nothing to build"

have cmake || die "cmake not found (run ./scripts/setup.sh to install the build dependencies)"

# ---- configure + build ------------------------------------------------------
[ "$CLEAN" = 1 ] && { log "cleaning $BUILD_DIR"; rm -rf "$BUILD_DIR"; }
if [ "$SKIP_BUILD" = 0 ]; then
  GEN=(); have ninja && GEN=(-G Ninja)
  PREFIX_ARG=(); [ -n "$QT_PREFIX" ] && PREFIX_ARG=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
  log "configuring (Release, KSyntaxHighlighting=$USE_KSYNTAX)"
  cmake -S . -B "$BUILD_DIR" "${GEN[@]}" "${PREFIX_ARG[@]}" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DCRUCIBLE_USE_KSYNTAX="$USE_KSYNTAX"
  log "building with $JOBS jobs"
  cmake --build "$BUILD_DIR" --parallel "$JOBS"
else
  [ -d "$BUILD_DIR" ] || die "--skip-build given but $BUILD_DIR does not exist"
fi

STAGE="$ROOT/$BUILD_DIR/stage"
rm -rf "$STAGE"
log "installing into the staging directory"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" --prefix /usr
BIN="$STAGE/usr/bin/CrucibleCode"
[ -x "$BIN" ] || die "CrucibleCode was not installed to $BIN"
have strip && strip --strip-unneeded "$BIN" || true
install -Dm644 LICENSE "$STAGE/usr/share/doc/cruciblecode/copyright"
[ -f THIRD_PARTY.md ] && install -Dm644 THIRD_PARTY.md "$STAGE/usr/share/doc/cruciblecode/THIRD_PARTY.md"

# Remove qt.conf so it isn't caught as an unpackaged file by rpmbuild[cite: 1]
rm -f "$STAGE/usr/bin/qt.conf"

mkdir -p "$DIST"
sums() { ( cd "$DIST" && sha256sum "$1" > "$1.sha256" ); ok "$DIST/$1"; }

# ---- .deb -------------------------------------------------------------------
if [ "$DO_DEB" = 1 ]; then
  log "building .deb"
  PKG="$ROOT/$BUILD_DIR/deb-root"
  rm -rf "$PKG"; mkdir -p "$PKG"
  cp -a "$STAGE/." "$PKG/"
  mkdir -p "$PKG/DEBIAN"

  DEPENDS=""
  if have dpkg-shlibdeps; then
    # dpkg-shlibdeps wants a debian/control file in the current directory
    TMP="$(mktemp -d)"; mkdir -p "$TMP/debian"
    printf 'Source: cruciblecode\n\nPackage: cruciblecode\nArchitecture: any\n' > "$TMP/debian/control"
    DEPENDS="$(cd "$TMP" && dpkg-shlibdeps -O -e"$BIN" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')" || true
    rm -rf "$TMP"
  fi
  if [ -z "$DEPENDS" ]; then
    warn "dpkg-shlibdeps unavailable or failed: using a generic Qt 6 dependency list"
    DEPENDS="libc6, libstdc++6, libqt6core6 | libqt6core6t64, libqt6gui6 | libqt6gui6t64, libqt6widgets6 | libqt6widgets6t64, libqt6network6 | libqt6network6t64"
  fi
  SIZE="$(du -sk "$PKG/usr" | cut -f1)"
  cat > "$PKG/DEBIAN/control" <<CTRL
Package: cruciblecode
Version: $VERSION
Section: devel
Priority: optional
Architecture: $DEB_ARCH
Installed-Size: $SIZE
Depends: $DEPENDS
Recommends: cmake, clangd, git
Suggests: python3
Maintainer: CrucibleCode contributors <https://github.com/Abdulaziz-hu/CrucibleCode/issues>
Homepage: https://github.com/Abdulaziz-hu/CrucibleCode
Description: Lightweight IDE for C, C++ and C#
 CrucibleCode is a fast, cross-platform code editor and IDE built with Qt 6:
 CMake / .NET builds, an integrated terminal, Git, Language Server support,
 snippets, project templates and live preview for web projects.
CTRL
  OUT="cruciblecode_${VERSION}_${DEB_ARCH}.deb"
  dpkg-deb --root-owner-group --build "$PKG" "$DIST/$OUT" >/dev/null
  sums "$OUT"
fi

# ---- .rpm -------------------------------------------------------------------
if [ "$DO_RPM" = 1 ]; then
  log "building .rpm"
  TOP="$ROOT/$BUILD_DIR/rpm-top"
  rm -rf "$TOP"; mkdir -p "$TOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS,BUILDROOT}
  RPM_VERSION="${VERSION//-/~}"                       # '-' is not allowed in an RPM version
  cat > "$TOP/SPECS/cruciblecode.spec" <<SPEC
%global debug_package %{nil}
%global __os_install_post %{nil}
%define _build_id_links none
Name:           cruciblecode
Version:        $RPM_VERSION
Release:        1
Summary:        Lightweight IDE for C, C++ and C#
License:        MIT
URL:            https://github.com/Abdulaziz-hu/CrucibleCode
BuildArch:      $MACHINE
Recommends:     cmake
Recommends:     clang-tools-extra

%description
CrucibleCode is a fast, cross-platform code editor and IDE built with Qt 6:
CMake / .NET builds, an integrated terminal, Git, Language Server support,
snippets, project templates and live preview for web projects.

%install
mkdir -p %{buildroot}
cp -a "$STAGE"/. %{buildroot}/

%files
/usr/bin/CrucibleCode
/usr/share/applications/cruciblecode.desktop
/usr/share/icons/hicolor/*/apps/cruciblecode.png
%license /usr/share/doc/cruciblecode/copyright
/usr/share/doc/cruciblecode/THIRD_PARTY.md
SPEC
  if ! rpmbuild -bb --target "$MACHINE" --define "_topdir $TOP" "$TOP/SPECS/cruciblecode.spec" >"$TOP/rpmbuild.log" 2>&1; then
    tail -n 40 "$TOP/rpmbuild.log" >&2; die "rpmbuild failed (full log: $TOP/rpmbuild.log)"
  fi
  RPM_FILE="$(find "$TOP/RPMS" -name '*.rpm' | head -n1)"
  [ -n "$RPM_FILE" ] || die "rpmbuild produced no package"
  OUT="$(basename "$RPM_FILE")"
  cp "$RPM_FILE" "$DIST/$OUT"
  sums "$OUT"
fi

# ---- AppImage ---------------------------------------------------------------
if [ "$DO_APPIMAGE" = 1 ]; then
  log "building AppImage"
  TOOLS="$ROOT/build/tools"; mkdir -p "$TOOLS"
  fetch() { # url dest
    [ -x "$2" ] && return 0
    log "downloading $(basename "$2")"
    if have curl; then curl -fL --retry 3 -o "$2" "$1"; else wget -O "$2" "$1"; fi
    chmod +x "$2"
  }
  LD_BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
  QT_BASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"
  fetch "$LD_BASE/linuxdeploy-$MACHINE.AppImage" "$TOOLS/linuxdeploy-$MACHINE.AppImage"
  fetch "$QT_BASE/linuxdeploy-plugin-qt-$MACHINE.AppImage" "$TOOLS/linuxdeploy-plugin-qt-$MACHINE.AppImage"

  QMAKE_BIN=""
  for q in "${QT_PREFIX:+$QT_PREFIX/bin/qmake}" qmake6 qmake-qt6 qmake; do
    [ -n "$q" ] && { have "$q" || [ -x "$q" ]; } && { QMAKE_BIN="$(command -v "$q" || echo "$q")"; break; }
  done
  [ -n "$QMAKE_BIN" ] || die "qmake (Qt 6) not found: the AppImage Qt plugin needs it (use --qt-prefix)"

  APPDIR="$ROOT/$BUILD_DIR/AppDir"
  rm -rf "$APPDIR"
  cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr" >/dev/null
  have strip && strip --strip-unneeded "$APPDIR/usr/bin/CrucibleCode" || true

  # linuxdeploy itself is an AppImage: extract-and-run avoids needing FUSE (containers, CI)
  export APPIMAGE_EXTRACT_AND_RUN=1
  export QMAKE="$QMAKE_BIN"
  export NO_STRIP=1                                   # Disables internal strip execution to avoid .relr.dyn errors
  export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
  export EXTRA_QT_PLUGINS="svg;iconengines"
  export DEPLOY_PLATFORM_THEMES=1
  mkdir -p "$ROOT/$BUILD_DIR/appimage-out"
  ( cd "$ROOT/$BUILD_DIR/appimage-out" && rm -f ./*.AppImage &&
    "$TOOLS/linuxdeploy-$MACHINE.AppImage" --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/CrucibleCode" \
        --desktop-file "$APPDIR/usr/share/applications/cruciblecode.desktop" \
        --icon-file "$ROOT/resources/icons/icon-256.png" --icon-filename cruciblecode \
        --plugin qt --output appimage )
  AI_FILE="$(find "$ROOT/$BUILD_DIR/appimage-out" -maxdepth 1 -name '*.AppImage' | head -n1)"
  [ -n "$AI_FILE" ] || die "linuxdeploy produced no AppImage"
  OUT="CrucibleCode-${VERSION}-${MACHINE}.AppImage"
  cp "$AI_FILE" "$DIST/$OUT"; chmod +x "$DIST/$OUT"
  sums "$OUT"
fi

log "done. Packages are in $ROOT/$DIST/"
ls -lh "$DIST" | sed 's/^/    /'