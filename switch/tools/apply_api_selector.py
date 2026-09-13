from pathlib import Path

source_path = Path("switch/source/main.cpp")
source = source_path.read_text()

# Keep the provider state available for the existing refresh pipeline, but do
# not inject the API selector UI during this diagnostic build. Settings must be
# tested in its original XML form so we can distinguish a selector/UI crash
# from the underlying Borealis Settings navigation path.
include = '#include "api_sources.hpp"\n'
if include not in source:
    marker = '#include <algorithm>\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate main.cpp include boundary")
    source = source.replace(marker, marker + include, 1)

if "static int g_apiSource" not in source:
    marker = 'static bool g_homeRefreshInProgress = false;\n'
    addition = marker + '''static int g_apiSource = 0; // 0=Miruro, 1=AnimePahe, 2=Gogoanime, 3=Aniwatch, 4=HiAnime
static bool g_apiSourceRefreshPending = false;
'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate Home persistence globals")
    source = source.replace(marker, addition, 1)

# The selector UI is intentionally disabled for this diagnostic build.
# Leaving main.xml untouched gives us the stock Settings tab.
source_path.write_text(source)
print("API selector UI disabled for Settings crash isolation test")
