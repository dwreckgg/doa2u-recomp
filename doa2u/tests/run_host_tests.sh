#!/usr/bin/env bash
# Runs every built host test (doa2u/build/tests/Release/*_test.exe). Exit code is
# the number of failing programs. Build first: cmake --build doa2u/build --config Release --target host_tests
cd "$(dirname "$0")/../build/tests/Release" || exit 99
fail=0
for exe in *_test.exe; do
  [ -e "$exe" ] || { echo "no test executables built"; exit 98; }
  out=$("./$exe" 2>&1); code=$?
  printf '%-24s exit %d :: %s\n' "$exe" "$code" "$(echo "$out" | tr '\n' '|')"
  [ "$code" -eq 0 ] || fail=$((fail+1))
done
echo "host tests: $fail failing"
exit $fail
