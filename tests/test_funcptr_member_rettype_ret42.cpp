// Test that function pointer members use the correct return type in codegen.
// Uses a short-returning callback to verify the return type is not hardcoded
// to int or void. If function_signature is not propagated to StructMember,
// the codegen falls back to Type::Void (ret_size=0), losing the return value.
typedef short (*ShortCallback)(short);

struct Handler {
    ShortCallback callback;
};

short double_it(short x) {
    return x * 2;
}

int main() {
    Handler h;
    h.callback = double_it;
    short result = h.callback(21);
    return result;  // Should be 42
}
