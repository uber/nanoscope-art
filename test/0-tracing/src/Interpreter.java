class Interpreter extends TestCase {
  
  @Override
  protected void test() {
    $interp$a();
  }

  void $interp$a() {
    $interp$b();
    $interp$c();
  }

  int i;

  void $interp$b() {
    i++;
  }

  void $interp$c() {
    $interp$d();
  }

  void $interp$d() {
    i++;
  }
}