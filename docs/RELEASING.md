# Releasing qypr and waylaunch

`qypr` and `waylaunch` are released independently because they build and
install as separate products. Their numeric versions live in
`qypr/CMakeLists.txt` and `waylaunch/CMakeLists.txt`; the shared `common/`
sources ship with each product and do not have their own release stream.

Both products start at `0.1.0`, which is a public preview under SemVer. Use
`1.0.0` when the product's user-facing configuration and command-line
interfaces are ready to be treated as stable.

## Version rules

- Increment PATCH for backwards-compatible fixes.
- Increment MINOR for backwards-compatible features.
- Increment MAJOR for incompatible changes to the product's supported config,
  command-line interface, or installed behavior.
- Use a SemVer pre-release such as `-rc.1` for release candidates. The product's
  CMake `VERSION` stays at the numeric portion, and the tag carries the suffix.
- `--version` output is generated from CMake for every installed executable.

Examples:

| Product | Stable tag | Release candidate tag |
|---|---|---|
| qypr | `qypr-v0.1.0` | `qypr-v0.2.0-rc.1` |
| waylaunch | `waylaunch-v0.1.0` | `waylaunch-v0.2.0-rc.1` |

Before tagging a new version, update the corresponding `project(... VERSION
...)` in its CMake file, commit that change, and push the commit. A tag is
rejected by the release workflow if its numeric version does not match.

## Publish

Create and push an annotated tag for the product and version:

```sh
git tag -a waylaunch-v0.1.0 -m "waylaunch v0.1.0"
git push origin waylaunch-v0.1.0
```

Use `qypr-v...` for qypr. The GitHub Actions workflow then builds and tests
that product, stages its CMake install tree under `/usr`, and publishes a
GitHub release with generated notes, a `.tar.gz` install archive, and a
`.sha256` checksum. Tags containing a pre-release suffix publish as GitHub
pre-releases. Release assets are built on Ubuntu and named with the product,
version, and architecture. Each product release is kept out of GitHub's
repository-wide “Latest” marker because the repository has two independent
release streams.

To install an archive, extract it and copy its `usr/` tree into the system
prefix. For example:

```sh
tar -xzf waylaunch-0.1.0-linux-x86_64.tar.gz
sudo cp -a waylaunch-0.1.0/usr/. /usr/
```

Install the runtime dependencies listed in the product README first. For
qypr, install its PAM service file as described in the qypr documentation if
you use the lock screen. The archive does not bundle system libraries.
