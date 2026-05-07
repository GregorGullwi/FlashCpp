template<class T>
struct StaticAssertOnInstantiation {
	static_assert(sizeof(T) == 0, "explicit instantiation must evaluate static_assert");
};

template class StaticAssertOnInstantiation<int>;

int main() {
	return 0;
}

