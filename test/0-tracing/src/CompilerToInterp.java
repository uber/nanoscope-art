class CompilerToInterp extends TestCase {
  
  @Override
  protected void test() {
    a();
  }

  void a() {
    $interp$b();
  }

  int i;

  void $interp$b() {
    i++;
  }
}