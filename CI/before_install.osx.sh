#!/bin/sh -ex

export HOMEBREW_NO_EMOJI=1
export HOMEBREW_NO_INSTALL_CLEANUP=1
export HOMEBREW_AUTOREMOVE=1

# purge large and unnecessary packages that get in our way
brew uninstall ruby php openjdk node postgresql maven google-cloud-sdk || true

# purge things pre-installed that cause issues
brew uninstall curl # aom cairo httpd jpeg-xl libavif
brew uninstall xquartz gd fontconfig freetype harfbuzz brotli

brew tap --repair
brew update --quiet

# Some of these tools can come from places other than brew, so check before installing
brew install xquartz gd fontconfig freetype harfbuzz brotli

# Fix: can't open file: @loader_path/libbrotlicommon.1.dylib (No such file or directory)
# TODO: this is also now broke :()
#BREW_LIB_PATH="$(brew --prefix)/lib"
#install_name_tool -change "@loader_path/libbrotlicommon.1.dylib" "${BREW_LIB_PATH}/libbrotlicommon.1.dylib" ${BREW_LIB_PATH}/libbrotlidec.1.dylib
#install_name_tool -change "@loader_path/libbrotlicommon.1.dylib" "${BREW_LIB_PATH}/libbrotlicommon.1.dylib" ${BREW_LIB_PATH}/libbrotlienc.1.dylib

command -v ccache >/dev/null 2>&1 || brew install ccache
command -v cmake >/dev/null 2>&1 || brew install cmake
command -v qmake >/dev/null 2>&1 || brew install qt@5
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"

# try to find fontconfig
find /opt | grep -i freetype
#mkdir -p /opt/homebrew/opt/fontconfig/lib/
#/opt/homebrew/Cellar/freetype/2.13.2.reinstall/lib/libfreetype.dylib



# Install deps
brew install icu4c yaml-cpp sqlite

ccache --version
cmake --version
qmake --version

if [[ "${MACOS_AMD64}" ]]; then
    curl -fSL -R -J https://gitlab.com/OpenMW/openmw-deps/-/raw/main/macos/openmw-deps-20221113.zip -o ~/openmw-deps.zip
else
    curl -fSL -R -J https://gitlab.com/OpenMW/openmw-deps/-/raw/main/macos/openmw-deps-20231022_arm64.zip -o ~/openmw-deps.zip
fi

unzip -o ~/openmw-deps.zip -d /tmp > /dev/null

