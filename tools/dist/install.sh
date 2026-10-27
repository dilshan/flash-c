#!/bin/sh
# Flash C installer.  Run from the unpacked release directory.
#   ./install.sh                  install for the current user (~/.local), or system-wide (/usr/local) as root
#   ./install.sh --prefix DIR     install under DIR
#   ./install.sh --uninstall      remove a previous installation (same --prefix)
set -e

here=$(cd "$(dirname "$0")" && pwd)
if [ "$(id -u)" = 0 ]; then prefix=/usr/local; else prefix="$HOME/.local"; fi
uninstall=0

while [ $# -gt 0 ]; do
    case "$1" in
    --prefix) prefix=$2; shift ;;
    --prefix=*) prefix=${1#--prefix=} ;;
    --uninstall) uninstall=1 ;;
    -h | --help) sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "install.sh: unknown option $1" >&2; exit 1 ;;
    esac
    shift
done

if [ "$uninstall" = 1 ]; then
    rm -f "$prefix/bin/fc" "$prefix/bin/fcc"
    rm -rf "$prefix/share/flashc" "$prefix/share/doc/flashc"
    echo "Flash C removed from $prefix"
    exit 0
fi

[ -x "$here/bin/fc" ] || { echo "install.sh: run it from the unpacked release directory" >&2; exit 1; }

install -d "$prefix/bin" "$prefix/share/flashc/help" "$prefix/share/doc/flashc"
install -m 755 "$here/bin/fc" "$here/bin/fcc" "$prefix/bin"
install -m 644 "$here"/help/*.fch "$prefix/share/flashc/help"
for f in README.md LICENSE VERSION; do
    [ -f "$here/$f" ] && install -m 644 "$here/$f" "$prefix/share/doc/flashc"
done
[ -d "$here/docs" ] && cp -R "$here/docs/." "$prefix/share/doc/flashc/"

echo "Flash C $(cat "$here/VERSION" 2>/dev/null) installed in $prefix"
case ":$PATH:" in
*":$prefix/bin:"*) echo "Run 'fc' to start the IDE or 'fcc' to compile from the command line." ;;
*) echo "Add $prefix/bin to your PATH to use it:"
   echo "    echo 'export PATH=\"$prefix/bin:\$PATH\"' >> ~/.profile" ;;
esac
