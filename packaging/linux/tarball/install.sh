#!/bin/sh
# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Installs AUCDataTool from the unpacked .tar.gz, with launcher, icon and file types:
#   sudo ./install.sh        system-wide: /opt/AUCDataTool, links in /usr/local
#   ./install.sh             this user only: ~/.local/opt/AUCDataTool, links in ~/.local
#   ./install.sh --prefix DIR        install the application into DIR instead
#   ./install.sh --uninstall         remove it again (same user / sudo as for installing)
# The application also runs without installing: ./bin/AUCDataTool
set -eu

ID=io.github.doluk.AUCDataTool
SRC=$(cd "$(dirname "$0")" && pwd)

if [ "$(id -u)" = 0 ]; then
    PREFIX=/opt/AUCDataTool
    LINKS=/usr/local
    DATA=/usr/local/share
else
    PREFIX=$HOME/.local/opt/AUCDataTool
    LINKS=$HOME/.local
    DATA=${XDG_DATA_HOME:-$HOME/.local/share}
fi

uninstall=false
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) [ $# -ge 2 ] || { echo "--prefix needs a directory" >&2; exit 2; }; PREFIX=$2; shift ;;
        --prefix=*) PREFIX=${1#--prefix=} ;;
        --uninstall) uninstall=true ;;
        -h|--help) sed -n '4,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

refresh() {
    if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "$DATA/mime" >/dev/null 2>&1 || true; fi
    if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database -q "$DATA/applications" 2>/dev/null || true; fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -q -t "$DATA/icons/hicolor" 2>/dev/null || true; fi
}

remove_integration() {
    rm -f "$LINKS/bin/AUCDataTool" "$DATA/applications/$ID.desktop" \
          "$DATA/icons/hicolor/scalable/apps/$ID.svg" "$DATA/mime/packages/$ID.xml"
}

if $uninstall; then
    remove_integration
    # Only a directory that holds AUCDataTool, never an arbitrary --prefix.
    if [ -x "$PREFIX/bin/AUCDataTool" ] && [ -f "$PREFIX/install.sh" ]; then rm -rf "$PREFIX"; fi
    refresh
    echo "AUCDataTool removed from $PREFIX"
    exit 0
fi

[ -x "$SRC/bin/AUCDataTool" ] || { echo "run install.sh from the unpacked AUCDataTool folder" >&2; exit 1; }

# Copy next to the target first, then swap, so a failed copy leaves an older install intact.
mkdir -p "$(dirname "$PREFIX")"
if [ "$SRC" != "$(cd "$PREFIX" 2>/dev/null && pwd)" ]; then
    rm -rf "$PREFIX.new"
    cp -a "$SRC" "$PREFIX.new"
    if [ -e "$PREFIX" ]; then
        [ -x "$PREFIX/bin/AUCDataTool" ] || { rm -rf "$PREFIX.new"; echo "$PREFIX exists and is not an AUCDataTool installation" >&2; exit 1; }
        rm -rf "$PREFIX"
    fi
    mv "$PREFIX.new" "$PREFIX"
fi

# Command, launcher (with the absolute path: ~/.local/bin is not always in a desktop's PATH),
# icon and file types.
mkdir -p "$LINKS/bin" "$DATA/applications" "$DATA/icons/hicolor/scalable/apps" "$DATA/mime/packages"
ln -sf "$PREFIX/bin/AUCDataTool" "$LINKS/bin/AUCDataTool"
sed "s|^Exec=AUCDataTool|Exec=\"$PREFIX/bin/AUCDataTool\"|" "$PREFIX/share/applications/$ID.desktop" \
    > "$DATA/applications/$ID.desktop"
ln -sf "$PREFIX/share/icons/hicolor/scalable/apps/$ID.svg" "$DATA/icons/hicolor/scalable/apps/$ID.svg"
ln -sf "$PREFIX/share/mime/packages/$ID.xml" "$DATA/mime/packages/$ID.xml"
refresh

echo "AUCDataTool installed in $PREFIX"
echo "  start it from the application menu or with: $LINKS/bin/AUCDataTool"
echo "  remove it with: $PREFIX/install.sh --uninstall"
case ":$PATH:" in *":$LINKS/bin:"*) ;; *) echo "  note: $LINKS/bin is not in your PATH" ;; esac

# System libraries the bundled Qt needs (see INSTALL.txt for the packages).
missing=$( (ldd "$PREFIX/bin/AUCDataTool"; ldd "$PREFIX/plugins/platforms/libqxcb.so"; \
            ldd "$PREFIX/plugins/platforms/libqwayland.so") 2>/dev/null | grep 'not found' | awk '{print $1}' | sort -u)
if [ -n "$missing" ]; then
    echo
    echo "Missing system libraries (AUCDataTool may not start until they are installed):"
    echo "$missing" | sed 's/^/  /'
    echo "See $PREFIX/INSTALL.txt for the packages that provide them."
fi
