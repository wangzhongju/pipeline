#!/usr/bin/env bash
# Unified native / RISC-V cross build helper.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    cat <<'EOF'
Usage:
  scripts/build.sh [options]

Options:
  -t, --target TARGET          native | riscv64. Default: native
      --type BUILD_TYPE        Release | Debug | RelWithDebInfo | MinSizeRel. Default: Release
  -j, --jobs N                 Parallel build jobs. Cross builds default to min(nproc, 4)
      --sysroot DIR            Pass -DCMAKE_SYSROOT=DIR
      --toolchain-root DIR     Pass RISC-V toolchain root
      --toolchain-prefix NAME  Pass RISC-V toolchain prefix
      --cmake-arg ARG          Pass an extra argument to CMake configure
      --clean                  Remove the selected build directory first
      --install                Run cmake --install after build
  -h, --help                   Show this help

Fixed build directories:
  native      build/
  riscv64     build-riscv64/

Examples:
  scripts/build.sh
  scripts/build.sh --type Debug
  scripts/build.sh --target riscv64 --sysroot /opt/riscv/sysroot --jobs 4
  scripts/build.sh --target riscv64 --sysroot /opt/riscv/sysroot --install
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

is_build_type() {
    case "$1" in
        Debug|Release|RelWithDebInfo|MinSizeRel) return 0 ;;
        *) return 1 ;;
    esac
}

TARGET="native"
BUILD_TYPE="Release"
JOBS=""
SYSROOT=""
TOOLCHAIN_ROOT=""
TOOLCHAIN_PREFIX=""
CLEAN=0
RUN_INSTALL=0
CMAKE_EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -t|--target)
            [[ $# -ge 2 ]] || die "--target requires a value"
            TARGET="$2"
            shift 2
            ;;
        --target=*)
            TARGET="${1#*=}"
            shift
            ;;
        --type)
            [[ $# -ge 2 ]] || die "--type requires a value"
            BUILD_TYPE="$2"
            shift 2
            ;;
        --type=*)
            BUILD_TYPE="${1#*=}"
            shift
            ;;
        -j|--jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            shift 2
            ;;
        --jobs=*)
            JOBS="${1#*=}"
            shift
            ;;
        --sysroot)
            [[ $# -ge 2 ]] || die "--sysroot requires a value"
            SYSROOT="$2"
            shift 2
            ;;
        --sysroot=*)
            SYSROOT="${1#*=}"
            shift
            ;;
        --toolchain-root)
            [[ $# -ge 2 ]] || die "--toolchain-root requires a value"
            TOOLCHAIN_ROOT="$2"
            shift 2
            ;;
        --toolchain-root=*)
            TOOLCHAIN_ROOT="${1#*=}"
            shift
            ;;
        --toolchain-prefix)
            [[ $# -ge 2 ]] || die "--toolchain-prefix requires a value"
            TOOLCHAIN_PREFIX="$2"
            shift 2
            ;;
        --toolchain-prefix=*)
            TOOLCHAIN_PREFIX="${1#*=}"
            shift
            ;;
        --cmake-arg)
            [[ $# -ge 2 ]] || die "--cmake-arg requires a value"
            CMAKE_EXTRA_ARGS+=("$2")
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --install)
            RUN_INSTALL=1
            shift
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

is_build_type "${BUILD_TYPE}" || die "unsupported build type: ${BUILD_TYPE}"

case "${TARGET}" in
    native)
        BUILD_DIR="${PROJECT_ROOT}/build"
        TOOLCHAIN_FILE=""
        ;;
    riscv64)
        BUILD_DIR="${PROJECT_ROOT}/build-riscv64"
        TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/riscv64-toolchain.cmake"
        ;;
    *)
        die "unsupported target: ${TARGET}"
        ;;
esac

INSTALL_PREFIX="${BUILD_DIR}/install"

if [[ -z "${JOBS}" ]]; then
    JOBS="$(nproc)"
    if [[ "${TARGET}" != "native" && "${JOBS}" -gt 4 ]]; then
        JOBS=4
    fi
fi
[[ "${JOBS}" =~ ^[0-9]+$ ]] || die "--jobs must be a positive integer"
[[ "${JOBS}" -gt 0 ]] || die "--jobs must be greater than zero"

CMAKE_CONFIGURE_ARGS=(
    -B "${BUILD_DIR}"
    -S "${PROJECT_ROOT}/src"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ -n "${TOOLCHAIN_FILE}" ]]; then
    CMAKE_CONFIGURE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
fi

if [[ -n "${SYSROOT}" ]]; then
    CMAKE_CONFIGURE_ARGS+=("-DCMAKE_SYSROOT=${SYSROOT}")
fi

case "${TARGET}" in
    riscv64)
        [[ -z "${TOOLCHAIN_ROOT}" ]] || CMAKE_CONFIGURE_ARGS+=("-D_RISCV_TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT}")
        [[ -z "${TOOLCHAIN_PREFIX}" ]] || CMAKE_CONFIGURE_ARGS+=("-D_RISCV_TOOLCHAIN_PREFIX=${TOOLCHAIN_PREFIX}")
        ;;
    native)
        if [[ -n "${SYSROOT}" ]]; then
            die "--sysroot requires a cross target"
        fi
        if [[ -n "${TOOLCHAIN_ROOT}" || -n "${TOOLCHAIN_PREFIX}" ]]; then
            die "--toolchain-root/--toolchain-prefix require a cross target"
        fi
        ;;
esac

CMAKE_CONFIGURE_ARGS+=("${CMAKE_EXTRA_ARGS[@]}")

echo "=================================================="
echo "  pipeline build"
echo "  TARGET         : ${TARGET}"
echo "  BUILD_TYPE     : ${BUILD_TYPE}"
echo "  BUILD_DIR      : ${BUILD_DIR}"
echo "  INSTALL_PREFIX : ${INSTALL_PREFIX}"
echo "  JOBS           : ${JOBS}"
if [[ -n "${TOOLCHAIN_FILE}" ]]; then
    echo "  TOOLCHAIN_FILE : ${TOOLCHAIN_FILE}"
fi
if [[ -n "${SYSROOT}" ]]; then
    echo "  SYSROOT        : ${SYSROOT}"
fi
echo "=================================================="

if [[ "${CLEAN}" -eq 1 ]]; then
    echo "Cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

cmake "${CMAKE_CONFIGURE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ "${RUN_INSTALL}" -eq 1 ]]; then
    cmake --install "${BUILD_DIR}"
fi

echo ""
echo "Build succeeded."
echo "Binary: ${BUILD_DIR}/bin/espl_launch"
if [[ "${RUN_INSTALL}" -eq 1 ]]; then
    echo "Install prefix: ${INSTALL_PREFIX}"
fi
