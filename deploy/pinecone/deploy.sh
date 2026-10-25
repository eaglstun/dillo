#!/usr/bin/env bash
# Build a chosen commit on Pinecone and atomically activate the user-local install.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
target=${DILLO_DEPLOY_TARGET:-eric@68.183.63.41}
revision=
bootstrap=false

usage() {
   cat <<'EOF'
Usage: deploy/pinecone/deploy.sh [--bootstrap] [--revision GIT_REF] [--target USER@HOST]

  --bootstrap       Install/update Debian build and Xvfb packages first (sudo prompts).
  --revision REF    Deploy this local commit instead of HEAD.
  --target HOST     Override eric@68.183.63.41.

With no --revision, tracked working-tree changes are included in a reproducible
Git tree snapshot. Untracked files are not deployed. No GitHub push is required.
EOF
}

while [ "$#" -gt 0 ]; do
   case "$1" in
      --bootstrap)
         bootstrap=true
         shift
         ;;
      --revision)
         [ "$#" -ge 2 ] || { echo "--revision needs a value" >&2; exit 2; }
         revision=$2
         shift 2
         ;;
      --target)
         [ "$#" -ge 2 ] || { echo "--target needs a value" >&2; exit 2; }
         target=$2
         shift 2
         ;;
      -h|--help)
         usage
         exit 0
         ;;
      *)
         echo "Unknown argument: $1" >&2
         usage >&2
         exit 2
         ;;
   esac
done

cd "$repo_root"
if [ -n "$revision" ]; then
   source_revision=$(git rev-parse --verify "${revision}^{commit}")
   deploy_id=$source_revision
else
   source_revision=HEAD
   if ! git diff-index --quiet HEAD --; then
      source_revision=$(git stash create "dillo deployment snapshot")
      [ -n "$source_revision" ] || { echo "Could not snapshot the working tree" >&2; exit 1; }
      tree=$(git rev-parse "${source_revision}^{tree}")
      deploy_id=worktree-$tree
   else
      source_revision=$(git rev-parse HEAD)
      deploy_id=$source_revision
   fi
fi
short_id=${deploy_id:0:21}

remote_tools=.local/share/dillo-deploy
ssh "$target" "mkdir -p '$remote_tools' && cat > '$remote_tools/bootstrap-debian.sh' && chmod 755 '$remote_tools/bootstrap-debian.sh'" \
   < "$script_dir/bootstrap-debian.sh"

if $bootstrap; then
   # A TTY is intentional: sudo on Pinecone requires Eric's password.
   ssh -t "$target" "$remote_tools/bootstrap-debian.sh"
fi

if ! ssh "$target" 'command -v g++ >/dev/null && command -v autoconf >/dev/null && command -v automake >/dev/null && command -v fltk-config >/dev/null && command -v xvfb-run >/dev/null'; then
   cat >&2 <<EOF
Pinecone is missing build or headless-runtime packages.
Run this from a real terminal, then retry:

  $0 --bootstrap
EOF
   exit 1
fi

echo "Uploading Dillo snapshot $short_id to $target..."
git archive --format=tar "$source_revision" |
   ssh "$target" "set -eu; src=\"\$HOME/.local/src/dillo/$deploy_id\"; mkdir -p \"\$src\"; tar -xf - -C \"\$src\""

echo "Building and activating $short_id on $target..."
ssh "$target" bash -s -- "$deploy_id" "$short_id" <<'REMOTE_BUILD'
set -euo pipefail

deploy_id=$1
short_id=$2
source_dir="$HOME/.local/src/dillo/$deploy_id"
build_dir="$HOME/.cache/dillo-build/$deploy_id"
install_root="$HOME/.local/opt/dillo"
release_dir="$install_root/releases/$deploy_id"
lock_file="$HOME/.cache/dillo-deploy.lock"

mkdir -p "$HOME/.cache" "$HOME/.local/bin" "$install_root/releases"
exec 9>"$lock_file"
flock 9

if [ ! -x "$release_dir/bin/dillo" ]; then
   cd "$source_dir"
   ./autogen.sh
   mkdir -p "$build_dir"
   cd "$build_dir"
   "$source_dir/configure" --prefix="$release_dir"
   make -j"$(getconf _NPROCESSORS_ONLN)"
   make install
fi

printf '%s\n' "$deploy_id" > "$release_dir/DEPLOYED_REVISION"

next_link="$install_root/.current.$short_id.$$"
ln -s "releases/$deploy_id" "$next_link"
mv -Tf "$next_link" "$install_root/current"

for program in "$release_dir"/bin/*; do
   if [ -f "$program" ] && [ -x "$program" ]; then
      name=$(basename "$program")
      ln -sfn "../opt/dillo/current/bin/$name" "$HOME/.local/bin/$name"
   fi
done

cat > "$HOME/.local/bin/dillo-xvfb" <<'EOF'
#!/bin/sh
exec xvfb-run -a "$HOME/.local/opt/dillo/current/bin/dillo" "$@"
EOF
chmod 755 "$HOME/.local/bin/dillo-xvfb"

"$HOME/.local/bin/dillo" --version
test -x "$HOME/.local/bin/dilloc"
if ldd "$HOME/.local/bin/dillo" | grep -q 'not found'; then
   echo "Dillo has unresolved shared libraries:" >&2
   ldd "$HOME/.local/bin/dillo" >&2
   exit 1
fi

echo "Activated Dillo $short_id"
echo "Binary: $HOME/.local/bin/dillo"
echo "Headless wrapper: $HOME/.local/bin/dillo-xvfb"
REMOTE_BUILD
