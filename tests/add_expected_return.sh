#!/bin/bash
# Helper script to add EXPECTED_RETURN annotations to test files
# Usage: ./add_expected_return.sh <test_file.cpp>

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <test_file.cpp>"
    echo "Example: $0 tests/test_my_feature.cpp"
    exit 1
fi

TEST_FILE="$1"

if [ ! -f "$TEST_FILE" ]; then
    echo "Error: File '$TEST_FILE' not found"
    exit 1
fi

# Check if file already has EXPECTED_RETURN
if grep -q "EXPECTED_RETURN:" "$TEST_FILE"; then
    echo "File already has EXPECTED_RETURN annotation"
    current_value=$(grep -E "^\s*//\s*EXPECTED_RETURN:\s*[0-9]+" "$TEST_FILE" | head -1 | sed -E 's/.*EXPECTED_RETURN:\s*([0-9]+).*/\1/')
    echo "Current value: $current_value"
    read -p "Do you want to update it? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 0
    fi
fi

# Compile the test
BASE_NAME=$(basename "$TEST_FILE" .cpp)
OBJ_FILE="${BASE_NAME}.obj"
EXE_FILE="/tmp/${BASE_NAME}_exe"

echo "Compiling $TEST_FILE..."
if ! ./x64/Debug/FlashCpp "$TEST_FILE" -o "$OBJ_FILE" > /dev/null 2>&1; then
    echo "Error: Failed to compile $TEST_FILE"
    exit 1
fi

echo "Linking..."
if ! clang++ -no-pie -o "$EXE_FILE" "$OBJ_FILE" -lstdc++ -lc > /dev/null 2>&1; then
    echo "Error: Failed to link $OBJ_FILE"
    rm -f "$OBJ_FILE"
    exit 1
fi

echo "Running executable..."
"$EXE_FILE" > /dev/null 2>&1
RETURN_VALUE=$?

echo "Return value: $RETURN_VALUE"

# Clean up
rm -f "$OBJ_FILE" "$EXE_FILE"

# Ask if user wants to add the annotation
read -p "Add EXPECTED_RETURN: $RETURN_VALUE to the file? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Check if file already has the annotation
    if grep -q "EXPECTED_RETURN:" "$TEST_FILE"; then
        # Update existing annotation
        sed -i.bak -E "s|^(\s*//\s*EXPECTED_RETURN:\s*)[0-9]+|\1$RETURN_VALUE|" "$TEST_FILE"
        rm -f "${TEST_FILE}.bak"
        echo "Updated EXPECTED_RETURN to $RETURN_VALUE in $TEST_FILE"
    else
        # Add new annotation at the top
        TMP_FILE=$(mktemp)
        echo "// EXPECTED_RETURN: $RETURN_VALUE" > "$TMP_FILE"
        cat "$TEST_FILE" >> "$TMP_FILE"
        mv "$TMP_FILE" "$TEST_FILE"
        echo "Added EXPECTED_RETURN: $RETURN_VALUE to $TEST_FILE"
    fi
else
    echo "No changes made"
fi
