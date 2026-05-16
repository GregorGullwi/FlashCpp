int ga = 5;
int gb = 7;

int fa() {
	return 3;
}

int fb() {
	return 4;
}

template <auto P>
int read_object() {
	return *P;
}

template <auto F>
int call_function() {
	return F();
}

int main() {
	int object_pair = read_object<&ga>() * 10 + read_object<&gb>();
	if (object_pair != 57) {
		return 1;
	}

	int function_pair = call_function<&fa>() * 10 + call_function<&fb>();
	if (function_pair != 34) {
		return 2;
	}

	return 0;
}
