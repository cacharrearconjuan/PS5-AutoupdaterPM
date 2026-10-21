#!/usr/bin/env python3
"""Convierte un fichero en una cabecera C: bin2h.py <entrada> <salida.h> <nombre>"""
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, dst, name = sys.argv[1:]
    with open(src, "rb") as f:
        data = f.read()
    if not data:
        sys.exit(f"{src} está vacío")

    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")

    with open(dst, "w", encoding="ascii") as f:
        f.write("/* Generado por tools/bin2h.py - no editar */\n")
        f.write("#pragma once\n#include <stddef.h>\n\n")
        f.write(f"static const unsigned char {name}[] = {{\n")
        f.write("\n".join(lines))
        f.write("\n    0x00\n};\n")
        f.write(f"static const size_t {name}_len = {len(data)};\n")


if __name__ == "__main__":
    main()
