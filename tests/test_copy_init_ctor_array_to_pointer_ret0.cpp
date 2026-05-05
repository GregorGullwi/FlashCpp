struct View {
	const char* p;
	View(const char* s) : p(s) {}
};

int main() {
	View view = "abc";
	return view.p[1] == 'b' ? 0 : 1;
}
