struct Base { long long value; };
struct Key : Base { short tag; };

Key value{7, {1}};

int main() {
	return value.value == 7 && value.tag == 1 ? 0 : 1;
}
