#!/usr/bin/env python3

import requests
import json

def test_github_token(token, repo="khiyamiftikhar/gate-lock-home-node"):
    """Test GitHub token with detailed debugging"""
    
    print(f"Testing token: {token[:20]}...")
    print(f"Repository: {repo}")
    print("-" * 50)
    
    # Determine header format based on token type
    if token.startswith('github_pat_'):
        headers = {
            'Authorization': f'Bearer {token}',
            'Accept': 'application/vnd.github+json',
            'X-GitHub-Api-Version': '2022-11-28'
        }
        print("Using Bearer authentication (fine-grained token)")
    else:
        headers = {
            'Authorization': f'token {token}',
            'Accept': 'application/vnd.github+json'
        }
        print("Using token authentication (classic token)")
    
    # Test 1: Basic API access
    print("\n1. Testing basic API access...")
    try:
        response = requests.get('https://api.github.com/user', headers=headers)
        print(f"   Status: {response.status_code}")
        if response.status_code == 200:
            user_data = response.json()
            print(f"   ✅ Authenticated as: {user_data.get('login', 'Unknown')}")
        else:
            print(f"   ❌ Error: {response.text}")
            return False
    except Exception as e:
        print(f"   ❌ Exception: {e}")
        return False
    
    # Test 2: Repository access
    print(f"\n2. Testing repository access to {repo}...")
    try:
        response = requests.get(f'https://api.github.com/repos/{repo}', headers=headers)
        print(f"   Status: {response.status_code}")
        if response.status_code == 200:
            repo_data = response.json()
            print(f"   ✅ Repository: {repo_data['full_name']}")
            print(f"   Private: {repo_data['private']}")
            print(f"   Default branch: {repo_data['default_branch']}")
        elif response.status_code == 404:
            print(f"   ❌ Repository not found or no access")
            print(f"   Response: {response.text}")
            return False
        else:
            print(f"   ❌ Error: {response.text}")
            return False
    except Exception as e:
        print(f"   ❌ Exception: {e}")
        return False
    
    # Test 3: Actions access
    print(f"\n3. Testing GitHub Actions access...")
    try:
        response = requests.get(f'https://api.github.com/repos/{repo}/actions/runs', 
                              headers=headers, 
                              params={'per_page': 5})
        print(f"   Status: {response.status_code}")
        if response.status_code == 200:
            runs_data = response.json()
            runs = runs_data.get('workflow_runs', [])
            print(f"   ✅ Found {len(runs)} workflow runs")
            print(f"   Total runs: {runs_data.get('total_count', 0)}")
            
            if runs:
                print("   Recent runs:")
                for run in runs[:3]:
                    status = run['conclusion'] or run['status']
                    print(f"     - {run['name']}: {status} ({run['created_at'][:10]})")
            else:
                print("   No workflow runs found")
        elif response.status_code == 404:
            print(f"   ❌ Actions not found or no access")
            print(f"   This could mean:")
            print(f"     - GitHub Actions is disabled for this repo")
            print(f"     - Token doesn't have actions:read permission")
            print(f"   Response: {response.text}")
            return False
        else:
            print(f"   ❌ Error: {response.text}")
            return False
    except Exception as e:
        print(f"   ❌ Exception: {e}")
        return False
    
    # Test 4: List artifacts from latest run
    print(f"\n4. Testing artifacts access...")
    try:
        response = requests.get(f'https://api.github.com/repos/{repo}/actions/runs', 
                              headers=headers, 
                              params={'per_page': 1, 'status': 'completed'})
        
        if response.status_code == 200:
            runs_data = response.json()
            runs = runs_data.get('workflow_runs', [])
            
            if runs:
                latest_run = runs[0]
                run_id = latest_run['id']
                print(f"   Latest run ID: {run_id}")
                
                # Get artifacts for this run
                response = requests.get(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}/artifacts', 
                                      headers=headers)
                
                if response.status_code == 200:
                    artifacts_data = response.json()
                    artifacts = artifacts_data.get('artifacts', [])
                    print(f"   ✅ Found {len(artifacts)} artifacts in latest run:")
                    for artifact in artifacts:
                        print(f"     - {artifact['name']} ({artifact['size_in_bytes']} bytes)")
                else:
                    print(f"   ❌ Error getting artifacts: {response.text}")
            else:
                print("   No completed runs found")
        else:
            print(f"   ❌ Error getting runs: {response.text}")
            
    except Exception as e:
        print(f"   ❌ Exception: {e}")
        return False
    
    print("\n✅ All tests passed!")
    return True

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) != 2:
        print("Usage: python test_token.py YOUR_GITHUB_TOKEN")
        sys.exit(1)
    
    token = sys.argv[1]
    success = test_github_token(token)
    sys.exit(0 if success else 1)