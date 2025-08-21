import json
import requests

# Test ping
response = requests.post('http://localhost:3335/rpc', 
    headers={'Content-Type': 'application/json'},
    json={"jsonrpc": "2.0", "method": "ping", "id": 1},
    timeout=2)

print(f"Status: {response.status_code}")
print(f"Headers: {response.headers}")
print(f"Body: {response.text}")
