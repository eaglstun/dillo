# Deploying Dillo to Pinecone

Pinecone has no graphical session, so Dillo is installed together with Xvfb and
gets a `dillo-xvfb` wrapper. The install is entirely under `/home/eric/.local`;
only the one-time Debian package bootstrap needs root.

## First deployment

From this repository on the Mac, run in a real terminal:

```sh
./deploy/pinecone/deploy.sh --bootstrap
```

Enter the Pinecone `sudo` password when prompted. The command then streams the
local working-tree snapshot to Pinecone, builds it there, and activates it. It
does not depend on the commit having been pushed to GitHub. Tracked changes are
included; untracked files are not.

## Routine deployment

After the bootstrap, deploying the current commit is one command and needs no
root access:

```sh
./deploy/pinecone/deploy.sh
```

A particular commit, tag, or local branch can be selected explicitly. In that
mode, working-tree changes are ignored:

```sh
./deploy/pinecone/deploy.sh --revision c08fb6acd0b6
```

Re-running a deployed revision is safe. Builds and installs are revisioned at:

```text
~/.local/src/dillo/<revision>
~/.cache/dillo-build/<revision>
~/.local/opt/dillo/releases/<revision>
```

`~/.local/opt/dillo/current` is switched atomically after a successful build,
and commands in `~/.local/bin` follow that link. Older releases remain available
for rollback:

```sh
ssh eric@68.183.63.41
ln -sfn "releases/<full-commit>" ~/.local/opt/dillo/current.next
mv -Tf ~/.local/opt/dillo/current.next ~/.local/opt/dillo/current
```

## Running on the headless droplet

Ensure `~/.local/bin` is on `PATH`, or use the full paths shown here:

```sh
~/.local/bin/dillo --version
~/.local/bin/dillo-xvfb https://dillo-browser.org/
```

For control-socket automation, start one headless browser in the background and
then use `dilloc` as documented in `AUTOMATION.md`:

```sh
nohup ~/.local/bin/dillo-xvfb about:blank >~/.cache/dillo-xvfb.log 2>&1 &
~/.local/bin/dilloc open https://example.com/
~/.local/bin/dilloc dump
~/.local/bin/dilloc quit
```

Bare `dilloc` deliberately refuses to choose when multiple Dillo instances are
running. In that case, set `DILLO_PID` to the PID named by the desired socket in
`~/.dillo/ctl/`.
