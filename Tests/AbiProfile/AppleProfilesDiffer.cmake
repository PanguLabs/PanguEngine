cmake_minimum_required(VERSION 4.0)

foreach(_key IN ITEMS PANGU_MACOS_PROOF PANGU_IOS_PROOF)
    if(NOT DEFINED ${_key})
        message(FATAL_ERROR "Missing ${_key}")
    endif()
endforeach()
foreach(_name IN ITEMS MACOS IOS)
    set(_dir "${PANGU_${_name}_PROOF}")
    foreach(_file IN ITEMS verified-profile.json verified-fingerprint.sha256)
        if(NOT EXISTS "${_dir}/${_file}")
            message(FATAL_ERROR "Missing verified Apple collector output: ${_dir}/${_file}")
        endif()
    endforeach()
    file(READ "${_dir}/verified-profile.json" _profile)
    file(READ "${_dir}/verified-fingerprint.sha256" _digest)
    string(JSON _triple GET "${_profile}" target triple)
    string(JSON _sdk GET "${_profile}" runtime sdk_abi_id)
    string(JSON _stdlib GET "${_profile}" stdlib version)
    string(LENGTH "${_digest}" _digest_length)
    if(NOT _digest_length EQUAL 64 OR NOT _digest MATCHES "^[a-f0-9]+$")
        message(FATAL_ERROR "Apple digest was not a 64-character lowercase SHA-256")
    endif()
    set("_profile_${_name}" "${_profile}")
    set("_digest_${_name}" "${_digest}")
    set("_triple_${_name}" "${_triple}")
    set("_sdk_${_name}" "${_sdk}")
    set("_stdlib_${_name}" "${_stdlib}")
endforeach()
if(NOT _triple_MACOS STREQUAL "x86_64-apple-macosx14.0.0"
   OR NOT _triple_IOS STREQUAL "arm64-apple-ios18.0.0"
   OR NOT _sdk_MACOS STREQUAL "macos-sdk-15.2"
   OR NOT _sdk_IOS STREQUAL "ios-sdk-18.2"
   OR NOT _stdlib_MACOS STREQUAL "23.1.2"
   OR NOT _stdlib_IOS STREQUAL "23.1.2"
   OR _profile_MACOS STREQUAL _profile_IOS
   OR _digest_MACOS STREQUAL _digest_IOS)
    message(FATAL_ERROR "Apple compiler/SDK ABI Profiles unexpectedly agree or are malformed")
endif()
message(STATUS "macOS and iOS C++ Profiles / fingerprints are distinct and SDK-grounded.")
