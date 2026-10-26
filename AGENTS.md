# Stay focused on the objective

This repository packages the unmodified upstream MsQuic Rust API and native
sources as a Rust crate. Implement only what is strictly necessary for that.

- Keep MsQuic and its dependencies as Git submodules at their exact upstream
  pins. Never patch upstream files in this repository.
- Forward upstream features and build behavior. Do not invent native build
  policy.
- Keep versions in Cargo.toml. Check the crate version against upstream's
  Cargo.toml, ignoring prerelease suffixes.
- Do not add release catalogs, source inventories, generated provenance
  manifests, release controllers, or tests of CI/workspace configuration.
- Keep tests focused on MsQuic: API initialization, native version, and a local
  TLS handshake with stream echo. Do not duplicate upstream's test suite.
- Keep packaging and CI small. Check the compressed crate is below 10 MB before
  building it. Run pre-commit before packaging and test the packaged crate
  before publishing through crates.io trusted publishing.
- Review and simplify the complete change before finishing. Remove unnecessary
  abstractions, files, checks, and duplicated information.
- Build and test locally before pushing. Stop a release sequence when a check
  fails.
- Squash unpublished work when requested. Preserve existing published tags.
- Never push, publish, authenticate, or restore a remote without current user
  authorization. The current task is local preparation for review only.
