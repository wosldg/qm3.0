import urllib.request, json

url = "http://localhost:8848/api/user/parking"
try:
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=5) as resp:
        data = json.loads(resp.read().decode())
        items = data.get("data", data) if isinstance(data, dict) else data
        if isinstance(items, dict) and "data" in items:
            items = items["data"]
        if isinstance(items, list):
            print(f"API 返回 {len(items)} 个停车场:")
            for item in items:
                print(f"  {item.get('name', item.get('parkingId'))}")
        else:
            print("返回数据:", json.dumps(data, ensure_ascii=False, indent=2))
except Exception as e:
    print(f"请求失败: {e}")