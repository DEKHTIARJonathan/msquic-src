#[cfg(test)]
mod tests {
    use std::ffi::CString;

    extern "C" {
        fn smoke_api() -> i32;
        fn smoke_loopback(
            cert: *const std::os::raw::c_char,
            key: *const std::os::raw::c_char,
            pfx: *const u8,
            pfx_length: u32,
        ) -> i32;
    }

    #[test]
    fn api_lifecycle_and_version() {
        let _registration =
            msquic_src::Registration::new(&msquic_src::RegistrationConfig::default()).unwrap();
        assert!(msquic_src::Api::get_tls_provider().is_ok());
        assert_eq!(unsafe { smoke_api() }, 0);
    }

    #[test]
    fn local_handshake_and_stream_echo() {
        let root = std::path::Path::new(env!("OUT_DIR"));
        let cert = CString::new(root.join("localhost.pem").to_str().unwrap()).unwrap();
        let key = CString::new(root.join("localhost-key.pem").to_str().unwrap()).unwrap();
        let pfx = std::fs::read(root.join("localhost.p12")).unwrap();
        assert_eq!(
            unsafe { smoke_loopback(cert.as_ptr(), key.as_ptr(), pfx.as_ptr(), pfx.len() as u32) },
            0
        );
    }
}
