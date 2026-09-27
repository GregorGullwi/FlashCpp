class Vault;

template <typename Owner>
struct Family {
	template <typename Member>
	struct Tool {
		template <typename T>
		int read(const T& vault) const {
			return vault.secret + vault.hidden() - 42;
		}
	};
};

class Vault {
	int secret;
	int hidden() const {
		return secret;
	}

public:
	Vault() : secret(42) {}
	friend struct Family<long>::Tool<int>;
};

int main() {
	Vault vault;
	Family<long>::Tool<int> friend_tool;
	return friend_tool.read(vault);
}
