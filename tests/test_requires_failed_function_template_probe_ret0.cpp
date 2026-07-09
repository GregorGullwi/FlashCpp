template<typename Type>
void pointer_only(Type*);

template<typename Type>
concept has_pointer_call = requires(Type value) {
	pointer_only(value);
};

static_assert(!has_pointer_call<int>);

int main() {
	return has_pointer_call<int> ? 1 : 0;
}
