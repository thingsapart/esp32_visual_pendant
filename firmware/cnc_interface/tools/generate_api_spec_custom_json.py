#!/usr/bin/env python3
"""
Lightweight C header analyzer to extract enums, simple #defines and function
prototypes and emit a JSON descriptor plus a stub header.

This intentionally avoids full C compilation by using a small parser based on
regex + ast for simple enum value evaluation. It's permissive and aimed at
headers with well-formed enums, simple #defines and prototypes.

Usage:
  python generate_api_spec_custom_json.py -o path/to/ui_components.json --stub-out path/to/stub.h files...

Notes:
 - Not a replacement for clang/libclang but works without requiring a C toolchain.
 - Supports typed/anonymous enums and auto-increment values; supports hex/dec
   and simple bitwise/arithmetic expressions referencing earlier enum names.
"""
import argparse
import json
import re
import sys
import ast
from pathlib import Path


def strip_comments(text: str) -> str:
    """Remove C-style /* ... */ and C++-style //... comments from text."""
    # remove block comments
    text = re.sub(r"/\*[\s\S]*?\*/", "", text)
    # remove line comments
    text = re.sub(r"//.*$", "", text, flags=re.MULTILINE)
    return text


def safe_eval(expr, names):
    """Evaluate a simple integer expression safely using ast.
    Allowed nodes: Expression, BinOp, UnaryOp, Num, Name, Paren, operators + - * / << >> | & ^ ~.
    Names are looked up in the `names` dict.
    Returns int or raises ValueError.
    """
    node = ast.parse(expr, mode="eval")

    allowed_ops = (
        ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod,
        ast.LShift, ast.RShift, ast.BitOr, ast.BitAnd, ast.BitXor,
        ast.UAdd, ast.USub, ast.Invert
    )

    def _eval(n):
        if isinstance(n, ast.Expression):
            return _eval(n.body)
        if isinstance(n, ast.Constant):
            if isinstance(n.value, int):
                return n.value
            raise ValueError("non-int constant")
        if isinstance(n, ast.Num):
            return n.n
        if isinstance(n, ast.BinOp):
            if not isinstance(n.op, allowed_ops):
                raise ValueError("disallowed operator")
            left = _eval(n.left)
            right = _eval(n.right)
            return _apply_binop(left, right, n.op)
        if isinstance(n, ast.UnaryOp):
            if not isinstance(n.op, allowed_ops):
                raise ValueError("disallowed unary op")
            val = _eval(n.operand)
            return _apply_unary(val, n.op)
        if isinstance(n, ast.Name):
            if n.id in names:
                return names[n.id]
            raise ValueError(f"unknown name {n.id}")
        raise ValueError("unsupported AST node")

    def _apply_binop(a, b, op):
        if isinstance(op, ast.Add):
            return a + b
        if isinstance(op, ast.Sub):
            return a - b
        if isinstance(op, ast.Mult):
            return a * b
        if isinstance(op, ast.Div):
            return a // b
        if isinstance(op, ast.Mod):
            return a % b
        if isinstance(op, ast.LShift):
            return a << b
        if isinstance(op, ast.RShift):
            return a >> b
        if isinstance(op, ast.BitOr):
            return a | b
        if isinstance(op, ast.BitAnd):
            return a & b
        if isinstance(op, ast.BitXor):
            return a ^ b
        raise ValueError("unsupported binop")

    def _apply_unary(a, op):
        if isinstance(op, ast.UAdd):
            return +a
        if isinstance(op, ast.USub):
            return -a
        if isinstance(op, ast.Invert):
            return ~a
        raise ValueError("unsupported unaryop")

    return int(_eval(node))


ENUM_RE = re.compile(r"typedef\s+enum\s*(?:\w+)?\s*\{([\s\S]*?)\}\s*(\w+)\s*;", re.MULTILINE)
ENUM_SIMPLE_RE = re.compile(r"enum\s+(\w+)\s*\{([\s\S]*?)\}\s*;", re.MULTILINE)
DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$", re.MULTILINE)
FUNC_PROTO_RE = re.compile(r"^\s*([A-Za-z_\*][A-Za-z0-9_\s\*\(\)\[\]]*?)\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{}]*)\)\s*;", re.MULTILINE)
STRUCT_RE = re.compile(r"typedef\s+struct\s*(?:\w+)?\s*\{([\s\S]*?)\}\s*(\w+)\s*;", re.MULTILINE)
INCLUDE_RE = re.compile(r"^\s*#\s*include\s+(.*)$", re.MULTILINE)


def parse_enums(text, enums):
    # typedef enum { ... } name;
    for m in ENUM_RE.finditer(text):
        body, name = m.group(1), m.group(2)
        process_enum_body(name, body, enums)
    for m in ENUM_SIMPLE_RE.finditer(text):
        name, body = m.group(1), m.group(2)
        process_enum_body(name, body, enums)


def parse_structs(text, structs):
    # typedef struct { ... } name;
    for m in STRUCT_RE.finditer(text):
        name = m.group(2)
        structs.add(name)


def process_enum_body(name, body, enums):
    # split by commas but be tolerant of trailing commas and comments
    lines = re.split(r",\s*(?![^{}]*\})", body)
    value_map = {}
    cur = 0
    for item in lines:
        item = item.strip()
        if not item:
            continue
        # strip comments
        item = re.sub(r"/\*.*?\*/", "", item)
        item = re.sub(r"//.*$", "", item, flags=re.MULTILINE)
        if not item:
            continue
        if "=" in item:
            parts = item.split("=", 1)
            ident = parts[0].strip()
            expr = parts[1].strip()
            try:
                val = safe_eval(expr, value_map)
            except Exception:
                try:
                    # try simple int parsing (hex/dec)
                    val = int(expr, 0)
                except Exception:
                    val = None
            if val is None:
                cur = 0
            else:
                cur = val
        else:
            ident = item.strip()
        if ident:
            value_map[ident] = cur
        cur = (cur + 1) if isinstance(cur, int) else cur
    enums[name] = value_map


def parse_defines(text, constants):
    for m in DEFINE_RE.finditer(text):
        name, rest = m.group(1), m.group(2)
        # skip header-guard defines entirely
        if name.upper().endswith('_H') or name.upper().endswith('_H__'):
            continue
        # ignore function-like defines
        if "(" in name:
            print("DROPPED: NAME, REST:",name,rest)
            continue
        # keep the RHS verbatim (but strip trailing comments/whitespace)
        rest = rest.split("//")[0].rstrip()
        if rest == "":
            print("DROPPED - EMPTY: NAME, REST:",name,rest)
            continue
        # Include defines that alias to something else: multi-token RHS,
        # or RHS that starts with a preprocessor directive (#include, #ifdef),
        # or contains a quoted string.
        tokens = re.split(r"\s+", rest.strip())
        # Only accept preprocessor RHS if it's an #include (not #ifdef/#endif/etc.)
        is_preproc_include = rest.lstrip().startswith('#include')
        print("USING: NAME, REST:",name,rest, len(tokens) > 1,
              is_preproc_include, '"' in rest,  ("(" in rest and ")" in rest))
        if len(tokens) > 1 or is_preproc_include or '"' in rest or ("(" in rest and ")" in rest):
            constants[name] = rest.strip()


def parse_includes(text, includes):
    for m in INCLUDE_RE.finditer(text):
        inc = m.group(1).strip()
        # keep the spelling as in source, e.g. "<...>" or "\"...\""
        includes.add(f"#include {inc}")


def parse_functions(text, functions, struct_types):
    for m in FUNC_PROTO_RE.finditer(text):
        ret, name, args = m.group(1).strip(), m.group(2).strip(), m.group(3).strip()
        # debug: prototype matched (suppressed in normal runs)
        # print(f"FOUND_PROTO: {name} ret=<{ret}> args=<{args}>", file=sys.stderr)
        # skip typedefs and function pointer typedefs
        if ret.startswith("typedef"):
            continue
        # skip macros disguised as prototypes
        if name.upper().startswith("MACRO"):
            continue
        # helper: extract a param's type (drop the trailing parameter name)
        def _param_type(param):
            p = param.strip()
            # drop any default values
            p = p.split('=')[0].strip()
            # strip trailing array suffixes
            p = re.sub(r"\s*\[[^\]]*\]\s*$", "", p)
            # remove a trailing parameter name if present (identifier at end)
            m2 = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*$', p)
            if m2:
                # ensure the match is not part of a type token (e.g. function pointer)
                name = m2.group(1)
                # remove the name only if there's a separating space or '*' before it
                # e.g. 'int x', 'lv_obj_t *obj', 'const foo_t &bar'
                # find the position where the name starts
                start = m2.start(1)
                if start > 0 and (p[start-1].isspace() or p[start-1] in ('*', '&')):
                    return p[:start].rstrip()
            return p

        def _is_allowed(ret_type, arg_types):
            # Reject functions that return any parsed typedef'd struct
            for st in struct_types:
                if re.search(r"\b" + re.escape(st) + r"\b", ret_type):
                    return False

            # If return type is a pointer, only allow pointers to lv_obj_t
            if '*' in ret_type:
                if 'lv_obj_t' not in ret_type:
                    return False
            # inspect arguments
            for a in arg_types:
                at = a.strip()
                if at == 'void' or at == '':
                    continue
                # if arg mentions a callback type ending in cb_t -> reject
                if re.search(r"\b[A-Za-z_][A-Za-z0-9_]*cb_t\b", at):
                    return False
                ptype = _param_type(at)
                # if the parameter type references any parsed struct type, reject
                for st in struct_types:
                    if re.search(r"\b" + re.escape(st) + r"\b", ptype):
                        return False
                # allow void * arguments
                if re.search(r"\bvoid\s*\*", ptype):
                    continue
                # if pointer argument, only allow pointers to lv_obj_t
                if '*' in ptype:
                    if 'lv_obj_t' not in ptype:
                        return False
                # otherwise primitive/struct types are allowed
            return True

        # normalize args
        if args.strip() == "void" or args.strip() == "":
            arg_list = []
        else:
            # split by commas but ignore commas inside parentheses
            parts = []
            depth = 0
            cur = []
            for ch in args:
                if ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                if ch == ',' and depth == 0:
                    parts.append(''.join(cur).strip())
                    cur = []
                else:
                    cur.append(ch)
            if cur:
                parts.append(''.join(cur).strip())
            arg_list = [p for p in parts if p]
        # normalize argument types: strip parameter names and collapse spaces around '*'
        norm_args = []
        for p in arg_list:
            ptype = _param_type(p)
            # collapse spaces and remove space before/after '*'
            ptype = re.sub(r"\s*\*\s*", "*", ptype)
            ptype = " ".join(ptype.split())
            norm_args.append(ptype)
        # only include "simple" functions per rules: no pointer returns except to lv_obt_t,
        # no pointer arguments except to lv_obt_t (but allow void *), and no args ending with cb_t
        # normalize return type as well (collapse spaces around '*')
        ret_norm = re.sub(r"\s*\*\s*", "*", ret)
        ret_norm = " ".join(ret_norm.split())

        if _is_allowed(ret, arg_list):
            functions[name] = {"return_type": ret_norm, "args": norm_args, "args_named": arg_list}


def generate_stub(header_path, constants, enums, functions, includes=None):
    guard = header_path.name.replace('.', '_').upper() + '_'
    lines = []
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    # defines
    # Do not emit any #include or other preprocessor lines in the stub.
    # Only emit simple defines collected in `constants`.
    for k, v in constants.items():
        if isinstance(v, str) and v.lstrip().startswith('#'):
            continue
        lines.append(f"#define {k} {v}")
    lines.append("")
    # enums
    for ename, members in enums.items():
        lines.append(f"typedef enum {{")
        for nm, val in members.items():
            if val is None:
                lines.append(f"    {nm},")
            else:
                lines.append(f"    {nm} = {val},")
        lines.append(f"}} {ename};")
        lines.append("")
    # function stubs as static inline minimal implementations
    for fname, meta in functions.items():
        ret = meta["return_type"]
        # prefer args with names for stub output when available
        args = meta.get("args_named", meta.get("args", []))
        args_str = ', '.join(args) if args else 'void'
        # pick a trivial return value for common types
        ret_val = '0'
        if 'void' in ret:
            body = '    (void)0;'
            ret_stmt = ''
        else:
            body = f'    return {ret_val};'
            ret_stmt = ''
        lines.append(f'static inline {ret} {fname}({args_str}) {{')
        lines.append(body)
        lines.append('}')
        lines.append('')

    lines.append(f"#endif /* {guard} */")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description='Generate UI JSON descriptor and stub header from C headers')
    parser.add_argument('files', nargs='+', help='C header files to parse')
    parser.add_argument('-o', '--output', default='ui_components.json', help='JSON output file')
    parser.add_argument('--stub-out', default=None, help='Write a stub header file')
    args = parser.parse_args()

    constants = {}
    enums = {}
    functions = {}
    includes = set()
    structs = set()

    for fp in args.files:
        p = Path(fp)
        if not p.exists():
            print(f"warning: file not found: {fp}", file=sys.stderr)
            continue
        text = p.read_text()
        text = strip_comments(text)
        parse_defines(text, constants)
        parse_includes(text, includes)
        parse_enums(text, enums)
        parse_structs(text, structs)
        parse_functions(text, functions, structs)

    out = {
        "constants": constants,
        "enums": enums,
        "functions": functions,
        "widgets": {},
        "objects": {}
    }

    # includes: dispatch -> stub filename (stripped to basename, placed under include/)
    # and c_gen -> list of headers passed in with a leading src/ or ./src/ stripped
    includes_section = {"dispatch": [], "c_gen": []}
    if args.stub_out:
        includes_section["dispatch"].append(f"include/{Path(args.stub_out).name}")
    for fp in args.files:
        s = fp
        if s.startswith('./src/'):
            s = s[len('./src/'):]
        elif s.startswith('src/'):
            s = s[len('src/'):]
        includes_section["c_gen"].append(s)

    out["includes"] = includes_section

    # DEBUG: show which functions were parsed and serializable form
    # debug prints removed

    out_path = Path(args.output)
    out_path.write_text(json.dumps(out, indent=2, sort_keys=False))
    print(f"Wrote {out_path}")

    if args.stub_out:
        stub_path = Path(args.stub_out)
        stub_src = generate_stub(stub_path, constants, enums, functions, includes=includes)
        stub_path.write_text(stub_src)
        print(f"Wrote stub header {stub_path}")


if __name__ == '__main__':
    main()
