#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
reaper_resource=${REAPER_RESOURCE_PATH:-"${HOME}/Library/Application Support/REAPER"}

mkdir -p "$reaper_resource/Effects/s3g"
mkdir -p "$reaper_resource/Scripts/s3g"
cp "$repo_dir/tracker/reaper/s3g_tracker_acceptance_capture.jsfx" \
  "$reaper_resource/Effects/s3g/s3g_tracker_acceptance_capture"
cp "$repo_dir/tracker/reaper/s3g_tracker_acceptance.lua" \
  "$reaper_resource/Scripts/s3g/s3g_tracker_acceptance.lua"

echo "Installed Tracker acceptance harness into: $reaper_resource"
echo "Run Scripts/s3g/s3g_tracker_acceptance.lua from REAPER Actions."
