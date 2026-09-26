extern "C" int accept(int (*(*(*callback)(int))[3])[4]) {
	return callback == nullptr;
}

int (*(*wrong(float value))[3])[4] {
	(void)value;
	return nullptr;
}

int main() {
	return accept(wrong);
}
