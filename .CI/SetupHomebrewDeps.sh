#!/usr/bin/env bash

set -e

# c-v-pasted from .zsh_commands
c2-make-universal-dylib() {
    # Prefix for where to find the ARM64 library
    local _arm64_homebrew="/opt/homebrew"

    # Prefix for where to find the x86 library
    local _x86_64_homebrew="/opt/homebrew-x86_64"

    # Prefix we use to override the library (should be the base systems directory)
    local _override_homebrew="${_arm64_homebrew}"

    # Directory where we place the finished universal library
    local _universal_lib="/opt/universal-lib"

    local _input_lib="$1"
    if [ -z "${_input_lib}" ]; then
        echo "usage: $0 [lib-path-relative-to-homebrew] (e.g. $0 lib/libboost_random-mt.dylib)"
        return
    fi

    if [ ! -d "${_arm64_homebrew}" ]; then
        echo "error: The ARM64 homebrew directory (${_arm64_homebrew}) does not exist"
        return
    fi
    if [ ! -d "${_x86_64_homebrew}" ]; then
        echo "error: The x86_64 homebrew directory (${_x86_64_homebrew}) does not exist"
        return
    fi
    if [ ! -d "${_universal_lib}" ]; then
        echo "error: The universal lib directory (${_universal_lib}) does not exist"
        return
    fi

    if [ ! -w "${_universal_lib}" ]; then
        echo "error: The current user does not have write permission in the universal lib directory (${_universal_lib})"
        return
    fi

    local _input_lib_filename="$(basename "${_input_lib}")"

    local _arm64_lib="${_arm64_homebrew}/${_input_lib}"
    local _x86_64_lib="${_x86_64_homebrew}/${_input_lib}"
    local _override_lib=$(realpath "${_override_homebrew}/${_input_lib}")

    local _universal_lib="${_universal_lib}/${_input_lib_filename}"

    if [ -f "${_universal_lib}" ]; then
        echo "warning: The final output path (${_universal_lib}) already exists, you might run into errors"
        echo "Some of these errors can be solved by re-linking the original libraries (e.g. brew link --overwrite boost), or re-installing them (e.g. brew reinstall openssl@1.1)"
        echo ""
    fi

    if [ ! -f "${_arm64_lib}" ]; then
        echo "error: The ARM64 library cannot be found at '${_arm64_lib}' (combined prefix '${_arm64_homebrew}' with '${_input_lib})"
        return
    fi

    if [ ! -f "${_x86_64_lib}" ]; then
        echo "error: The x86_64 library cannot be found at '${_x86_64_lib}' (combined prefix '${_arm64_homebrew}' with '${_input_lib})"
        return
    fi

    # Create the combined library
    if ! lipo "${_arm64_lib}" "${_x86_64_lib}" -create -output "${_universal_lib}"; then
        echo "error: Something went wrong creating the combined library"
        echo "Some errors can be solved by re-linking the original libraries (e.g. brew link --overwrite boost)"
        return
    fi

    echo "Created the combined library at '${_universal_lib}"

    # Override
    rm -v "${_override_lib}"
    ln -v -s "${_universal_lib}" "${_override_lib}"
}

sudo mkdir /opt/homebrew-x86_64
sudo mkdir /opt/universal-lib

sudo chown -R $USER /opt/universal-lib

echo "Installing x86_64 brew"
sudo curl -L https://github.com/Homebrew/brew/tarball/master | sudo tar xz --strip 1 -C /opt/homebrew-x86_64
sudo chown -R $USER /opt/homebrew-x86_64

echo "Installing ARM dependencies"
brew install "$@"

echo "Installing x86_64 dependencies"
for dep in "$@"
do
    arch -x86_64 /opt/homebrew-x86_64/bin/brew fetch --force --bottle-tag=x86_64_monterey "$dep"
    arch -x86_64 /opt/homebrew-x86_64/bin/brew install $(arch -x86_64 /opt/homebrew-x86_64/bin/brew --cache --bottle-tag=x86_64_monterey "$dep")
done

echo "Unlink & link libraries"
brew unlink "$@" || true
brew link --overwrite "$@"

echo "Unlink & link x86_64"
arch -x86_64 /opt/homebrew-x86_64/bin/brew unlink "$@" || true
arch -x86_64 /opt/homebrew-x86_64/bin/brew link --overwrite "$@"

echo "Relinking boost libraries"
c2-make-universal-dylib lib/libboost_random-mt.dylib

echo "Relinking OpenSSL 3 libcrypto"
c2-make-universal-dylib lib/libcrypto.dylib
echo "Relinking OpenSSL 3 libssl"
c2-make-universal-dylib lib/libssl.dylib
