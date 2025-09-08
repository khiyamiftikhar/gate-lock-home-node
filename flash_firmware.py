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
                
                # Save and extract
                os.makedirs(output_dir, exist_ok=True)
                zip_path = Path(output_dir) / f"{artifact_name}.zip"
                
                with open(zip_path, 'wb') as f:
                    f.write(response.content)
                
                # Extract the zip
                extract_path = Path(output_dir) / artifact_name
                if extract_path.exists():
                    shutil.rmtree(extract_path)
                
                with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                    zip_ref.extractall(extract_path)
                
                print(f"Downloaded and extracted to: {extract_path}")
                os.remove(zip_path)  # Clean up zip file
                return str(extract_path)
        
        print(f"Artifact '{artifact_name}' not found in recent successful runs")
        return None
        
    except requests.exceptions.RequestException as e:
        if hasattr(e, 'response') and e.response.status_code == 401:
            print("Error: Authentication failed. You may need a GitHub token for private repositories.")
            print("Create a token at: https://github.com/settings/tokens")
        else:
            print(f"Error downloading artifact: {e}")
        return None

def flash_with_idf(port, chip, build_path, baud=460800):
    """Flash firmware using idf.py (ESP-IDF way)"""
    
    build_path = Path(build_path).resolve()
    
    # Check if this looks like a build directory
    if not (build_path / 'bootloader').exists() and not any(build_path.glob('*.bin')):
        print(f"Error: {build_path} doesn't look like an ESP-IDF build directory")
        print("Expected to find bootloader/ directory or .bin files")
        return False
    
    # Set target for idf.py
    env = os.environ.copy()
    env['IDF_TARGET'] = chip
    
    # Build the idf.py flash command
    command = [
        'idf.py', 
        '-p', port,
        '-b', str(baud),
        'flash'
    ]
    
    print(f"Flashing {chip} firmware to {port} using idf.py...")
    print(f"Build directory: {build_path}")
    print(f"Command: {' '.join(command)}")
    
    try:
        # Run from the build directory
        result = subprocess.run(
            command, 
            cwd=str(build_path), 
            env=env,
            check=True
        )
        print("Flashing complete! ✨")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error flashing: {e}")
        print("\nTroubleshooting:")
        print("1. Make sure the device is connected and drivers are installed")
        print("2. Try a different baud rate with --baud parameter")
        print("3. Put the device in download mode (hold BOOT button while pressing RESET)")
        return False
    except FileNotFoundError:
        print("Error: idf.py not found. Make sure you're running from ESP-IDF terminal.")
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