import sys
import os
import subprocess
import argparse
import zipfile
import requests
import json
from pathlib import Path
import tempfile
import shutil

def download_latest_artifact(repo_url, artifact_name, token=None, output_dir="./downloads"):
    """Downloads the latest artifact from GitHub Actions"""
    
    # Parse repo URL to get owner/repo
    if repo_url.startswith('https://github.com/'):
        repo_path = repo_url.replace('https://github.com/', '').rstrip('/')
    elif repo_url.startswith('github.com/'):
        repo_path = repo_url.replace('github.com/', '').rstrip('/')
    else:
        repo_path = repo_url.rstrip('/')
    
    if '/' not in repo_path:
        print("Error: Invalid repository format. Use: owner/repo or https://github.com/owner/repo")
        return None
    
    owner, repo = repo_path.split('/', 1)
    
    # GitHub API headers
    headers = {'Accept': 'application/vnd.github+json'}
    if token:
        headers['Authorization'] = f'token {token}'
    
    try:
        # Get latest successful workflow runs
        print(f"Fetching workflow runs for {owner}/{repo}...")
        runs_url = f'https://api.github.com/repos/{owner}/{repo}/actions/runs'
        response = requests.get(runs_url, headers=headers, params={
            'status': 'completed',
            'conclusion': 'success',
            'per_page': 10
        })
        response.raise_for_status()
        
        runs = response.json()['workflow_runs']
        if not runs:
            print("No successful workflow runs found")
            return None
        
        # Try each recent run until we find the artifact
        for run in runs:
            run_id = run['id']
            print(f"Checking run {run_id} from {run['created_at'][:10]}...")
            
            # Get artifacts for this run
            artifacts_url = f'https://api.github.com/repos/{owner}/{repo}/actions/runs/{run_id}/artifacts'
            response = requests.get(artifacts_url, headers=headers)
            response.raise_for_status()
            
            artifacts = response.json()['artifacts']
            
            # Find the specific artifact
            target_artifact = None
            for artifact in artifacts:
                if artifact['name'] == artifact_name:
                    target_artifact = artifact
                    break
            
            if target_artifact:
                print(f"Found artifact '{artifact_name}' in run {run_id}")
                
                # Download the artifact
                download_url = target_artifact['archive_download_url']
                response = requests.get(download_url, headers=headers)
                response.raise_for_status()
                
                # Save and extract with proper build structure
                os.makedirs(output_dir, exist_ok=True)
                zip_path = Path(output_dir) / f"{artifact_name}.zip"
                
                with open(zip_path, 'wb') as f:
                    f.write(response.content)
                
                # Extract and restructure for ESP-IDF
                extract_path = Path(output_dir) / artifact_name
                if extract_path.exists():
                    shutil.rmtree(extract_path)
                
                with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                    zip_ref.extractall(extract_path)
                
                # Now recreate proper build structure
                build_path = recreate_build_structure_from_flat(extract_path)
                
                print(f"Downloaded and extracted to: {build_path}")
                os.remove(zip_path)  # Clean up zip file
                return str(build_path)
        
        print(f"Artifact '{artifact_name}' not found in recent successful runs")
        return None
        
    except requests.exceptions.RequestException as e:
        if hasattr(e, 'response') and e.response.status_code == 401:
            print("Error: Authentication failed. You may need a GitHub token for private repositories.")
            print("Create a token at: https://github.com/settings/tokens")
        else:
            print(f"Error downloading artifact: {e}")
        return None

def recreate_build_structure_from_flat(flat_dir):
    """
    Take flat extracted firmware files and recreate ESP-IDF build directory structure
    """
    flat_path = Path(flat_dir)
    build_path = flat_path / "build"
    
    # Clean and create build directory
    if build_path.exists():
        shutil.rmtree(build_path)
    build_path.mkdir(parents=True, exist_ok=True)
    
    print(f"Recreating build structure from: {flat_path}")
    
    # List all files in flat directory
    flat_files = [f for f in flat_path.iterdir() if f.is_file()]
    print(f"Found {len(flat_files)} files to organize")
    
    # Process each file and place it in correct location
    for file_path in flat_files:
        filename = file_path.name
        
        # Determine correct placement based on filename
        if filename == "bootloader.bin":
            dest_dir = build_path / "bootloader"
            dest_dir.mkdir(exist_ok=True)
            dest_path = dest_dir / filename
            
        elif filename == "partition-table.bin":
            dest_dir = build_path / "partition_table"
            dest_dir.mkdir(exist_ok=True)
            dest_path = dest_dir / filename
            
        elif filename.endswith('.bin') and filename not in ['bootloader.bin', 'partition-table.bin']:
            # Main firmware binary goes to build root
            dest_path = build_path / filename
            
        elif filename in ['flash_args', 'flasher_args.json', 'project_description.json']:
            # Configuration files go to build root
            dest_path = build_path / filename
            
        elif filename == 'flash_info.txt':
            # Our custom info file goes to build root
            dest_path = build_path / filename
            
        else:
            # Unknown files go to build root
            print(f"Unknown file type: {filename}, placing in build root")
            dest_path = build_path / filename
        
        # Copy the file
        shutil.copy2(file_path, dest_path)
        print(f"  {filename} -> {dest_path.relative_to(flat_path)}")
    
    # Verify we have essential files
    essential_files = [
        build_path / "bootloader" / "bootloader.bin",
        build_path / "partition_table" / "partition-table.bin"
    ]
    
    main_bin_files = list(build_path.glob("*.bin"))
    main_bin_files = [f for f in main_bin_files if not f.name.startswith(('bootloader', 'partition'))]
    
    print(f"\nBuild structure verification:")
    for essential in essential_files:
        status = "✓" if essential.exists() else "✗"
        print(f"  {status} {essential.relative_to(flat_path)}")
    
    if main_bin_files:
        print(f"  ✓ Main firmware: {[f.name for f in main_bin_files]}")
    else:
        print(f"  ✗ No main firmware .bin file found!")
    
    # Check for flash configuration
    flash_config_files = ['flash_args', 'flasher_args.json']
    for config_file in flash_config_files:
        config_path = build_path / config_file
        status = "✓" if config_path.exists() else "✗"
        print(f"  {status} {config_file}")
    
    return str(build_path)

def flash_with_idf(port, chip, build_path, baud=460800):
    """Flash firmware using idf.py (ESP-IDF way)"""
    
    build_path = Path(build_path).resolve()
    
    # Check if this looks like a proper ESP-IDF build directory
    required_structure = [
        build_path / "bootloader" / "bootloader.bin",
        build_path / "partition_table" / "partition-table.bin"
    ]
    
    main_bins = list(build_path.glob("*.bin"))
    main_bins = [f for f in main_bins if not f.name.startswith(('bootloader', 'partition'))]
    
    missing_files = [f for f in required_structure if not f.exists()]
    
    if missing_files or not main_bins:
        print(f"Error: {build_path} doesn't have proper ESP-IDF build structure")
        print("Missing files:")
        for missing in missing_files:
            print(f"  - {missing}")
        if not main_bins:
            print(f"  - Main firmware .bin file")
        print("\nExpected structure:")
        print("  build/")
        print("    ├── bootloader/")
        print("    │   └── bootloader.bin")
        print("    ├── partition_table/")
        print("    │   └── partition-table.bin")
        print("    └── [project_name].bin")
        return False
    
    # Set target for idf.py
    env = os.environ.copy()
    env['IDF_TARGET'] = chip
    
    # Build the idf.py flash command
    esptool_cmd = [
    sys.executable, "-m",
    "esptool",
    "--chip", chip,
    "--port", port,
    "--baud", "460800",
    "write_flash", "@flasher_args.json"
]
    #subprocess.run(esptool_cmd, check=True, cwd=build_path)
    #command = [
     #   "idf.py", 
      #  '-p', port,
       # '-b', str(baud),
      #  'flash'
    #]
    
    print(f"\nFlashing {chip} firmware to {port} using pytool...")
    print(f"Build directory: {build_path}")
    print(f"Main firmware: {[f.name for f in main_bins]}")
    print(f"Command: {' '.join(esptool_cmd)}")
    
    try:
        # Run from the build directory
        result=subprocess.run(esptool_cmd, check=True, cwd=build_path.parent,env=env)
        #result = subprocess.run(
         #   command, 
          #  cwd=str(build_path.parent), 
           # env=env,
           # shell=True,
           # check=True
        #)
        print("Flashing complete! ✨")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error flashing: {e}")
        print("\nTroubleshooting:")
        print("1. Make sure the device is connected and drivers are installed")
        print("2. Try a different baud rate with --baud parameter")
        print("3. Put the device in download mode (hold BOOT button while pressing RESET)")
        print("4. Check if idf.py can detect the chip: idf.py -p [PORT] flash")
        return False
    except FileNotFoundError:
        print("Error: idf.py not found. Make sure you're running from ESP-IDF terminal.")
        print("On Windows: Use 'ESP-IDF Command Prompt'")
        print("On Linux/Mac: Source the ESP-IDF environment: . ~/esp/esp-idf/export.sh")
        return False

def monitor_device(port, chip):
    """Start serial monitor using idf.py"""
    env = os.environ.copy()
    env['IDF_TARGET'] = chip
    
    print(f"Starting monitor on {port}. Press Ctrl+] to exit.")
    try:
        subprocess.run(['idf.py', '-p', port, 'monitor'], env=env)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
    except FileNotFoundError:
        print("Error: idf.py not found. Make sure you're running from ESP-IDF terminal.")

def list_serial_ports():
    """List available serial ports"""
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        if ports:
            print("Available serial ports:")
            for port in ports:
                print(f"  {port.device} - {port.description}")
        else:
            print("No serial ports found")
    except ImportError:
        print("Note: Install pyserial to list ports: pip install pyserial")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Flash ESP-IDF firmware with auto-download from GitHub.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Download and flash ESP32 firmware
  python flash_firmware.py --repo owner/repo --artifact esp32-firmware --port COM5 --chip esp32
  
  # Download and flash ESP32-C3 firmware  
  python flash_firmware.py --repo owner/repo --artifact esp32c3-firmware --port COM6 --chip esp32c3
  
  # Download only (no flashing)
  python flash_firmware.py --repo owner/repo --artifact esp32-firmware --download-only
  
  # Flash from local build directory
  python flash_firmware.py --build-path ./my-build --port COM5 --chip esp32
  
  # Flash and then monitor
  python flash_firmware.py --repo owner/repo --artifact esp32-firmware --port COM5 --chip esp32 --monitor
        """
    )
    
    # Repository and download options
    parser.add_argument('--repo', help="GitHub repository (owner/repo or full URL)")
    parser.add_argument('--token', help="GitHub token for private repos (or set GITHUB_TOKEN env var)")
    parser.add_argument('--artifact', help="Artifact name to download (e.g., esp32-firmware)")
    
    # Flashing options
    parser.add_argument('--port', help="Serial port of the ESP device (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument('--chip', choices=['esp32', 'esp32c3', 'esp32s2', 'esp32s3', 'esp32c6', 'esp32h2'], 
                        help="Chip type")
    parser.add_argument('--build-path', help="Path to the 'build' directory containing firmware files")
    parser.add_argument('--baud', type=int, default=460800, help="Baud rate for flashing (default: 460800)")
    
    # Utility options
    parser.add_argument('--list-ports', action='store_true', help="List available serial ports")
    parser.add_argument('--download-only', action='store_true', help="Only download, don't flash")
    parser.add_argument('--monitor', action='store_true', help="Start serial monitor after flashing")
    
    args = parser.parse_args()
    
    # Get GitHub token from environment if not provided
    if not args.token:
        args.token = os.getenv('GITHUB_TOKEN')
    
    if args.list_ports:
        list_serial_ports()
        exit()
    
    # Download artifact if repo and artifact specified
    if args.repo and args.artifact:
        print(f"Downloading {args.artifact} from {args.repo}...")
        downloaded_path = download_latest_artifact(args.repo, args.artifact, args.token)
        
        if not downloaded_path:
            print("Failed to download artifact")
            exit(1)
        
        # Use downloaded path as build path if not specified
        if not args.build_path:
            args.build_path = downloaded_path
        
        if args.download_only:
            print(f"Download complete: {downloaded_path}")
            exit()
    
    # Validate flashing requirements
    if not args.port or not args.chip or not args.build_path:
        if args.repo and args.artifact:
            print("Error: --port and --chip are required for flashing")
        else:
            print("Error: Either provide --repo and --artifact for download, or --build-path for local flashing")
            print("Both --port and --chip are required for flashing")
        parser.print_help()
        exit(1)
    
    if not os.path.exists(args.build_path):
        print(f"Error: Build path '{args.build_path}' does not exist.")
        exit(1)
    
    # Flash the firmware
    success = flash_with_idf(args.port, args.chip, args.build_path, args.baud)
    
    if success and args.monitor:
        print("\nStarting serial monitor...")
        monitor_device(args.port, args.chip)
    
    exit(0 if success else 1)