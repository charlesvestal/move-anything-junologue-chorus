#!/bin/bash
# Install Junologue Chorus module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/junologue-chorus" ]; then
    echo "Error: dist/junologue-chorus not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Junologue Chorus Module ==="

# Deploy to Move - audio_fx subdirectory
echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/move-anything/modules/audio_fx/junologue-chorus"
scp -r dist/junologue-chorus/* ableton@move.local:/data/UserData/move-anything/modules/audio_fx/junologue-chorus/

# Set permissions so Module Store can update later
echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/move-anything/modules/audio_fx/junologue-chorus"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/move-anything/modules/audio_fx/junologue-chorus/"
echo ""
echo "Restart Move Anything to load the new module."
