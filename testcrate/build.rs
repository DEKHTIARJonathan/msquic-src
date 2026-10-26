mod windows;

fn main() {
    println!("cargo:rerun-if-changed=smoke.c");
    println!("cargo:rerun-if-env-changed=MSQUIC_EXPECTED_VERSION");
    println!("cargo:rerun-if-env-changed=MSQUIC_TEST_WINDOWS_STATIC");
    windows::configure(std::env::var("MSQUIC_TEST_WINDOWS_STATIC").as_deref() == Ok("1"));
    let expected = std::env::var("MSQUIC_EXPECTED_VERSION")
        .expect("set MSQUIC_EXPECTED_VERSION to the upstream native version");
    let components: Vec<_> = expected.split('.').collect();
    assert_eq!(components.len(), 3);
    let include = std::env::var_os("MSQUIC_INCLUDE_DIR")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../msquic/src/inc")
        });
    cc::Build::new()
        .file("smoke.c")
        .include(include)
        .define("QUIC_BUILD_STATIC", None)
        .define("_GNU_SOURCE", None)
        .define("EXPECTED_MAJOR", Some(components[0]))
        .define("EXPECTED_MINOR", Some(components[1]))
        .define("EXPECTED_PATCH", Some(components[2]))
        .flag_if_supported("-std=c11")
        .flag_if_supported("-fms-extensions")
        .compile("msquic_src_smoke");
    let fixtures = std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap());
    let status = std::process::Command::new("openssl")
        .args([
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "2",
            "-subj",
            "/CN=localhost",
            "-keyout",
        ])
        .arg(fixtures.join("localhost-key.pem"))
        .arg("-out")
        .arg(fixtures.join("localhost.pem"))
        .output()
        .expect("the smoke tests require the openssl CLI to generate a temporary certificate");
    assert!(
        status.status.success(),
        "could not generate test certificate: {}",
        String::from_utf8_lossy(&status.stderr)
    );
    let status = std::process::Command::new("openssl")
        .args(["pkcs12", "-export", "-passout", "pass:", "-inkey"])
        .arg(fixtures.join("localhost-key.pem"))
        .arg("-in")
        .arg(fixtures.join("localhost.pem"))
        .arg("-out")
        .arg(fixtures.join("localhost.p12"))
        .output()
        .expect("generate the Schannel certificate fixture");
    assert!(
        status.status.success(),
        "{}",
        String::from_utf8_lossy(&status.stderr)
    );
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        println!("cargo:rustc-link-lib=crypt32");
    }
}
