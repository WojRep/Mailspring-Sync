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

echo "== append-message-test (#325)"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/append-message-test.cpp" \
  "${SUPPORT[@]}" -lcurl -o "$OUT/append-message-test" && "$OUT/append-message-test" || status=1

echo "== mail-selection-test (#325)"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/mail-selection-test.cpp" -o "$OUT/mail-selection-test" \
  && "$OUT/mail-selection-test" || status=1

SQLITECPP=("$ROOT/Vendor/SQLiteCpp/src/Database.cpp" "$ROOT/Vendor/SQLiteCpp/src/Statement.cpp"
  "$ROOT/Vendor/SQLiteCpp/src/Column.cpp" "$ROOT/Vendor/SQLiteCpp/src/Exception.cpp" "$ROOT/Vendor/SQLiteCpp/src/Backup.cpp")

echo "== query-test (#327)"
clang++ "${CXXFLAGS[@]}" -DSQLITE_OMIT_LOAD_EXTENSION -I"$ROOT/MailSync/Models" -I"$ROOT/Vendor/SQLiteCpp/include" "$ROOT/Tests/query-test.cpp" "$ROOT/MailSync/Query.cpp" \
  "$ROOT/Tests/stubs/mailutils-qmarks.cpp" "${SQLITECPP[@]}" "${SUPPORT[@]}" -lsqlite3 -lcurl \
  -o "$OUT/query-test" && "$OUT/query-test" || status=1

echo "== uid-reconcile-test (#327)"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/uid-reconcile-test.cpp" -o "$OUT/uid-reconcile-test" \
  && "$OUT/uid-reconcile-test" || status=1

echo "== dav-auth-policy-test (bezpieczeństwo poświadczeń CalDAV)"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/dav-auth-policy-test.cpp" -o "$OUT/dav-auth-policy-test" \
  && "$OUT/dav-auth-policy-test" || status=1

echo "== dav-discovery-outcome-test (#341 serwer bez CalDAV)"
clang++ "${CXXFLAGS[@]}" "$ROOT/Tests/dav-discovery-outcome-test.cpp" -o "$OUT/dav-discovery-outcome-test" \
  && "$OUT/dav-discovery-outcome-test" || status=1

exit $status
