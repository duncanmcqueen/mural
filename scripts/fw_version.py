import os
import sys
Import("env", "projenv")

# Bakes the git version into the firmware as MURAL_FW_VERSION (shown on the
# Diagnostics page and in the serial log at boot). Applied to the project
# sources only, so a new commit doesn't force the libraries to rebuild.

project_dir = env.subst("$PROJECT_DIR")
sys.path.insert(0, os.path.join(project_dir, "scripts"))
from release_version import get_version

version = get_version(project_dir)
print("Firmware version: " + version)
projenv.Append(CPPDEFINES=[("MURAL_FW_VERSION", env.StringifyMacro(version))])
