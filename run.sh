#!/usr/bin/env bash

set -e

# Ensure ssh-agent exists
if [ -z "$SSH_AUTH_SOCK" ]; then
    echo "ERROR: SSH agent not running."
    echo ""
    echo "Start it with:"
    echo '  eval "$(ssh-agent -s)"'
    echo "  ssh-add ~/.ssh/id_ed25519"
    exit 1
fi

docker run -it --rm \
    -v $SSH_AUTH_SOCK:/ssh-agent \
    -e SSH_AUTH_SOCK=/ssh-agent \
    pi:gengodb-benchmark-0.0.1