struct ForwardDeclared;

using IncompleteAlias = ForwardDeclared;

constexpr unsigned long long invalid_size = sizeof(IncompleteAlias);

int main() {
	return static_cast<int>(invalid_size);
}
