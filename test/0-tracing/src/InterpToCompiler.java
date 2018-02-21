class InterpToCompiler extends TestCase {
  
  @Override
  protected void test() {
    $interp$a();
  }

  void $interp$a() {
    b();
  }

  int i;

  void b() {
    i++;
  }
}