from pathlib import Path

path = Path("switch/borealis/library/lib/views/image.cpp")
text = path.read_text()

old_width = '''    else if (widthMode == YGMeasureModeAtMost)\n        if (type == ImageScalingType::FIT)\n            return originalWidth;\n        else\n            return std::min(width, originalWidth);\n'''
new_width = '''    else if (widthMode == YGMeasureModeAtMost)\n        if (type == ImageScalingType::FIT)\n            return originalWidth;\n        else if (type == ImageScalingType::CROP)\n            return width;\n        else\n            return std::min(width, originalWidth);\n'''

old_height = '''    else if (heightMode == YGMeasureModeAtMost)\n        if (type == ImageScalingType::FIT)\n            return originalHeight;\n        else\n            return std::min(height, originalHeight);\n'''
new_height = '''    else if (heightMode == YGMeasureModeAtMost)\n        if (type == ImageScalingType::FIT)\n            return originalHeight;\n        else if (type == ImageScalingType::CROP)\n            return height;\n        else\n            return std::min(height, originalHeight);\n'''

if text.count(old_width) != 1:
    raise SystemExit("Expected exactly one Borealis CROP width-measure block")
if text.count(old_height) != 1:
    raise SystemExit("Expected exactly one Borealis CROP height-measure block")

text = text.replace(old_width, new_width, 1)
text = text.replace(old_height, new_height, 1)
path.write_text(text)

print("Patched Borealis Image CROP measurement to honor the requested view bounds.")
