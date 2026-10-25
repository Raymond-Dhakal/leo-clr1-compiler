#!/usr/bin/env python3
"""Regenerate web/examples.js and web/grammar.txt from the repository sources.
Run from anywhere:  python3 tools/make_web.py"""
import json, pathlib, shutil, re
root = pathlib.Path(__file__).resolve().parent.parent
titles = {
 "01_arithmetic": "Arithmetic operators", "02_precedence": "Precedence and associativity",
 "03_if_else": "IF / ELSE", "04_while_loop": "WHILE loop", "05_repeat_until": "REPEAT UNTIL",
 "06_logical": "Logical operators", "07_input_output": "INPUT and PRINT",
 "08_combined": "Factorial, prime, gcd", "09_for_loops": "FOR loops",
 "10_prompts_and_print": "Prompts and PRINT lists"}
stdin = {"07_input_output": "3 8", "08_combined": "12", "10_prompts_and_print": "4 5 TRUE"}
out = []
for f in sorted((root / "examples").glob("*.leo")):
    out.append({"id": f.stem, "title": titles.get(f.stem, f.stem),
                "source": f.read_text(), "stdin": stdin.get(f.stem, "")})
(root / "web" / "examples.js").write_text(
    "// Generated from ../examples/*.leo by tools/make_web.py. Do not edit by hand.\n"
    "export const EXAMPLES = " + json.dumps(out, indent=1) + ";\n")
shutil.copyfile(root / "language" / "grammar.txt", root / "web" / "grammar.txt")
print("web/examples.js: %d examples; web/grammar.txt updated" % len(out))
