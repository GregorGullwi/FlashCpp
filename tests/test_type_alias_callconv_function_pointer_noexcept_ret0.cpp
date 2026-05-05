using callback_t = bool(__stdcall*)(int, ...) noexcept;

struct Holder {
	using member_callback_t = int(__stdcall*)(long, ...) noexcept;
	member_callback_t callback;
};

int main() {
	return 0;
}
