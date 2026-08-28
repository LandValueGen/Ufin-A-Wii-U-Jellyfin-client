#!/usr/bin/env python3
# Converts a binary file into a C byte-array header, for embedding
# compiled shader (.gsh) files directly into the Ufin executable rather
# than loading them from disk at runtime.
#
# Usage: bin2h.py <input_bin> <output_header> <symbol_name>

import sys

def main():
    if len(sys.argv) != 4:
        print("usage: bin2h.py <input_bin> <output_header> <symbol_name>", file=sys.stderr)
        sys.exit(1)

    input_path, output_path, symbol = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(input_path, "rb") as f:
        data = f.read()

    with open(output_path, "w") as f:
        f.write(f"// Auto-generated from {input_path} -- do not edit by hand.\n")
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"static const uint32_t {symbol}_size = {len(data)};\n")
        f.write(f"static const unsigned char {symbol}_data[] = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")

    print(f"wrote {output_path} ({len(data)} bytes as {symbol}_data)")

if __name__ == "__main__":
    main()
