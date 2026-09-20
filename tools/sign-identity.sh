#!/bin/zsh
# Print the code-signing identity to use, or "-" for ad-hoc. Single source of
# truth for tools/build.sh and CMakeLists.txt.
#
# Why this is not a one-liner (both traps cost a session on 2026-09-18):
#
#  1. `security find-identity -v -p codesigning | head -1` is unsafe. This
#     keychain holds Apple Development certs from several teams, and the one
#     that sorts first is the *personal* team's (O=<personal team>,
#     OU=<personal-team>, CN "Apple Development: <personal-apple-id>"). Apps signed
#     with it refuse to launch:
#
#       open: ... NSPOSIXErrorDomain Code=162 "Launchd job spawn failed"
#
#     The Third Eye Technologies cert (O=Third Eye Technologies, Inc,
#     OU=BNT9H4C5D7) launches. Selecting the wrong one fails silently at build
#     time and only shows up as a refusal to start.
#
#  2. `-v` still lists REVOKED certificates, tagged CSSMERR_TP_CERT_REVOKED.
#
# The CN is not a usable selector: the personal cert carries the Apple ID email
# while the Third Eye cert carries the legal name, both under the same UID. So
# match on the certificate's Organization instead, which is stable.
#
# A real certificate keeps the cdhash stable across rebuilds, so the TCC
# microphone grant sticks; ad-hoc re-prompts every build. No provisioning
# profile is needed.
#
#   MOTU_SIGN_IDENTITY   use this identity verbatim ("-" forces ad-hoc)
#   MOTU_SIGN_ORG        organization to match (default "Third Eye Technologies")

SIGN_ORG="${MOTU_SIGN_ORG:-Third Eye Technologies}"

if [[ -n "${MOTU_SIGN_IDENTITY:-}" ]]; then
  print -r -- "$MOTU_SIGN_IDENTITY"
  exit 0
fi

typeset -a candidates
candidates=(${(f)"$(security find-identity -v -p codesigning 2>/dev/null \
                    | grep -v CSSMERR \
                    | sed -n 's/^ *[0-9]*) [0-9A-F]* "\(.*\)"$/\1/p')"})

for c in "${candidates[@]}"; do
  subject=$(security find-certificate -c "$c" -p 2>/dev/null \
            | openssl x509 -noout -subject 2>/dev/null)
  if [[ "$subject" == *"$SIGN_ORG"* ]]; then
    print -r -- "$c"
    exit 0
  fi
done

# No organization match: ad-hoc is safer than an arbitrary certificate.
print -r -- "-"
