// Consumer-side Windows linking. This file is not part of the published crate.
pub fn configure(static_link: bool) {
    if std::env::var("CARGO_CFG_TARGET_ENV").as_deref() != Ok("msvc") || !static_link {
        return;
    }
    // Supply native system dependencies for upstream builds that omit them.
    // Repeating import libraries emitted by upstream is harmless.
    for library in [
        "ws2_32", "ntdll", "bcrypt", "ncrypt", "crypt32", "iphlpapi", "advapi32", "schannel",
        "user32",
    ] {
        println!("cargo:rustc-link-lib={library}");
    }
}
