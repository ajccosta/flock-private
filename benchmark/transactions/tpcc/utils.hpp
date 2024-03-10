template<typename T>
class alignas(64) padded {
public:
	T ui;
  padded<T> ():ui() {};
  // conversion from T (constructor):
  padded<T> (const T& val):ui(val) {};
  // conversion from A (assignment):
  padded<T>& operator= (const T& val) {ui = val; return *this;}
  // conversion to A (type-cast operator)
  operator T() {return T(ui);}
};

void errexit(std::string s) {
  std::cout << s << std::endl;
  abort();
}

bool LIKELY(bool x) { return x;}
