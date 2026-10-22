#!/usr/bin/env bash
#
# Stage a dev_install of the build and tar it up, for the GitHub Linux and
# MacOS runners.  Run from the top of the source tree after make, with the
# same SPICE, CDF and N_ARCH settings in the environment.
#
#   .github/scripts/package.sh VERSION PLATFORM ARCH OUTDIR
#
# Writes OUTDIR/das2C-VERSION-PLATFORM-ARCH.tar.gz, for example
# das2C-3.0.0-ubuntu24.04-x64.tar.gz.  The tarball is made here because
# upload-artifact zips drop the programs' execute permissions.
#
# DEPENDS.txt in the tarball names the system packages (Ubuntu) or Homebrew
# formulas (MacOS) that provide the shared libraries the programs load.
# CSPICE and CDF are linked statically, so they never show up there.

set -euo pipefail

if [ $# -ne 4 ]; then
	echo "usage: $0 VERSION PLATFORM ARCH OUTDIR" >&2
	exit 2
fi

ver="$1"
name="das2C-$1-$2-$3"
out="$4"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
top="$stage/das2C-$ver"

make PREFIX=/ DESTDIR="$top" dev_install
cp LICENSE "$top/LICENSE.txt"
cp README.md THIRD-PARTY.txt "$top/"
cp -r LICENSES "$top/"

if [ "$(uname -s)" = "Darwin" ]; then
	# Homebrew libraries are linked by their keg paths, $BREW/opt/<formula>/lib
	# (see Darwin.mak), so the formula name is the path component after opt.
	brew_pfx=$(brew --prefix)
	formulas=$(otool -L "$top"/bin/* \
	           | awk -v p="$brew_pfx/" 'index($1, p) == 1 {
	                 split(substr($1, length(p) + 1), a, "/"); print a[2] }' \
	           | sort -u)
	{
		echo "Built on $(sw_vers -productName) $(sw_vers -productVersion)."
		echo "The programs need these Homebrew formulas:"
		echo
		for f in $formulas; do echo "  $f"; done
		echo
		echo "Install them with:  brew install $(echo $formulas)"
		echo
		echo "The programs are not notarized.  If MacOS refuses to run them"
		echo "after a browser download, clear the quarantine flag:"
		echo
		echo "  xattr -dr com.apple.quarantine das2C-$ver"
	} > "$top/DEPENDS.txt"
else
	# ldd may give /lib paths that dpkg only knows under /usr/lib.
	libs=$(for f in "$top"/bin/*; do ldd "$f"; done \
	       | awk '/=> \// {print $3}' | sort -u)
	. /etc/os-release
	{
		echo "Built on $PRETTY_NAME.  The programs need these packages:"
		echo
		for lib in $libs; do
			dpkg -S "$lib" 2>/dev/null \
				|| dpkg -S "$(realpath "$lib")" 2>/dev/null \
				|| dpkg -S "/usr$lib" 2>/dev/null \
				|| echo "::warning::No package found for $lib" >&2
		done | cut -d: -f1 | sort -u | sed 's/^/  /'
	} > "$top/DEPENDS.txt"
fi
cat "$top/DEPENDS.txt"

# COPYFILE_DISABLE keeps MacOS tar from adding ._ files for extended attributes
mkdir -p "$out"
COPYFILE_DISABLE=1 tar -C "$stage" -czf "$out/$name.tar.gz" "das2C-$ver"
ls -l "$out/$name.tar.gz"
