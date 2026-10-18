#!/usr/bin/env bash
#
# GlideMix: makes the local code signing certificate the build looks for.
#
# Why this exists: macOS remembers a permission grant -- the Documents folder,
# the microphone -- against the app's code signing identity. An ad-hoc
# signature has no stable identity, so every rebuild looks like a new app,
# every prompt comes back, and the app sits there not starting until someone
# answers it. A certificate that stays the same across rebuilds fixes that.
#
# The certificate is self-signed and lives in the login keychain. It is for
# signing local builds on this machine and nothing else: it is not trusted as
# a root, it does not go near the system trust store, and it means nothing to
# anyone else's Mac. Distribution builds still need a real Developer ID.
#
# Run it once:
#     buildscripts/tools/macos/make_signing_certificate.sh
#
# Then configure the build again so CMake picks the certificate up.

set -euo pipefail

NAME="${1:-GlideMix Local Dev}"
KEYCHAIN="${HOME}/Library/Keychains/login.keychain-db"

if security find-certificate -c "${NAME}" >/dev/null 2>&1; then
    echo "\"${NAME}\" is already in the keychain; nothing to do."
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cat > "${WORK}/cert.cnf" <<EOF
[ req ]
default_bits       = 2048
distinguished_name = dn
x509_extensions    = codesign
prompt             = no

[ dn ]
CN = ${NAME}

[ codesign ]
basicConstraints     = critical,CA:false
keyUsage             = critical,digitalSignature
extendedKeyUsage     = critical,codeSigning
subjectKeyIdentifier = hash
EOF

openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -config "${WORK}/cert.cnf" \
    -keyout "${WORK}/cert.key" -out "${WORK}/cert.crt" 2>/dev/null

# A throwaway passphrase for the transfer file only; the older ciphers are
# what the macOS importer understands.
PASSPHRASE="$(openssl rand -hex 16)"
openssl pkcs12 -export \
    -inkey "${WORK}/cert.key" -in "${WORK}/cert.crt" \
    -out "${WORK}/cert.p12" -name "${NAME}" \
    -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1 \
    -passout "pass:${PASSPHRASE}" 2>/dev/null

security import "${WORK}/cert.p12" -k "${KEYCHAIN}" -P "${PASSPHRASE}" \
    -T /usr/bin/codesign -T /usr/bin/security

echo "Made \"${NAME}\". Configure the build again to sign with it."
