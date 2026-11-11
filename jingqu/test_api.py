import requests
import json

BASE = "http://localhost:8848"

# Login
resp = requests.post(f"{BASE}/api/auth/login", json={"username": "admin", "password": "123456"})
token = resp.json()["data"]["token"]
headers = {"Authorization": f"Bearer {token}"}

# Get trail network
resp = requests.get(f"{BASE}/api/trail/nodes", headers=headers)
data = resp.json()["data"]
print(f"Nodes: {len(data['nodes'])}, Edges: {len(data['edges'])}")

# Check if edges have geometry
geom_count = sum(1 for e in data['edges'] if 'geometry' in e)
print(f"Edges with geometry in response: {geom_count}")

# Test route planning
route_resp = requests.post(
    f"{BASE}/api/trail/route",
    headers=headers,
    json={
        "start": {"longitude": 110.4789, "latitude": 29.3395},
        "end": {"longitude": 110.4695, "latitude": 29.3539}
    }
)
route_data = route_resp.json()
if route_resp.status_code == 200:
    route_nodes = route_data["data"]["nodes"]
    print(f"\nRoute planning: SUCCESS")
    print(f"Route points: {len(route_nodes)}")
    print(f"Total distance: {route_data['data']['totalDistanceM']:.1f}m")
    print(f"Walk time: {route_data['data']['walkMinutes']}min")
    print(f"\nFirst 5 route points:")
    for n in route_nodes[:5]:
        print(f"  ({n['longitude']:.6f}, {n['latitude']:.6f})")
    print(f"\nLast 3 route points:")
    for n in route_nodes[-3:]:
        print(f"  ({n['longitude']:.6f}, {n['latitude']:.6f})")
else:
    print(f"Route planning failed: {route_resp.status_code} - {route_data}")