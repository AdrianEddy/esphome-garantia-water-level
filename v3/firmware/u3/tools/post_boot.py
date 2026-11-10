"""PlatformIO extra script for env u3_boot: check the image and write the DFU images."""
Import("env")  # noqa: F821  (provided by PlatformIO)
import os
import sys

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools"))  # noqa: F821
import mkimage  # noqa: E402


def _post(source, target, env):
    mkimage.check_boot(str(target[0]))
    mkimage.write_images(env.subst("$PROJECT_DIR"))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _post)  # noqa: F821
