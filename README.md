# msquic-src

The upstream MsQuic Rust API and native sources, packaged as a Rust crate.
MsQuic and its TLS dependencies remain Git submodules at their exact upstream
pins. The wrapper runs upstream's build script and forwards its Cargo features.

Use the crate version listed in `Cargo.toml`. To build statically with bundled
QuicTLS, enable `src`, `quictls`, and `static`. The available features are
listed in `Cargo.toml`; `src` and `find` are mutually exclusive.

Source builds require CMake, a C/C++ compiler, Perl, and Make. Linux static
builds also require NUMA development libraries. Windows CI uses MSVC, Ninja,
Strawberry Perl, and JOM. Binding regeneration requires libclang.

## Local checks

Initialize the `msquic` submodule and its pinned QuicTLS, XDP, and optional
OpenSSL submodules before building.

```sh
prek run -a
make package
make build
make test
```

`make check-version` compares this crate's version with upstream's Cargo
manifest, ignoring prerelease suffixes. Packaging reads the pinned Git trees,
compresses TLS sources to stay below crates.io's 10 MB limit, and preserves
their licenses. Source extraction and generated files stay under Cargo's
`OUT_DIR`.

The smoke tests cover Rust API initialization, the native version, and a local
TLS handshake with stream echo. CI tests the packaged crate across supported
feature modes on Linux x86_64, macOS ARM64, and Windows x64. Upstream's Linux
ARM64 and Android Rust binding issues remain unsupported here.

Upstream static Windows builds can require explicit system libraries; the smoke
test's `windows.rs` shows the consumer configuration. Tests use unoptimized
builds. No upstream test suite or performance benchmarks run here.

The single `release.yml` workflow runs pre-commit, packaging, and MsQuic smoke
tests on pushes and pull requests. Publication through crates.io trusted
publishing and registry verification run only for version tags. Release
sequencing is performed outside this repository.
