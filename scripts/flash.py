import sys
import subprocess
import os

def usage():
    print("Usage:")
    print("  python flash.py main -p COM22")
    print("  python flash.py safe -p COM22")
    sys.exit(1)

# ---- parse arguments ----
if len(sys.argv) != 4 or sys.argv[2] != "-p":
    usage()

app = sys.argv[1]
port = sys.argv[3]

if app not in ("main", "safe"):
    print("ERROR: app must be 'main' or 'safe'")
    usage()

# ---- resolve ESP-IDF ----
idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    print("ERROR: IDF_PATH not set. Run ESP-IDF export first.")
    sys.exit(1)

idf_py = os.path.join(idf_path, "tools", "idf.py")

# ---- select app ----
if app == "main":
    app_dir = "apps/main_app"
    build_dir = "build_main"
else:
    app_dir = "apps/safe_app"
    build_dir = "build_safe"

# ---- build + flash command ----
cmd = [
    sys.executable,
    idf_py,
    "-C", app_dir,
    "-B", build_dir,
    "-p", port,
    "flash"
]

print("Flashing:", app)
print("Port:", port)
print("Command:", " ".join(cmd))

subprocess.check_call(cmd)
