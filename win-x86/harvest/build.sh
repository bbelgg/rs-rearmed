#!/bin/bash
# Rebuild rsharvest2.exe from source, to check the committed one or to change it. Users do not need this:
# rsharvest2.exe and rsharvest2.exe.config are committed next to this script, and lp_install.py
# harvest-export copies them.
#
# Needs the .NET SDK (any version >= 6), from Microsoft's installer: https://dotnet.microsoft.com/download
# The build pulls the net48 reference assemblies from NuGet the first time (network once), then is offline.
# The build is reproducible (rsharvest2.csproj: deterministic, no PDB, paths mapped, no git revision), so
# the same source gives the same exe wherever it is built; this prints its SHA-256 next to the one in
# README.md.
#
# Output: dist/rsharvest2.exe and dist/rsharvest2.exe.config, with the two wrapper scripts.
# --install: also copy the two built files over the committed ones (after a change to rsharvest2.cs;
# then update the SHA-256 in README.md).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
command -v dotnet >/dev/null || {
    echo "The .NET SDK is missing. Install it with Microsoft's installer (https://dotnet.microsoft.com/download) and run this again." >&2
    exit 1
}
out="$here/dist"
mkdir -p "$out"
dotnet publish "$here/rsharvest2.csproj" -c Release -o "$out"
cp "$here/harvest-game.bat" "$here/harvest-game.ps1" "$out/"
built=$(shasum -a 256 "$out/rsharvest2.exe" | cut -d' ' -f1)
expected=$(sed -n 's/^SHA-256 of rsharvest2.exe: `\([0-9a-f]*\)`.*/\1/p' "$here/README.md")
echo
echo "Built: $out/rsharvest2.exe"
echo "  SHA-256 built:     $built"
echo "  SHA-256 README.md: ${expected:-(none)}"
if [[ $built == "$expected" ]]; then
    echo "  The same as the committed rsharvest2.exe."
else
    echo "  Different from README.md: rsharvest2.cs or rsharvest2.csproj changed, or another compiler version."
fi
if [[ ${1:-} == --install ]]; then
    cp "$out/rsharvest2.exe" "$out/rsharvest2.exe.config" "$here/"
    echo "Copied over the committed files; put the new SHA-256 into README.md."
fi
