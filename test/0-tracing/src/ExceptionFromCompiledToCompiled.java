public class ExceptionFromCompiledToCompiled extends TestCase {

  @Override
  protected void test() {
    a();
  }

  private static void a() {
    b();
  }

  private static void b() {
    try {
      c();
    } catch (Throwable e) { }
    d();
  }

  private static void c() throws Throwable {
    throw new Throwable();
  }

  static int i;

  private static void d() {
    i++;
  }
}