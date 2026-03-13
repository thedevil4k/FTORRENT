import urllib.request
import json
import zipfile
import io

req = urllib.request.Request("https://api.github.com/repos/thedevil4k/FTORRENT/actions/runs/22491266302/logs", headers={"User-Agent": "Mozilla/5.0"})
try:
    with urllib.request.urlopen(req) as response:
        with zipfile.ZipFile(io.BytesIO(response.read())) as z:
            for info in z.infolist():
                if 'build-windows (arm64)' in info.filename and 'Configure CMake' in info.filename:
                    print(f"\n--- {info.filename} ---")
                    print(z.read(info.filename).decode('utf-8', errors='ignore')[-1500:])
                if 'build-linux (i386)' in info.filename and 'checkout' in info.filename.lower() and 'Post' not in info.filename:
                    print(f"\n--- {info.filename} ---")
                    print(z.read(info.filename).decode('utf-8', errors='ignore')[-1500:])
except Exception as e:
    print(e)
