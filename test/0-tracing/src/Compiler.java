class Compiler extends TestCase {
  
  @Override
  protected void test() {
    a();
  }

  void a() {
    b();
    c();
  }

  int i;

  void b() {
    i++;
  }

  void c() {
    d();
  }

  void d() {
    i++;
  }
}