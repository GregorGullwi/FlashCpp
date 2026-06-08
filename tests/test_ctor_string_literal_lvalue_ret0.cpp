struct PickCtor {
	int selected;

	PickCtor(const char* const&) : selected(1) {}
	PickCtor(const char*&&) = delete;
};

int main() {
	PickCtor pick("hello");
	return pick.selected == 1 ? 0 : 1;
}
