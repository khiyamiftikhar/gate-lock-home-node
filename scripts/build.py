import sys
import subprocess
import os

if len(sys.argv) < 2:
    print("Usage: build.py [main|safe]")
    sys.exit(1)

app = sys.argv[1]

if app not in ("main", "safe"):
    print("Invalid app. Use 'main' or 'safe'")
    sys.exit(1)

idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    print("ERROR: IDF_PATH not set. Run ESP-IDF environment first.")
    sys.exit(1)

idf_py = os.path.join(idf_path, "tools", "idf.py")

app_dir = "apps/main_app" if app == "main" else "apps/safe_app"
build_dir = f"build_{app}"

cmd = [
    sys.executable,
    idf_py,
    "-C", app_dir,
    "-B", build_dir,
    "build"
]

print("Running:", " ".join(cmd))
subprocess.check_call(cmd)
