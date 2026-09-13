from pathlib import Path
import re

main = Path("switch/source/main.cpp")
source = main.read_text()

# The Home screen must not depend on a streaming provider. Keep the provider
# integration available for the later episode/source layer, but make the
# current Home probe a local no-op until AniList becomes the Home data source.
pattern = re.compile(
    r"static ApiResult run_api_probe\(\)\n\{.*?\n\}\n\nstatic bool download_image",
    re.S,
)

replacement = '''static ApiResult run_api_probe()
{
    ApiResult result;
    log_stage("HOME STREAMING PROVIDERS DETACHED");
    result.status = "Home provider requests disabled";
    result.response.clear();
    return result;
}

static bool download_image'''

matches = list(pattern.finditer(source))
if len(matches) != 1:
    raise SystemExit(f"Expected exactly one run_api_probe function, found {len(matches)}")

source = source[:matches[0].start()] + replacement + source[matches[0].end():]
main.write_text(source)
print("Detached streaming API requests from Home")
