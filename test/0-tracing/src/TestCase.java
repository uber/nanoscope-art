import java.io.File;
import java.io.FileInputStream;
import java.util.Scanner;

public abstract class TestCase {

  private final String name = getClass().getSimpleName();

  public void run() throws Exception {
    File outFile = new File("/sdcard/out.txt");
    String startThreadName = "START_TRACING";
    String stopThreadName = "STOP_TRACING:" + outFile.getAbsolutePath();

    Thread.currentThread().setName(startThreadName);
    test();
    Thread.currentThread().setName(stopThreadName);

    Scanner in = new Scanner(new FileInputStream(outFile)).useDelimiter("\n");
    String indent = "  ";
    String prefix = null;

    System.out.println();
    System.out.println("===== " + name);
    System.out.println();
    while (in.hasNext()) {
      String timestamp = in.useDelimiter(":").next().replace("\n", "");
      String name = in.useDelimiter("\n").next().replace(":", "");
      if (name.equals("POP")) {
        if (prefix != null) {
          if (prefix.isEmpty()) {
            prefix = null;
          } else {
            prefix = prefix.substring(0, prefix.length() - indent.length());
          }
        }
      } else {
        if (prefix == null) {
          prefix = "";
        } else {
          prefix += indent;
        }
        System.out.println(prefix + name);
      }
    }
  }

  protected abstract void test();
}