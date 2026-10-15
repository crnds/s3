# PlatformIO post: script (platformio.ini extra_scripts) adding
#
#   pio run -t publish
#
# which builds, then copies firmware.bin to ~/.cyd_firmware/s3.bin. ~/cyd's
# usage_server offers that file to the board (roadmap O9, src/ota.cpp): with
# Settings > Firmware > Auto update on, the board installs it within one poll.
# A plain `pio run` never reaches the board -- only what is published here.
#
# The copy is atomic (temp file + os.replace), so the server never offers a
# half-written image.
import hashlib
import os
import shutil

Import("env")  # noqa: F821 -- provided by PlatformIO's SCons

PUBLISH_DIR = os.path.expanduser("~/.cyd_firmware")
PUBLISH_PATH = os.path.join(PUBLISH_DIR, "s3.bin")


def publish(source, target, env):
    src = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    os.makedirs(PUBLISH_DIR, exist_ok=True)
    tmp = PUBLISH_PATH + ".tmp"
    shutil.copyfile(src, tmp)
    os.replace(tmp, PUBLISH_PATH)
    with open(PUBLISH_PATH, "rb") as f:
        data = f.read()
    slot = int(env.BoardConfig().get("upload.maximum_size", 6553600))
    print("Published %s (%d bytes, %.1f%% of the app slot)" % (PUBLISH_PATH, len(data), 100.0 * len(data) / slot))
    # The image digest esptool appends: what the server announces and what
    # the board compares with its own running image.
    print("Image digest %s (file sha256 %s)" % (data[-32:].hex()[:12], hashlib.sha256(data).hexdigest()[:12]))


env.AddCustomTarget(  # noqa: F821
    name="publish",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=[publish],
    title="Publish OTA",
    description="Build and offer firmware.bin to the board through the Mac server",
)
