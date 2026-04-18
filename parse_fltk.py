import json, base64

with open(r'C:\Users\thedevil4k\.gemini\antigravity\brain\ab96864c-6e2a-4cb1-b233-18cfa57fbb07\.system_generated\steps\135\content.md', 'r', encoding='utf-8') as f:
    data = f.read()

idx = data.index('{')
j = json.loads(data[idx:])
c = base64.b64decode(j['content']).decode('utf-8')

for line in c.split('\n'):
    if 'scrollbar' in line.lower():
        print(line)
