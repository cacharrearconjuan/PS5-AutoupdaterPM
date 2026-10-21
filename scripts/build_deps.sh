#!/usr/bin/env bash
# Instala el PS5 Payload SDK y compila mbedTLS + libcurl estáticos para PS5.
# Lo usa GitHub Actions (con caché), pero también vale en local:
#
#   export PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk
#   ./scripts/build_deps.sh
#
# Las versiones se pueden cambiar con variables de entorno.
set -euo pipefail

: "${PS5_PAYLOAD_SDK:?Define PS5_PAYLOAD_SDK (ruta donde instalar el SDK)}"
SDK_VERSION="${SDK_VERSION:-v0.44}"
MBEDTLS_VERSION="${MBEDTLS_VERSION:-3.6.4}"
CURL_VERSION="${CURL_VERSION:-8.18.0}"
JOBS="$(nproc 2>/dev/null || echo 2)"

WORK="$(mktemp -d)"
trap 'rm -rf -- "$WORK"' EXIT
cd "$WORK"

echo "=== 1/3 PS5 Payload SDK ${SDK_VERSION} ==="
curl -fsSL -o sdk.zip \
  "https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip"
unzip -q sdk.zip -d sdk
rm -rf "$PS5_PAYLOAD_SDK"
mkdir -p "$(dirname "$PS5_PAYLOAD_SDK")"
mv sdk/ps5-payload-sdk "$PS5_PAYLOAD_SDK"

export PATH="$PS5_PAYLOAD_SDK/bin:$PATH"
TARGET="$PS5_PAYLOAD_SDK/target"

echo "=== 2/3 mbedTLS ${MBEDTLS_VERSION} ==="
curl -fsSL -o mbedtls.tar.bz2 \
  "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MBEDTLS_VERSION}/mbedtls-${MBEDTLS_VERSION}.tar.bz2"
tar xjf mbedtls.tar.bz2
(
  cd "mbedtls-${MBEDTLS_VERSION}"
  make CC=prospero-clang AR=prospero-ar RANLIB=prospero-ranlib CFLAGS="-Os" lib -j"$JOBS"
  mkdir -p "$TARGET/include" "$TARGET/lib"
  cp -r include/mbedtls include/psa "$TARGET/include/"
  cp library/libmbedtls.a library/libmbedx509.a library/libmbedcrypto.a "$TARGET/lib/"
)

echo "=== 3/3 libcurl ${CURL_VERSION} ==="
CURL_TAG="curl-${CURL_VERSION//./_}"
curl -fsSL -o curl.tar.xz \
  "https://github.com/curl/curl/releases/download/${CURL_TAG}/curl-${CURL_VERSION}.tar.xz"
tar xJf curl.tar.xz
(
  cd "curl-${CURL_VERSION}"
  # La PS5 no tiene xattr; evita que la herramienta de línea de comandos lo use
  sed -i 's|define USE_XATTR| |g' src/tool_xattr.h
  CC=prospero-clang AR=prospero-ar RANLIB=prospero-ranlib \
  ./configure --host=x86_64-pc-freebsd \
              --prefix="$TARGET" \
              --enable-static --disable-shared \
              --with-mbedtls="$TARGET" --without-openssl \
              --without-libpsl --without-zlib --without-brotli --without-zstd \
              --without-libidn2 --without-nghttp2 --without-libssh2 \
              --without-ca-bundle --without-ca-path \
              --disable-docs --disable-manual \
              --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp \
              --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
              --disable-imap --disable-smb --disable-smtp --disable-gopher \
              --disable-mqtt --disable-file
  make -j"$JOBS"
  make install
)

echo "SDK y dependencias listos en $PS5_PAYLOAD_SDK"
