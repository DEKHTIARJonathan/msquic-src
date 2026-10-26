SHELL := /bin/bash -eu -o pipefail
VERSION := $(shell python3 -c 'import tomllib; print(tomllib.load(open("Cargo.toml", "rb"))["package"]["version"])')
UPSTREAM_VERSION := $(firstword $(subst -, ,$(VERSION)))

.PHONY: setup clean build test package check-version update

setup:
	curl -L --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/cargo-bins/cargo-binstall/v1.26.1/install-from-binstall-release.sh | BINSTALL_VERSION=1.26.1 bash
	cargo binstall --locked --no-confirm prek@0.5.5 cargo-audit@0.22.2
	prek prepare-hooks

clean:
	rm -rf -- target dist testcrate/target

check-version:
	python3 -c 'import tomllib; a=tomllib.load(open("Cargo.toml","rb"))["package"]["version"]; b=tomllib.load(open("msquic/Cargo.toml","rb"))["package"]["version"]; assert a.split("-")[0] == b.split("-")[0], (a,b)'

build: check-version
	if command -v xcrun >/dev/null; then export SDKROOT="$$(xcrun --sdk macosx --show-sdk-path)"; fi; \
	cargo build --locked

test: check-version
	if command -v xcrun >/dev/null; then export SDKROOT="$$(xcrun --sdk macosx --show-sdk-path)"; fi; \
	MSQUIC_EXPECTED_VERSION='$(UPSTREAM_VERSION)' cargo test --manifest-path testcrate/Cargo.toml -- --test-threads=1

package: check-version
	test -z "$$(git -C msquic status --porcelain --untracked-files=all)"
	test "$$(git -C msquic rev-parse HEAD)" = "$$(git -C msquic rev-parse 'v$(UPSTREAM_VERSION)^{commit}')"
	rm -rf target/package-src
	mkdir -p target/package-src/msquic/submodules
	cp Cargo.toml Cargo.lock build.rs README.md LICENSE.md target/package-src/
	cp -R src target/package-src/
	git -C msquic archive HEAD CMakeLists.txt LICENSE THIRD-PARTY-NOTICES cmake scripts/build.rs src/core src/platform src/inc src/bin src/generated src/manifest src/rs submodules/CMakeLists.txt | tar -xf - -C target/package-src/msquic
	find target/package-src/msquic -name '*.pgd' -delete
	for provider in quictls openssl; do \
	  if test -n "$$(git -C msquic ls-tree HEAD -- submodules/$$provider)"; then \
	    repository="msquic/submodules/$$provider"; \
	    paths="$$(git -C "$$repository" ls-tree --name-only HEAD | grep -Ev '^(doc|test|demos|\.github|fuzz|pyca-cryptography|wycheproof|boringssl|krb5|gost-engine|tlslite-ng|tlsfuzzer|python-ecdsa)$$')"; \
	    mkdir -p "target/package-src/$$repository"; \
	    git -C "$$repository" show HEAD:LICENSE.txt > "target/package-src/$$repository/LICENSE.txt"; \
	    info="$$(git -C "$$repository" ls-tree -r --name-only HEAD -- doc test demos fuzz | grep -E '/build\.info$$')"; \
	    git -C "$$repository" archive --prefix="submodules/$$provider/" HEAD -- $$paths $$info; \
	  fi; \
	done | xz -9 > target/package-src/msquic/submodules/tls-sources.tar.xz
	mkdir -p target/package-src/msquic/submodules/xdp-for-windows
	git -C msquic/submodules/xdp-for-windows archive HEAD published/external LICENSE | tar -xf - -C target/package-src/msquic/submodules/xdp-for-windows
	cargo package --manifest-path target/package-src/Cargo.toml --locked --no-verify
	mkdir -p dist
	cp target/package-src/target/package/msquic-src-$(VERSION).crate dist/
	python3 -c 'from pathlib import Path; p=Path("dist/msquic-src-$(VERSION).crate"); size=p.stat().st_size; print(f"{p}: {size:,} bytes"); assert size < 10_000_000, "crate exceeds 10 MB"'

update:
	cargo update --manifest-path Cargo.toml
	cargo update --manifest-path testcrate/Cargo.toml
	cargo outdated --workspace --depth 1
