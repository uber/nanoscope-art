public class ExceptionFromInterpToCompiled extends TestCase {

  @Override
  protected void test() {
    a();
  }

  private static void a() {
    b();
  }

  private static void b() {
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