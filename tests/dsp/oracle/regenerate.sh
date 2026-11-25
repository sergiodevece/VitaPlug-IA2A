#!/bin/sh
set -eu
# Usage: regenerate.sh /path/to/LAB2C /path/to/JUCE
source_repo=${1:?LAB2C repository required}
juce_dir=${2:?JUCE checkout required}
oracle_dir=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
fixture_dir="$oracle_dir/../../fixtures/lab2c"
revision=32ca42ba9bc7f4f925a82d1697b460ece57701b5
test "$(git -C "$juce_dir" rev-parse HEAD)" = 72782788ce18c2d4d760b28e0921d6ffc6431102
stage=$(mktemp -d)
echo "Isolated build: $stage"
git -C "$source_repo" archive -o "$stage/reference.tar" "$revision"
mkdir "$stage/source"
tar -xf "$stage/reference.tar" -C "$stage/source"
cp "$oracle_dir/Generate.cpp" "$stage/source/Tests/"
cp "$oracle_dir/../Scenario.h" "$stage/source/Scenario.h"
cat >> "$stage/source/CMakeLists.txt" <<'CMAKE'
juce_add_console_app(ParityOracle PRODUCT_NAME "ParityOracle")
target_sources(ParityOracle PRIVATE Tests/Generate.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(ParityOracle PRIVATE Source)
target_link_libraries(ParityOracle PRIVATE LAB2C_TestJuce LAB2C_UiAssets)
CMAKE
cmake -S "$stage/source" -B "$stage/build" -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$juce_dir"
cmake --build "$stage/build" --target ParityOracle --parallel 4
"$stage/build/ParityOracle_artefacts/Release/ParityOracle" "$fixture_dir"
(cd "$fixture_dir" && shasum -a 256 ./*.f32 > SHA256SUMS)
