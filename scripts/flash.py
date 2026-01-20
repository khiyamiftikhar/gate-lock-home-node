import sys
import subprocess
import os

def usage():
    print("")
    print("Usage:")
    print("  python flash.py <main|safe> -p COMx <command> [command ...]")
    print("")
    print("Examples:")
    print("  python flash.py main -p COM22 flash")
    print("  python flash.py main -p COM22 flash monitor")
    print("  python flash.py main -p COM22 monitor")
    print("  python flash.py safe -p COM22 flash monitor")
    print("")
    sys.exit(1)

# ---- basic arg check ----
if len(sys.argv) < 5:
    usage()

app = sys.argv[1]

if app not in ("main", "safe"):
    print("ERROR: app must be 'main' or 'safe'")
    usage()

if sys.argv[2] != "-p":
    usage()

port = sys.argv[3]

commands = sys.argv[4:]
if not commands:
    usage()

# ---- ESP-IDF environment ----
idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    print("ERROR: IDF_PATH not set. Run ESP-IDF export.ps1 first.")
    sys.exit(1)

idf_py = os.path.join(idf_path, "tools", "idf.py")

# ---- select app ----
if app == "main":
    app_dir = "apps/main_app"
    build_dir = "build_main"
else:
    app_dir = "apps/safe_app"
    build_dir = "build_safe"

# ---- construct command ----
cmd = [
    sys.executable,
    idf_py,
    "-C", app_dir,
    "-B", build_dir,
    "-p", port,
]

cmd.extend(commands)

# ---- execute ----
print("\nRunning command:")
print(" ", " ".join(cmd), "\n")

subprocess.check_call(cmd)
