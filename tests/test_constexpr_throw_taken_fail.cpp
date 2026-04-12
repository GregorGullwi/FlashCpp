// C++20 requires an evaluated throw expression to make the initializer non-constant.

constexpr int bad_value = false ? 1 : throw 3;

int main() {
	return bad_value;
}
