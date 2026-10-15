// A declarator whose function return is itself callable keeps one signature per
// Function component. `int (*(*p)())()` is a pointer to a function returning a
// function pointer: the ordered spine has two Function components, and the
// inner signature lives on the enclosing return FunctionType's
// callable_signature. It must parse, project, import, and lower as an 8-byte
// pointer object without dropping the inner callable.
struct Payload {
	int value;
	int tag;
};

int (*(*g_nested)())(int) = nullptr;
int (* (*g_struct_arg)(Payload))(Payload) = nullptr;

int main() {
	int (*(*local_nested)())(int) = nullptr;
	int (* (*local_struct)(Payload))(Payload) = nullptr;
	(void)g_nested;
	(void)g_struct_arg;
	(void)local_nested;
	(void)local_struct;
	return sizeof(g_nested) == sizeof(void*) && sizeof(local_struct) == sizeof(void*) ? 0 : 1;
}
