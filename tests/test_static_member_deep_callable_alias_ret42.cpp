using Callable0 = int(int);
using Callable1 = Callable0*(int);
using Callable2 = Callable1*(int);
using Callable3 = Callable2*(int);
using Callable4 = Callable3*(int);
using Callable5 = Callable4*(int);
using Callable6 = Callable5*(int);
using Callable7 = Callable6*(int);
using Callable8 = Callable7*(int);
using Callable9 = Callable8*(int);
using Callable10 = Callable9*(int);
using Callable11 = Callable10*(int);
using Callable12 = Callable11*(int);
using Callable13 = Callable12*(int);
using Callable14 = Callable13*(int);
using Callable15 = Callable14*(int);
using Callable16 = Callable15*(int);
using Callable17 = Callable16*(int);
using Callable18 = Callable17*(int);
using Callable19 = Callable18*(int);
using Callable20 = Callable19*(int);
using Callable21 = Callable20*(int);
using Callable22 = Callable21*(int);
using Callable23 = Callable22*(int);
using Callable24 = Callable23*(int);
using Callable25 = Callable24*(int);
using Callable26 = Callable25*(int);
using Callable27 = Callable26*(int);
using Callable28 = Callable27*(int);
using Callable29 = Callable28*(int);
using Callable30 = Callable29*(int);
using Callable31 = Callable30*(int);
using Callable32 = Callable31*(int);
struct DeepStaticCallable {
	static Callable32* (*callbacks)[2];
};
int main() {
	return 42;
}
