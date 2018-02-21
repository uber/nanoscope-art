public class ExceptionFromInterpToInterp extends TestCase {

  @Override
  protected void test() {
    $interp$a();
  }

  private static void $interp$a() {
    $interp$b();
  }

  private static void $interp$b() {
    try {
      $interp$c();
    } catch (Throwable e) { }
    d();
  }

  private static void $interp$c() throws Throwable {
    throw new Throwable();
  }

  static int i;

  private static void d() {
    i++;
  }
}