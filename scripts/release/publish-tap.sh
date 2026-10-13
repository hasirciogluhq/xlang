#!/usr/bin/env bash
# Commit the generated formula to the Homebrew tap repository.
#
# Usage: scripts/release/publish-tap.sh <version> <formula-file>
#
# Environment:
#   TAP_TOKEN  GitHub token with contents:write on the tap repo (required)
#   TAP_REPO   owner/name of the tap (default: hasirciogluhq/homebrew-tap)
#   TAP_BRANCH branch to push to (default: main)
#   TAP_PATH   formula path inside the tap (default: xlang.rb)
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <version> <formula-file>" >&2
  exit 2
fi

version="$1"
formula="$2"
tap_repo="${TAP_REPO:-hasirciogluhq/homebrew-tap}"
tap_branch="${TAP_BRANCH:-main}"
tap_path="${TAP_PATH:-xlang.rb}"

if [ -z "${TAP_TOKEN:-}" ]; then
  echo "error: TAP_TOKEN is not set (add the tap token secret and map it to TAP_TOKEN)" >&2
  exit 1
fi
if [ ! -f "$formula" ]; then
  echo "error: formula file not found: $formula" >&2
  exit 1
fi

# Pass the token as an HTTP header instead of embedding it in the remote URL,
# so it never ends up in .git/config.
auth="$(printf 'x-access-token:%s' "$TAP_TOKEN" | base64 | tr -d '\n')"
git_auth=(-c "http.https://github.com/.extraheader=AUTHORIZATION: basic ${auth}")

formula_abs="$(cd "$(dirname "$formula")" && pwd)/$(basename "$formula")"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git "${git_auth[@]}" clone --quiet --depth 1 --branch "$tap_branch" \
  "https://github.com/${tap_repo}.git" "$work/tap"

cd "$work/tap"
cp "$formula_abs" "$tap_path"

git config user.name "${GIT_AUTHOR_NAME:-github-actions[bot]}"
git config user.email "${GIT_AUTHOR_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}"

git add "$tap_path"
if git diff --cached --quiet; then
  echo "formula already up to date for xlang ${version}; nothing to push"
  exit 0
fi

git commit --quiet -m "xlang ${version}"

# The tap may receive pushes from other release pipelines; retry on conflict.
for attempt in 1 2 3; do
  if git "${git_auth[@]}" push --quiet origin "HEAD:${tap_branch}"; then
    echo "pushed xlang ${version} to ${tap_repo}@${tap_branch} (${tap_path})"
    exit 0
  fi
  echo "push failed (attempt ${attempt}); rebasing and retrying"
  git "${git_auth[@]}" pull --quiet --rebase origin "$tap_branch"
done

echo "error: could not push to ${tap_repo}" >&2
exit 1
