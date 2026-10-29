void target() {}

using Throwing = void (*)();
using Noexcept = void (*)() noexcept;

int main() {
	Noexcept converted = &target;
	return converted == &target ? 0 : 1;
}
