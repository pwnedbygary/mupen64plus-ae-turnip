#!/usr/bin/env bash
# Host regression: requires C compiler/linker, Node >=16, JDK >=12.
# No ROM, Android SDK, APK build, or device required. NODE/CC may override tools.
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
core=mupen64plus-core/upstream
"${NODE:-node}" tools/tests/wave-race-java.cjs "$work"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections \
  -I "$work/include" -I "$core/src" -I "$core/src/main" -I "$core/src/api" -I "$core/subprojects/md5" \
  tools/tests/wave-race-native.c "$core/src/main/util.c" \
  -Wl,--gc-sections -o "$work/native"
"$work/native" app/src/main/assets/mupen64plus_data/mupen64plus.ini
javac -d "$work/classes" $(find "$work/src" -name '*.java') \
  app/src/main/java/paulscode/android/mupen64plusae/persistent/ConfigFile.java \
  app/src/main/java/paulscode/android/mupen64plusae/util/RomDatabase.java \
  app/src/main/java/paulscode/android/mupen64plusae/util/CountryCode.java \
  app/src/main/java/paulscode/android/mupen64plusae/task/ExtractAssetsOrCleanupTask.java \
  tools/tests/wave-race-host.java
java -cp "$work/classes" WaveRaceHost "$work" app/src/main/assets