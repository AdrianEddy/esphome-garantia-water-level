"""PlatformIO extra script for env u3_app: C++ flags, then patch the image and write the DFU images."""
Import("env")  # noqa: F821  (provided by PlatformIO)
import os
import sys

env.Append(CXXFLAGS=["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"])  # noqa: F821

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools"))  # noqa: F821
import mkimage  # noqa: E402


def _post(source, target, env):
    mkimage.patch_app(str(target[0]))
    mkimage.write_images(env.subst("$PROJECT_DIR"))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _post)  # noqa: F821
