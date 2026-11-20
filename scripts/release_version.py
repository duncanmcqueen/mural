"""Works out the firmware version from git, shared by the build and the release packager.

Release tags look like v1.1-plus.1. On a tagged commit the version is "1.1-plus.1";
later commits get git's suffix ("1.1-plus.1-3-gabc1234"), and uncommitted changes add
"-dirty". With no release tag yet it's "dev-<commit>". Only *-plus* tags count, so
upstream's tags (v1.1, v2...) are ignored.
"""
import subprocess

TAG_PATTERN = "v*-plus*"


def get_version(project_dir):
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--match", TAG_PATTERN, "--always", "--dirty"],
            cwd=project_dir, capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    if out.startswith("v"):
        return out[1:]
    return "dev-" + out   # no release tag reachable: just the commit hash


if __name__ == "__main__":
    import os
    print(get_version(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
