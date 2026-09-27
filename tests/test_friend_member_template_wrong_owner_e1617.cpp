class Vault;

template <typename Owner>
struct Family {
	template <typename Member>
	struct Tool {
		template <typename T>
		int read(const T& vault) const {
			return vault.secret;
		}
	};
};

class Vault {
	int secret;

public:
	Vault() : secret(42) {}
	friend struct Family<long>::Tool<int>;
};

int main() {
	Vault vault;
	Family<char>::Tool<int> non_friend_tool;
	return non_friend_tool.read(vault);
}
