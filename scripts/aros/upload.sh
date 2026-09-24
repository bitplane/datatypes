#!/bin/sh
set -eu

channel=${1:?usage: upload.sh <channel-dir>}
pkg=${PKG:-pkg}
url=${PKG_CHANNEL_URL:-https://aros-pkg.azurewebsites.net/bitplane}
"$pkg" PUSH CHANNEL "$channel" TO "$url"
