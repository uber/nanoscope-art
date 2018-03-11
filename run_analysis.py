import os
import time

#
# Alternate running the app with tracing enabled & inlining disabled vs tracing disabled and inlining disabled.
# Between every run reset & clear the compilation profile. The application being executed should print
# performance metrics to logic during this test.
#

alternate = False
count = 0
MAX_COUNT = 40

def killApp():
    os.system("adb shell ps | grep com.ubercab.presidio.development | awk '{print $2}' | xargs adb shell kill")

def automateApp():
    print "Starting app"
    os.system("adb shell am start -n com.ubercab.presidio.development/com.ubercab.presidio.app.core.root.RootActivity")

    print "Waiting for app to launch. Then clicking shortcut button"
    time.sleep(12)
    os.system("adb shell input tap 200 800")
    time.sleep(3)
    killApp()


killApp()
for x in range(0, MAX_COUNT):
    count = count + 1
    print "Iteration " + str(count) + " of " + str(MAX_COUNT)
    if alternate:
        print "Setting system property for disabling inlining."
        os.system("adb shell setprop dev.nanoscope com.ubercab.presidio.development:out.txt")
        alternate = False
    else:
        print "Setting system property for enabling inlining."
        os.system("adb shell setprop dev.nanoscope \\'\\'")
        alternate = True

    # One second sleep for good measure. May be unnecessary.
    time.sleep(1)

    # Clear compliled code and clear profile. 
    # See https://source.android.com/devices/tech/dalvik/jit-compiler#force-compilation-of-a-specific-package
    print "Resetting compilation profile. Can take a while."
    os.system("adb shell cmd package compile --reset com.ubercab.presidio.development")

    # One second sleep for good measure. May be unnecessary.
    time.sleep(1)
    automateApp()

    # Run a second time. So we aren't only running the fresh start.
    time.sleep(4)
    automateApp()