#!/usr/bin/env bash
# Auto install AutoMarker extension to Premiere on mac

# Get script directory
scriptdir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Use local zxp (extension) file
echo "Using local .zxp file"
fname_zxp="AutoMarker.zxp"
path_zxp="$scriptdir/$fname_zxp"

# Download ExManCmd (extension manager)
echo "Download ExManCmd"
url="https://download.macromedia.com/pub/extensionmanager/ExManCmd_mac.dmg"
fname_exman=$(basename "$url")
tempdir=$(mktemp -d)
path_exman="$tempdir/$fname_exman"
curl "$url" --output "$path_exman"

# Mount ExManCmd DMG
mount_path="$tempdir/ExManCmdMount"
echo "Mount ExManCmd DMG: $path_exman to $mount_path"
hdiutil attach "$path_exman" -mountpoint $mount_path

# Install the .zxp file
exmancmd="$mount_path/Contents/MacOS/ExManCmd"
echo "Install zxp"
"$exmancmd" --install "$path_zxp"
EXIT_CODE=$?
# For debugging
# "$exmancmd" --list all

# Clean up
echo "Unmount ExManCmd DMG"
hdiutil detach "$mount_path"
rm -rf "$tempdir"

exit $EXIT_CODE
