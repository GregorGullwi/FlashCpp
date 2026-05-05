struct View {
	const char* p;
	View(const char* s) : p(s) {}
};

int main() {
	View view = "abc";
	(void)view;
	return 0;
}
