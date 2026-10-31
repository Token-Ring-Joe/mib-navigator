add_library(snmpb_libtomcrypt STATIC
    src/ciphers/aes/aes.c
    src/ciphers/des.c
    src/hashes/md5.c
    src/hashes/sha1.c
    src/hashes/sha2/sha256.c
    src/hashes/sha2/sha512.c
    src/misc/crypt/crypt.c
    src/misc/crypt/crypt_argchk.c
    src/misc/crypt/crypt_cipher_descriptor.c
    src/misc/crypt/crypt_cipher_is_valid.c
    src/misc/crypt/crypt_find_cipher.c
    src/misc/crypt/crypt_hash_descriptor.c
    src/misc/crypt/crypt_hash_is_valid.c
    src/misc/crypt/crypt_register_cipher.c
    src/misc/crypt/crypt_register_hash.c
    src/misc/zeromem.c
    src/modes/cbc/cbc_decrypt.c
    src/modes/cbc/cbc_done.c
    src/modes/cbc/cbc_encrypt.c
    src/modes/cbc/cbc_start.c
    src/modes/cfb/cfb_decrypt.c
    src/modes/cfb/cfb_done.c
    src/modes/cfb/cfb_encrypt.c
    src/modes/cfb/cfb_start.c
)

target_include_directories(snmpb_libtomcrypt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/headers
)

target_compile_definitions(snmpb_libtomcrypt
    PRIVATE
        LTC_SOURCE
)

if(MSVC)
    target_include_directories(snmpb_libtomcrypt
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc-compat
    )
    target_compile_definitions(snmpb_libtomcrypt
        PUBLIC
            LTC_NO_PROTOTYPES
    )
endif()

# The x86 rotate helpers in tomcrypt_macros.h use an "I" (compile-time constant)
# asm constraint, which GCC/Clang only satisfy after inlining. Unoptimized
# (Debug) builds do not inline, so use the portable C rotates there.
if(NOT WIN32)
    target_compile_definitions(snmpb_libtomcrypt
        PRIVATE
            $<$<CONFIG:Debug>:LTC_NO_ASM>
    )
endif()

if(WIN32)
    target_link_libraries(snmpb_libtomcrypt
        PRIVATE
            advapi32
    )
endif()
