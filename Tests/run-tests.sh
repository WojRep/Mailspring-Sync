#!/bin/bash
# Minimalne testy C++ mailsync (macOS). Buduje każdy test jako osobny program
# z potrzebnymi źródłami i od razu go uruchamia. Poza globem `MailSync/*.[ch]pp`
# z CMakeLists.txt, więc nie trafia do celu `mailsync`.
#
# Wymaga zbudowanych nagłówków MailCore (Vendor/mailcore2/build-mac/build/Release/include)
# — powstają przy zwykłym buildzie mailsync.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/mailsync-tests"
mkdir -p "$OUT"
SDK="$(xcrun --show-sdk-path)"
S="$ROOT/Vendor/StanfordCPPLib"

CXXFLAGS=(-std=c++17 -w
  -I"$ROOT/MailSync" -I"$ROOT/Vendor" -I"$ROOT/Vendor/nlohmann" -I"$S"
  -I"$ROOT/Vendor/mailcore2/build-mac/build/Release/include"
  -I"$ROOT/Vendor/libetpan/include"
  -I"$SDK/usr/include/libxml2")

# Zależności SyncException (GenericException → stos wywołań StanfordCPPLib).
SUPPORT=("$ROOT/MailSync/SyncException.cpp" "$ROOT/MailSync/GenericException.cpp"
  "$S/stacktrace/call_stack_gcc.cpp" "$S/exceptions.cpp" "$S/error.cpp"
  "$S/filelib.cpp" "$S/strlib.cpp" "$S/platform.cpp" "$S/simpio.cpp")

status=0

echo "== davxml-exception-test"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/davxml-exception-test.cpp" "$ROOT/MailSync/DavXML.cpp" \
  "${SUPPORT[@]}" -lxml2 -lcurl -o "$OUT/davxml-exception-test"
"$OUT/davxml-exception-test" 2>/dev/null || status=1

exit $status
