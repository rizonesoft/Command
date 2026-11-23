cmake_minimum_required(VERSION 4.0)
project(Command LANGUAGES C)
include("${IMPOSSIBLE_SDK_DIR}/cmake/ImpossibleSDK.cmake")

set(COMMON common/out.c common/text.c common/cmdline.c common/fs.c common/lines.c)
set(HASHES common/hash/sha256.c common/hash/sha1.c common/hash/md5.c common/hash/crc32.c common/hash/blake3.c)

impossible_add_program(thash CONSOLE TIER 0 SOURCES src/thash.c ${COMMON} ${HASHES})
impossible_add_program(thex CONSOLE TIER 0 SOURCES src/thex.c ${COMMON})
impossible_add_program(tfind CONSOLE TIER 0 SOURCES src/tfind.c ${COMMON})
impossible_add_program(ttree CONSOLE TIER 0 SOURCES src/ttree.c ${COMMON})
impossible_add_program(tcmp CONSOLE TIER 0 SOURCES src/tcmp.c ${COMMON})
# A test helper that shows how the command line was split.
impossible_add_program(targs CONSOLE TIER 0 SOURCES src/targs.c ${COMMON})
