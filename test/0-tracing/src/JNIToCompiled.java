class JNIToCompiled extends TestCase {
  @Override
  protected void test() {
    a();
  }

  static int i;

  public static void a() {
    b();
    try {
      jniMethod();
    } catch (Exception e) { }
    d();
  }

  public static void b() {
    i++;
  }

  public static void fromJNI() {
    c();
  }

  public static void c() {
    throw new RuntimeException();
  }

  public static void d() {
    i++;
  }

  public static native int jniMethod();
}