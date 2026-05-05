using callback_t = bool(__stdcall*)(int, ...) noexcept;

struct Holder {
	using member_callback_t = int(__stdcall*)(long, ...) noexcept;
	member_callback_t callback;
};

int accept_callback(callback_t callback) {
	(void)callback;
	return 0;
}

int main() {
	return accept_callback(0);
}
