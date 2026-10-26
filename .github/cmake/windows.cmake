# Keep Perl's unnormalized relative source paths below Windows MAX_PATH.
# Each Cargo build gets an isolated dependency directory under its test root.
file(TO_CMAKE_PATH "$ENV{MSQUIC_TEST_ROOT}" _test_root)
string(SHA256 _build_id "${CMAKE_BINARY_DIR}")
string(SUBSTRING "${_build_id}" 0 6 _build_id)
set(FETCHCONTENT_BASE_DIR "${_test_root}/${_build_id}" CACHE PATH "Short native dependency build directory")
