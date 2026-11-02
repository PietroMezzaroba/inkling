# PlatformIO pre-script: stamps the firmware with the git revision, so the
# device can report exactly what it runs (GET /info in Wi-Fi update mode).
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

try:
    version = subprocess.check_output(
        ["git", "describe", "--always", "--dirty"], cwd=env["PROJECT_DIR"], text=True  # noqa: F821
    ).strip()
except Exception:
    version = "dev"

env.Append(CPPDEFINES=[("INKLING_VERSION", env.StringifyMacro(version))])  # noqa: F821
