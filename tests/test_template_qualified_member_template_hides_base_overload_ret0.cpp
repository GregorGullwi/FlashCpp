namespace QualifiedMemberTemplateReplay {
	template <typename T>
	struct Base {
		static int select(int) {
			return 7;
		}
	};

	template <typename T>
	struct Holder : Base<T> {
		template <typename U>
		static int select(long) {
			return static_cast<int>(sizeof(U)) + 40;
		}
	};
}

template <typename T>
int runQualifiedMemberTemplateCall() {
	return QualifiedMemberTemplateReplay::Holder<T>::template select<T>(0);
}

int main() {
	return runQualifiedMemberTemplateCall<int>() == 44 ? 0 : 1;
}
