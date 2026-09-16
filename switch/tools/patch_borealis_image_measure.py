from pathlib import Path

header_path = Path("switch/borealis/library/include/borealis/views/image.hpp")
source_path = Path("switch/borealis/library/lib/views/image.cpp")

header = header_path.read_text()
source = source_path.read_text()

old_enum = '''    // The image is either cropped (not enough space) or untouched (too much space)\n    CROP,\n};\n'''
new_enum = '''    // The image is either cropped (not enough space) or untouched (too much space)\n    CROP,\n    // The image fills the view while conserving aspect ratio; overflow is clipped.\n    FILL,\n};\n'''

if header.count(old_enum) != 1:
    raise SystemExit("Expected exactly one Borealis ImageScalingType enum block")
header = header.replace(old_enum, new_enum, 1)

old_registration = '''            { "fit", ImageScalingType::FIT },\n            { "stretch", ImageScalingType::STRETCH },\n            { "crop", ImageScalingType::CROP },\n'''
new_registration = '''            { "fit", ImageScalingType::FIT },\n            { "stretch", ImageScalingType::STRETCH },\n            { "crop", ImageScalingType::CROP },\n            { "fill", ImageScalingType::FILL },\n'''

if source.count(old_registration) != 1:
    raise SystemExit("Expected exactly one Borealis image scaling XML registration block")
source = source.replace(old_registration, new_registration, 1)

old_draw_open = '''    if (this->scalingType == ImageScalingType::CROP)\n    {\n        nvgSave(vg);\n        nvgIntersectScissor(vg, x, y, width, height);\n    }\n'''
new_draw_open = '''    if (this->scalingType == ImageScalingType::CROP || this->scalingType == ImageScalingType::FILL)\n    {\n        nvgSave(vg);\n        nvgIntersectScissor(vg, x, y, width, height);\n    }\n'''

if source.count(old_draw_open) != 1:
    raise SystemExit("Expected exactly one Borealis CROP draw clipping block")
source = source.replace(old_draw_open, new_draw_open, 1)

old_draw_close = '''    if (this->scalingType == ImageScalingType::CROP)\n        nvgRestore(vg);\n'''
new_draw_close = '''    if (this->scalingType == ImageScalingType::CROP || this->scalingType == ImageScalingType::FILL)\n        nvgRestore(vg);\n'''

if source.count(old_draw_close) != 1:
    raise SystemExit("Expected exactly one Borealis CROP draw restore block")
source = source.replace(old_draw_close, new_draw_close, 1)

old_crop = '''        case ImageScalingType::CROP:\n            if (viewAspectRatio < imageAspectRatio)\n            {\n                this->imageHeight = this->originalImageHeight;\n                this->imageWidth  = this->imageHeight * imageAspectRatio;\n                this->imageX      = (width - this->imageWidth) / 2.0F;\n                this->imageY      = 0;\n            }\n            else\n            {\n                this->imageWidth  = this->originalImageWidth;\n                this->imageHeight = this->imageWidth * imageAspectRatio;\n                this->imageY      = (height - this->imageHeight) / 2.0F;\n                this->imageX      = 0;\n            }\n            break;\n'''
new_crop = '''        case ImageScalingType::CROP:\n            if (viewAspectRatio < imageAspectRatio)\n            {\n                this->imageHeight = this->originalImageHeight;\n                this->imageWidth  = this->imageHeight * imageAspectRatio;\n                this->imageX      = (width - this->imageWidth) / 2.0F;\n                this->imageY      = 0;\n            }\n            else\n            {\n                this->imageWidth  = this->originalImageWidth;\n                this->imageHeight = this->imageWidth * imageAspectRatio;\n                this->imageY      = (height - this->imageHeight) / 2.0F;\n                this->imageX      = 0;\n            }\n            break;\n        case ImageScalingType::FILL:\n        {\n            if (viewAspectRatio < imageAspectRatio)\n            {\n                this->imageHeight = this->getHeight();\n                this->imageWidth  = this->imageHeight * imageAspectRatio;\n                this->imageX      = (width - this->imageWidth) / 2.0F;\n                this->imageY      = 0;\n            }\n            else\n            {\n                this->imageWidth  = this->getWidth();\n                this->imageHeight = this->imageWidth * imageAspectRatio;\n                this->imageY      = (height - this->imageHeight) / 2.0F;\n                this->imageX      = 0;\n            }\n            break;\n        }\n'''

if source.count(old_crop) != 1:
    raise SystemExit("Expected exactly one Borealis CROP bounds block")
source = source.replace(old_crop, new_crop, 1)

header_path.write_text(header)
source_path.write_text(source)

print("Added Borealis ImageScalingType::FILL using the fixed-slot poster approach used by NX-torrent-player.")
