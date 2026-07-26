#!/usr/bin/python3

# Adds PlatformIO post-processing to convert hex files to uf2 files.
#
# The .uf2 is the only flashable artifact for these nRF52 boards (drag-and-drop
# to the UF2 bootloader); .elf/.hex/.zip are not. It is therefore produced as a
# mandatory post-build action of every plain `pio run`, NOT as an opt-in custom
# target. It used to be target-only, which meant `pio run -e <env>` reported
# success while emitting no .uf2 at all - a build that looked green but left
# nothing to flash, so an older release file could quietly stay on the board.
# If the conversion fails or yields an empty file, the build now fails loudly
# rather than leaving that gap. The custom target is kept for compatibility.

import os
import sys

Import("env")

firmware_hex = "${BUILD_DIR}/${PROGNAME}.hex"
uf2_file = os.environ.get("UF2_FILE_PATH", "${BUILD_DIR}/${PROGNAME}.uf2")


def create_uf2_action(source, target, env):
    uf2_cmd = " ".join(
        [
            '"$PYTHONEXE"',
            '"$PROJECT_DIR/bin/uf2conv/uf2conv.py"',
            '-f', '0xADA52840',
            '-c', firmware_hex,
            '-o', uf2_file,
        ]
    )
    result = env.Execute(uf2_cmd)
    if result:
        print("[create-uf2] ERROR: uf2conv failed (exit %s)" % result)
        env.Exit(1)

    # A zero-status uf2conv that wrote nothing (or an empty file) would leave
    # the same silent gap this script exists to close, so check the artifact
    # itself rather than trusting the exit code.
    resolved = env.subst(uf2_file)
    if not os.path.isfile(resolved) or os.path.getsize(resolved) == 0:
        print("[create-uf2] ERROR: expected UF2 missing or empty: %s" % resolved)
        env.Exit(1)

    print("[create-uf2] wrote %s (%d bytes)"
          % (resolved, os.path.getsize(resolved)))


# Mandatory, and deliberately NOT an AddPostAction on the hex. A post-action
# only fires when the hex itself is rebuilt: touch a source, get a byte-identical
# relink, and SCons skips the hex - so the post-action never runs and a missing
# .uf2 stays missing while the build still reports SUCCESS. That is the exact
# gap this script exists to close, so the .uf2 is registered as a real build
# target instead, marked AlwaysBuild so it is regenerated on every `pio run`
# regardless of what SCons thinks is up to date. The conversion takes ~1 s.
uf2_target = env.Command(uf2_file, firmware_hex, create_uf2_action)
env.AlwaysBuild(uf2_target)
env.Default(uf2_target)

# Retained so `pio run -t create_uf2` keeps working for anyone who calls it
# explicitly (and so build.sh's direct uf2conv invocation is not the only path).
env.AddCustomTarget(
    name="create_uf2",
    dependencies=firmware_hex,
    actions=create_uf2_action,
    title="Create UF2 file",
    description="Use uf2conv to convert hex binary into uf2",
    always_build=True,
)
