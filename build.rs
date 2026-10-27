use std::{env, fs, path::Path};

mod upstream {
    include!("msquic/scripts/build.rs");

    pub fn build() {
        main();
    }
}

fn main() {
    let root = env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR");
    let root = Path::new(&root);
    let out = env::var_os("OUT_DIR").expect("OUT_DIR");
    let staging = Path::new(&out).join("upstream");
    println!("cargo:rerun-if-changed=msquic");
    if staging.exists() {
        fs::remove_dir_all(&staging).expect("remove previous build sources");
    }
    let compressed = root.join("msquic/submodules/tls-sources.tar.xz");
    if compressed.is_file() {
        let input = std::io::BufReader::new(fs::File::open(compressed).expect("open TLS sources"));
        let decoder = lzma_rust2::XzReader::new(input, false);
        let mut archive = tar::Archive::new(decoder);
        archive.set_ignore_zeros(true);
        archive
            .unpack(&staging)
            .expect("unpack pristine TLS sources");
        std::io::copy(&mut archive.into_inner(), &mut std::io::sink())
            .expect("verify compressed TLS stream");
    }
    copy_sources(&root.join("msquic"), &staging);
    // Upstream's build script and overwrite feature may generate source files.
    // Give them an isolated checkout layout without changing any build choices.
    env::set_var("CARGO_MANIFEST_DIR", &staging);
    env::set_current_dir(&staging).expect("enter build source directory");
    upstream::build();
}

fn copy_sources(source: &Path, destination: &Path) {
    fs::create_dir_all(destination).expect("create build source directory");
    for entry in fs::read_dir(source).expect("read upstream sources") {
        let entry = entry.expect("read source entry");
        let name = entry.file_name();
        if name == ".git" || name == "tls-sources.tar.xz" {
            continue;
        }
        let output = destination.join(&name);
        if entry.path().is_dir() {
            copy_sources(&entry.path(), &output);
        } else if !output.exists() {
            fs::copy(entry.path(), output).expect("copy upstream source");
        }
    }
}
