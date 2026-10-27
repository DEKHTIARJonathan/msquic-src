# Stay focused on the objective

This repository packages the unmodified upstream MsQuic Rust API and native
sources as a Rust crate. Implement only what is strictly necessary for that.

## Scope and authorization

- Follow the user's active task and existing authorization. Continue authorized
  work without repeatedly asking permission. A status question or clarification
  does not cancel the remaining work.
- Never push, publish, authenticate, or restore a remote without authorization
  covering the active task. Editing a document does not authorize publishing or
  pushing it. Do not carry a completed release task's authorization into
  unrelated future work.
- Review and simplify the complete change before finishing. Remove unnecessary
  abstractions, files, checks, and duplicated information.
- Do not add release catalogs, source inventories, generated provenance
  manifests, release controllers, or tests of CI/workspace configuration.
- Keep durable instructions here; keep versions in Cargo.toml and release status
  in Git, GitHub Actions, and the registry. Do not turn this file into a release
  history or store temporary log paths and run IDs here.

## Upstream sources and version bumps

- Keep MsQuic and its dependencies as Git submodules at their exact upstream
  pins. Never patch upstream files, generated bindings, or dependency sources.
- Check out the requested upstream tag in `msquic/`, then initialize its own
  submodules with `git -C msquic submodule update --init --recursive`. Do not
  advance dependency pins independently of the MsQuic tag.
- Match the root Cargo version to upstream's Cargo.toml, ignoring prerelease
  suffixes. Update the root Cargo.lock package version too. `make check-version`
  checks the manifest versions; packaging also checks the upstream tag's commit.
- Compare upstream's complete feature, dependency, and build-dependency tables
  for each release. Mirror changes, including feature implications and optional
  dependencies. Keep only the additional dependencies required for packaging.
- A routine patch bump should change only Cargo.toml, Cargo.lock, and the
  `msquic` gitlink. Additional wrapper or CI changes need a reproduced failure,
  a minimal fix, and an explanation of why a version/pin bump was insufficient.
- Forward upstream's Rust API and native build script. Do not invent native
  build policy or silently work around upstream target restrictions.

## Source staging and packaging

- Stage build sources and generated files under Cargo's OUT_DIR. Upstream's
  build script and binding regeneration must not modify the checkout or the
  extracted crate's sources.
- Support both a full recursive Git checkout and an extracted published crate.
  Nested OpenSSL submodules contain directory symlinks: source copying must
  recognize these as directories, rather than trying to copy them as files.
- Use `make package`, which reads pristine pinned Git trees into
  `target/package-src/` and writes the candidate archive under `dist/`. Preserve
  source contents, executable permissions, licenses, and required build
  metadata.
- Compress the TLS sources to meet the crate size limit. If pruning unused
  documentation or tests, retain metadata such as `build.info` needed by the
  upstream build. Do not edit upstream source contents to reduce archive size.
- TLS archives concatenate provider tar streams. Extraction must read past tar
  zero blocks and consume the XZ decoder to completion to validate the stream.
- Check the compressed `.crate` is strictly below **10,000,000 bytes before any
  build**. Keep packaging and CI small; do not assume hosted runners have `rg`
  installed. Use portable tools in packaging commands.
- Pass the tested `.crate` itself between CI jobs. Unpacked source artifacts can
  lose executable bits, making the publication archive differ from the tested
  crate. Upstream's executable CMake helper exposed this failure.
- Reconstruct publication sources from the tested archive and restore
  Cargo.toml.orig as Cargo.toml before repackaging. Require a byte-for-byte
  match with the tested archive before publishing, then verify the registry
  serves the same bytes. Follow `.github/workflows/release.yml` for the exact
  procedure.

## Local validation and smoke tests

- Run `prek run -a` before packaging and fix all failures. Also run
  `cargo fmt --check` and
  `cargo fmt --manifest-path testcrate/Cargo.toml --check`.
- After packaging and checking its size, build and test locally before pushing.
  `make build` and `make test` exercise the source checkout; they do not replace
  testing a consumer of the extracted `.crate`.
- Test the extracted archive from a separate consumer project using `testcrate`.
  Point its dependency and MSQUIC_INCLUDE_DIR at the extracted crate, and set
  MSQUIC_EXPECTED_VERSION to the native version without a prerelease suffix.
- Keep tests focused on MsQuic: API initialization, native version, and a local
  TLS handshake with stream echo. Run smoke tests serially with temporary local
  certificate fixtures. Do not duplicate upstream's test suite or add
  performance benchmarks and configuration tests.
- Exercise supported feature modes from the pinned manifest: default, static
  QuicTLS, preview API with overwrite, and bundled/external OpenSSL in shared
  and static modes where available. External OpenSSL tests may reuse the
  installation produced by a validated bundled OpenSSL build through upstream's
  OPENSSL_ROOT_DIR configuration.
- Do not build/test with `--all-features`: `src` and `find` are mutually
  exclusive. Disable default features when selecting a conflicting explicit
  mode. Respect upstream's macOS restrictions on `find` and binding
  regeneration.
- Follow README.md and the existing CI platform setup. macOS builds need SDKROOT
  from `xcrun --sdk macosx --show-sdk-path`; the Make targets set it already.
  Linux static builds need NUMA development libraries, and binding regeneration
  needs libclang. Windows CI uses MSVC, Ninja, Strawberry Perl, JOM, and short
  dependency build paths. Keep consumer system-library linking in the smoke
  test's Windows helper rather than patching upstream.
- Inspect the actual CI job results. Queued runners are not build failures, and
  an enabled overwrite feature does not prove regeneration ran on a target where
  upstream skips it. Report the platforms and modes actually validated.

## Commits, tags, CI, and publication

- When asked for one commit per version, aggregate that version's changes and
  fixes into a single commit. Squash unpublished work when requested. Preserve
  existing published tags.
- For stable releases, use the upstream tag name (`vX.Y.Z`) for the wrapper tag,
  with X.Y.Z matching Cargo.toml. Review the complete per-version diff before
  tagging.
- Push master and the new tag atomically when authorized. Amend and force-push
  an unpublished release only within the user's authorization, using explicit
  expected old SHAs with `--force-with-lease` for branch and tag refs. A generic
  lease may reject an existing tag because it has no remote-tracking ref.
- Master pushes and tag pushes both run CI. New master pushes may cancel older
  master runs; tag runs remain independent. Monitor the tag run for each release
  and use the workflow's manifest-derived platform/feature matrix. Inspect runs
  with `gh run list` and `gh run view <run-id> --log-failed`.
- Stop the release sequence when a validation check fails. Fix actual build,
  test, packaging, or publication-integrity failures before advancing. A user
  may explicitly allow preparing later versions despite publication failures;
  that exception does not waive build and test validation.
- Publish through the existing crates.io trusted-publishing workflow. Do not
  substitute local tokens or manual publication for that workflow. Distinguish a
  pushed tag, passing smoke tests, successful publication, registry archive
  verification, and a GitHub release; one does not establish the others.
- crates.io can reject publication with HTTP 429 after too many versions of the
  same crate were published in a rolling 24-hour window, including earlier
  prereleases and yanked versions. Inspect the actual response and registry
  timestamps; do not assume a midnight reset or hard-code a quota here.
- A registry quota failure is not fixed by changing code, amending commits, or
  moving tags. Preserve the validated candidate and rerun **failed jobs** with
  `gh run rerun <run-id> --failed` after capacity frees up. Honor any requested
  publication order and do not claim publication succeeded or background retries
  were scheduled without evidence.
