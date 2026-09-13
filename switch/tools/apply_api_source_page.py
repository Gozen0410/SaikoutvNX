from pathlib import Path

xml_path = Path("switch/romfs/xml/activity/main.xml")
xml = xml_path.read_text()

# API providers are intentionally kept as a direct Settings section. The
# selector pass owns the controls and binds them when the XML root is created.
# Do not replace them with a second activity or a separate API page here.
required = [
    'id="api-source-current"',
    'id="api-source-miruro"',
    'id="api-source-animepahe"',
    'id="api-source-gogoanime"',
    'id="api-source-aniwatch"',
    'id="api-source-hianime"',
]
missing = [item for item in required if item not in xml]
if missing:
    raise SystemExit("API source Settings section is missing: " + ", ".join(missing))

print("API source page pass: keeping providers inline in Settings")