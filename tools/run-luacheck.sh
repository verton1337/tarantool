#!/bin/sh

set -eux

SOURCE_DIR="${1:-}"
BUILD_DIR="${2:-}"

if [ -z "${SOURCE_DIR}" ] || [ -z "${BUILD_DIR}" ]; then
    printf "Usage: ${0} /path/to/the/source/tree /path/to/the/build/directory\n"
    exit 1
fi

if ! type luacheck; then
    printf "Unable to find luacheck\n"
    exit 1
fi

# Workaround luacheck behaviour around the `include_files`
# configuration option, a current working directory and a
# directory path passed as an argument.
#
# https://github.com/mpeterv/luacheck/issues/208
#
# If we'll just invoke the following command under the
# `make luacheck` target:
#
#  | luacheck --codes                                 \
#  |     --config "${PROJECT_SOURCE_DIR}/.luacheckrc" \
#  |     "${PROJECT_SOURCE_DIR}"
#
# The linter will fail to find Lua sources in the following cases:
#
# 1. In-source build. The current working directory is not a
#    real path (contains components, which are symlinks).
# 2. Out-of-source build.
#
# It seems, the only reliable way to verify sources is to change
# the current directory prior to the luacheck call.
cd "${SOURCE_DIR}"

# Exclude the build directory if it is under the source directory.
#
# Except the case, when the build directory is the same as the
# source directory.
#
# We lean on the following assumptions:
#
# 1. "${SOURCE_DIR}" and "${BUILD_DIR}" have no the trailing slash.
# 2. "${SOURCE_DIR}" and "${BUILD_DIR}" are either real paths
#    (with resolved symlink components) or absolute paths with the
#    same symlink components (where applicable).
#
# Those assumptions should be true when the variables are passed
# from the CMake variables "${PROJECT_SOURCE_DIR}" and
# "${PROJECT_BINARY_DIR}".
#
# When the prerequisites are hold true, the EXCLUDE_FILES pattern
# will be relative to the "${SOURCE_DIR}" and luacheck will work
# as expected.
EXCLUDE_FILES=""
case "${BUILD_DIR}" in
"${SOURCE_DIR}/"*)
    EXCLUDE_FILES="${BUILD_DIR#"${SOURCE_DIR}/"}/**/*.lua"
    ;;
esac

if [ -z "${EXCLUDE_FILES}" ]; then
    luacheck --codes --config .luacheckrc .
else
    luacheck --codes --config .luacheckrc . --exclude-files "${EXCLUDE_FILES}"
fi
