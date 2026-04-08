#!/usr/bin/env python3
"""
Extract montecarlo font from kernel/graphics/font.c and generate BDF format.
"""

import re
import sys
from pathlib import Path

# Read the font.c file
font_c_path = Path(__file__).parent.parent / "kernel" / "graphics" / "font.c"
if not font_c_path.exists():
    print(f"Error: {font_c_path} not found")
    sys.exit(1)

with open(font_c_path, 'r') as f:
    content = f.read()

# Extract builtin_font_8x16
font_pattern = r'static const uchar builtin_font_8x16\[FONT_CHARS \* FONT_HEIGHT\] = \{(.*?)\};'
match = re.search(font_pattern, content, re.DOTALL)
if not match:
    print("Error: Could not find builtin_font_8x16 data")
    sys.exit(1)

font_data_str = match.group(1)

# Parse hex values - only get full lines that start with digits
hex_values = []
for line in font_data_str.split('\n'):
    # Extract hex values like 0x00, 0xFF, etc., but skip comment lines
    if line.strip().startswith('/*') or line.strip().startswith('//'):
        continue
    values = re.findall(r'0x[0-9a-fA-F]{2}', line)
    if values:
        hex_values.extend([int(v, 16) for v in values])

# We expect 128 characters * 16 bytes per character
if len(hex_values) != 128 * 16:
    print(f"Error: Expected {128*16} bytes, got {len(hex_values)}")
    print(f"Debug: First 50 values: {hex_values[:50]}")
    print(f"Debug: Last 50 values: {hex_values[-50:]}")
    sys.exit(1)

# Extract montecarlo overrides
overrides = {}
override_pattern = r"{ '(.)', montecarlo_glyph_(\w+) },"
for match in re.finditer(override_pattern, content):
    char = match.group(1)
    name = match.group(2)
    
    # Find the glyph definition
    glyph_pattern = rf'static const uchar montecarlo_glyph_{name}\[FONT_HEIGHT\] = \{{\s*(.*?)\s*\}};'
    glyph_match = re.search(glyph_pattern, content, re.DOTALL)
    if glyph_match:
        glyph_str = glyph_match.group(1)
        values = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', glyph_str)]
        if len(values) == 16:
            # Map character to ASCII code
            ascii_code = ord(char)
            overrides[ascii_code] = values

print(f"Extracted {len(hex_values)} bytes of font data")
print(f"Found {len(overrides)} montecarlo glyph overrides")

# Generate BDF file
bdf_content = """STARTFONT 2.1
FONT -montecarlo-montecarlo-medium-r-normal--16-120-100-100-m-80-iso8859-1
SIZE 16 100 100
FONTBOUNDINGBOX 8 16 0 -2
STARTPROPERTIES 2
FONT_ASCENT 12
FONT_DESCENT 4
ENDPROPERTIES
CHARS 128
"""

# For each character code
for char_code in range(128):
    # Get the glyph data
    if char_code in overrides:
        glyph_bytes = overrides[char_code]
    else:
        glyph_bytes = hex_values[char_code * 16:(char_code + 1) * 16]
    
    if len(glyph_bytes) != 16:
        print(f"Warning: Invalid glyph data for character {char_code}")
        continue
    
    # Convert each byte to hex bitmap format for BDF
    # Each byte represents a row, with MSB as leftmost pixel
    bitmap_lines = []
    for byte_val in glyph_bytes:
        # Convert byte to 2-digit hex
        bitmap_lines.append(f"{byte_val:02X}")
    
    # Write character entry
    bdf_content += f"""STARTCHAR chr{char_code}
ENCODING {char_code}
SWIDTH 500 0
DWIDTH 8 0
BBX 8 16 0 -2
BITMAP
"""
    for line in bitmap_lines:
        bdf_content += f"{line}\n"
    
    bdf_content += "ENDCHAR\n"

bdf_content += "ENDFONT\n"

# Write BDF file to targetfs
targetfs_fonts = Path(__file__).parent.parent / "targetfs" / "usr" / "share" / "fonts" / "PCF"
targetfs_fonts.mkdir(parents=True, exist_ok=True)

bdf_path = targetfs_fonts / "montecarlo-8x16.bdf"
with open(bdf_path, 'w') as f:
    f.write(bdf_content)

print(f"Generated BDF file: {bdf_path}")

# Try to compile to PCF format if bdftopcf is available
try:
    import subprocess
    pcf_path = targetfs_fonts / "montecarlo-8x16.pcf"
    result = subprocess.run(['bdftopcf', str(bdf_path), '-o', str(pcf_path)], 
                          capture_output=True, text=True)
    if result.returncode == 0:
        print(f"Generated PCF file: {pcf_path}")
        # Gzip the PCF file (X11 prefers compressed fonts)
        pcf_gz_path = Path(str(pcf_path) + '.gz')
        subprocess.run(['gzip', '-f', str(pcf_path)], check=True)
        print(f"Compressed: {pcf_gz_path}")
    else:
        print(f"Warning: bdftopcf not available, keeping BDF format")
        print(f"Error: {result.stderr}")
except (FileNotFoundError, subprocess.CalledProcessError) as e:
    print(f"Warning: Could not compile to PCF format: {e}")
    print("X11 can still use BDF format directly")
