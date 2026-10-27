# Releasing Flash C

Releases are built and published by GitHub Actions when a version tag is pushed. The version number
comes from the git tag, so there is no file to edit.

## How the version works

The Makefile runs `git describe --tags --always --dirty`, strips a leading `v`, and passes the result
to every compile as `-DFC_VERSION`. The IDE (startup, About box, help) and `fcc` all show it.

| Build state                     | Version shown         |
|---------------------------------|-----------------------|
| exactly on tag `v1.2.3`         | `1.2.3`               |
| 4 commits after `v1.2.3`        | `1.2.3-4-gabc1234`    |
| uncommitted changes             | `...-dirty` appended  |
| no tags in the repository       | the commit hash       |
| no `.git` (source archive)      | `0.0.0-unknown`, unless `make VERSION=x.y.z` is given |

`make version` prints the version the current tree would get.

## Making a release

1. Make sure `main` is committed and the CI workflow (`.github/workflows/ci.yml`) is green.
2. Pick a version following [semantic versioning](https://semver.org): `vMAJOR.MINOR.PATCH`.
   Use a suffix such as `v1.3.0-rc1` for a pre-release; any tag containing a hyphen is published
   as a GitHub pre-release.
3. Tag and push:

   ```
   git tag v1.2.3
   git push origin v1.2.3
   ```

4. Watch the **Release** workflow in the Actions tab. It will:
   - check out the full history (needed for `git describe`),
   - build on `ubuntu-22.04` (an older glibc keeps the binaries usable on most current distros),
   - run `make test`,
   - run `make dist` and write `SHA256SUMS`,
   - create the GitHub release with generated notes and attach both files.
5. Open the release page, edit the generated notes if you want, and check the download.

A failed run publishes nothing. Fix the problem, delete the tag (`git tag -d v1.2.3 &&
git push origin :refs/tags/v1.2.3`), and tag again.

## What is in a release

`flash-c-<version>-linux-x86_64.tar.gz` unpacks to `flash-c-<version>-linux-x86_64/`:

```
bin/fc  bin/fcc      IDE and command-line compiler (stripped)
help/*.fch           IDE help pages
docs/                documentation
install.sh           installer / uninstaller
README.md LICENSE VERSION
```

The compiler's runtime library and headers are embedded in `fcc`/`fc`, so the binaries need only
the C library of the host (glibc). `SHA256SUMS` lets users verify the download.

## Building a package locally

```
git tag v0.0.1-test        # optional; delete it afterwards
make dist                  # writes build/dist/flash-c-<version>-linux-<arch>.tar.gz
```

## Using a release

Install (default `~/.local`, or `/usr/local` as root; `--prefix DIR` to choose):

```
tar xzf flash-c-*.tar.gz
cd flash-c-*/
./install.sh
```

Remove it with `./install.sh --uninstall` (give the same `--prefix` if one was used).

No install is needed either: unpack and run `./bin/fc` or `./bin/fcc` from the folder. The IDE
finds its help files in `../help` relative to the binary, so the folder can be moved anywhere as
long as `bin/` and `help/` stay together.
