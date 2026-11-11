#!/usr/bin/env bash
# Creates the GitHub repo, pushes, and lets GitHub Actions build + release the ISO.
set -e
REPO="${1:-nova-os}"
git init -b main
git add .
git commit -m "Nova OS 0.1"
gh repo create "$REPO" --public --source=. --remote=origin --push
echo "Pushed. Watch the build with:  gh run watch"
echo "ISO will appear under:         gh release list"
