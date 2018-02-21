/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.io.File;
import java.io.FileInputStream;
import java.util.Scanner;

public class Main {

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);

    File outFile = new File("/sdcard/out.txt");
    String startThreadName = "START_TRACING";
    String stopThreadName = "STOP_TRACING:" + outFile.getAbsolutePath();

    Thread.currentThread().setName(startThreadName);
    test();
    Thread.currentThread().setName(stopThreadName);

    Scanner in = new Scanner(new FileInputStream(outFile)).useDelimiter("\n");
    String indent = "  ";
    String prefix = null;
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

  private static void test() {
    try {
      $opt$throwable();
    } catch (Throwable e) { }
  }

  private static void $opt$throwable() throws Throwable {
    if (isInterpreted()) {
      float a = 1 / 0;
    }
    throw new Throwable();
  }

  public static native boolean isInterpreted();
}
