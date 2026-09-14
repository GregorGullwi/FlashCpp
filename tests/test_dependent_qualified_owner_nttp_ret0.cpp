// A template non-type argument spelled as a namespace-qualified owner member
// (ns::Config::kLimit) reconstructs its Owner::member dependent name while
// parsing. The owner spelling must stay valid until the dependent name
// combines; a dangling owner view would corrupt the reconstructed name and
// break the static-member fold at rematerialization.
namespace ns {
struct Config {
	static constexpr int kLimit = 6;
};
}

template<int V>
struct Scale {
	static constexpr int value = V * 3;
};

int main() {
	return Scale<ns::Config::kLimit>::value == 18 ? 0 : 1;
}
