// Test: template template parameter alongside a type parameter, where the
// function body uses the deduced type T.
//
// This exercises the Kind::Template skip in registerTypeParamsInScope().
// makeTemplate() only sets kind and template_name, leaving type_value
// indeterminate.  Without the skip, registering the template-template param
// ("Container") as a TypeInfo entry would poison gTypesByName with a
// garbage-type entry, potentially corrupting resolution of T or other types
// during body re-parsing.
//
// The function reads container.data (a T) and returns it, so the return value
// depends on T being correctly resolved to int.

template <typename T>
struct Box {
	T data;
};

template <template <typename> class Container, typename T>
T getValue(Container<T>& c) {
	return c.data;
}

int main() {
	Box<int> b;
	b.data = 42;
	return getValue(b);	// Container=Box, T=int deduced; returns 42
}
