#!/usr/bin/env bash
set -euo pipefail

IDENTITY_NAME="${1:-BJ LED Ambilight Local}"
P12_PASSWORD="bj-led-local"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

if security find-identity -v -p codesigning | grep -Fq "$IDENTITY_NAME"; then
  echo "Code signing identity already exists: $IDENTITY_NAME"
  exit 0
fi

cat > "$TMP_DIR/cert.conf" <<EOF
[ req ]
distinguished_name = dn
x509_extensions = v3_req
prompt = no

[ dn ]
CN = $IDENTITY_NAME

[ v3_req ]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = codeSigning
subjectKeyIdentifier = hash
EOF

openssl req \
  -new \
  -newkey rsa:2048 \
  -nodes \
  -x509 \
  -days 3650 \
  -config "$TMP_DIR/cert.conf" \
  -keyout "$TMP_DIR/key.pem" \
  -out "$TMP_DIR/cert.pem"

openssl pkcs12 \
  -export \
  -legacy \
  -name "$IDENTITY_NAME" \
  -inkey "$TMP_DIR/key.pem" \
  -in "$TMP_DIR/cert.pem" \
  -out "$TMP_DIR/identity.p12" \
  -passout "pass:$P12_PASSWORD"

security import "$TMP_DIR/identity.p12" \
  -k "$HOME/Library/Keychains/login.keychain-db" \
  -P "$P12_PASSWORD" \
  -T /usr/bin/codesign

security add-trusted-cert \
  -r trustRoot \
  -p codeSign \
  -k "$HOME/Library/Keychains/login.keychain-db" \
  "$TMP_DIR/cert.pem" >/dev/null 2>&1 || true

security set-key-partition-list \
  -S apple-tool:,apple:,codesign: \
  -s \
  -k "" \
  "$HOME/Library/Keychains/login.keychain-db" >/dev/null 2>&1 || true

echo "Created code signing identity: $IDENTITY_NAME"
