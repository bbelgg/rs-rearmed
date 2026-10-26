"""C prototypes of the macOS SDK (and the project's shim libraries) from clang's AST dump.

`load(sources)` compiles each translation unit with `clang -fsyntax-only -Xclang -ast-dump`
and reads the top-level FunctionDecl / TypedefDecl nodes: parameter types come
canonical from the ParmVarDecls, return types are resolved through the typedefs.
`classify` turns a canonical type string into an ABI description.
"""
import os
import re
import subprocess


QUAL = re.compile(r"\b(const|volatile|restrict|__restrict|_Nonnull|_Nullable|_Null_unspecified|__nonnull|__nullable|"
                  r"__strong|__weak|__unsafe_unretained|__autoreleasing|_Noescape|__kindof|__unused)\b")
INTS = {
    "char": (8, True), "signed char": (8, True), "unsigned char": (8, False),
    "short": (16, True), "unsigned short": (16, False),
    "int": (32, True), "unsigned int": (32, False),
    "long": (64, True), "unsigned long": (64, False),
    "long long": (64, True), "unsigned long long": (64, False),
    "__int128": (128, True), "unsigned __int128": (128, False),
}


class Protos:
    def __init__(self):
        self.funcs = {}     # name -> dict(ret=str, params=[(sugar, canon)], variadic=bool, src=str)
        self.typedefs = {}  # name -> canonical string
        self.enums = set()  # typedef names of enums
        self.records = {}   # canonical name -> dict(kind='struct'|'union', fields=[(name, canonical type)])
        self._anon = None

    # --- AST text parsing -----------------------------------------------------
    @staticmethod
    def _split(line):
        """(head, type, canonical or None) of a declaration line."""
        i = line.find(" '")
        if i < 0:
            return None
        j = line.find("'", i + 2)
        t = line[i + 2:j]
        canon = None
        if line[j + 1:j + 3] == ":'":
            k = line.find("'", j + 3)
            canon = line[j + 3:k]
        return line[:i], t, canon

    def add_ast(self, lines, src):
        cur = None
        self._rec = None
        for l in lines:
            if l.startswith(("|-FunctionDecl", "`-FunctionDecl")):
                cur = None
                if " invalid " in l:
                    continue
                sp = self._split(l)
                if not sp:
                    continue
                name = sp[0].rsplit(" ", 1)[1]
                if name in self.funcs or name.startswith("col:") or name.startswith("line:"):
                    continue
                cur = dict(type=sp[1], params=[], src=src)
                self.funcs[name] = cur
            elif l.startswith(("|-RecordDecl", "`-RecordDecl")):
                cur = None
                self._anon = None
                m = re.search(r" (struct|union)(?: (\S+))? definition$", l.rstrip())
                if m:
                    rec = dict(kind=m.group(1), fields=[])
                    if m.group(2):
                        self.records.setdefault("%s %s" % (m.group(1), m.group(2)), rec)
                    else:
                        self._anon = rec
                    self._rec = rec
                else:
                    self._rec = None
            elif l[2:].startswith(("|-FieldDecl", "`-FieldDecl")) and l[:2] in ("| ", "  ") and self._rec is not None:
                sp = self._split(l)
                if sp:
                    fname = sp[0].rsplit(" ", 1)[1]
                    self._rec["fields"].append((fname, sp[2] or sp[1], " bitfield" in l))
            elif l.startswith(("|-TypedefDecl", "`-TypedefDecl")):
                cur = None
                sp = self._split(l)
                if sp:
                    name = sp[0].rsplit(" ", 1)[1]
                    self.typedefs.setdefault(name, sp[2] or sp[1])
                    if sp[1].startswith("enum "):
                        self.enums.add(name)
                    if self._anon is not None and sp[2] == name:  # typedef struct {...} Name;
                        self.records.setdefault(name, self._anon)
                self._anon = None
                self._rec = None
            elif cur is not None and l[2:].startswith(("|-ParmVarDecl", "`-ParmVarDecl")) and l[:2] in ("| ", "  "):
                sp = self._split(l)
                cur["params"].append((sp[1], sp[2] or sp[1]) if sp else ("?", "?"))
            elif l[:2] in ("|-", "`-"):
                cur = None
                if not l.startswith(("|-FieldDecl",)):
                    self._rec = None

    def add_source(self, path, flags, src, lang="objective-c"):
        out = subprocess.run(["clang", "-fsyntax-only", "-x", lang, "-Xclang", "-ast-dump"] + flags + [path],
                             capture_output=True, text=True)
        self.add_ast(out.stdout.splitlines(), src)
        return out.stderr

    # --- lookup ----------------------------------------------------------------
    def resolve(self, t, depth=0):
        """Resolve typedef names in a type string down to builtin/struct/pointer forms."""
        t = QUAL.sub("", t).strip()
        t = re.sub(r"\s+", " ", t)
        if depth > 12:
            return t
        if t in ("id", "Class", "SEL"):
            return "void *"
        if t in self.enums:
            return "enum " + t
        if t in INTS or t in ("float", "double", "long double", "void", "_Bool", "bool") or "*" in t or "(" in t \
                or t.startswith(("struct ", "union ", "enum ")):
            return t
        if t in self.typedefs and self.typedefs[t] != t:
            return self.resolve(self.typedefs[t], depth + 1)
        return t

    def proto(self, name):
        """dict(ret, params, variadic) with every type canonical, or None."""
        f = self.funcs.get(name)
        if not f:
            return None
        t = f["type"]
        m = re.match(r"^(.*?) \(\*\(", t)  # returns a function pointer (signal)
        if m:
            ret = "void *"
        else:
            ret = t[:t.index(" (")] if " (" in t else t
        variadic = bool(re.search(r"\.\.\.\)", t))
        return dict(ret=self.resolve(ret), params=[self.resolve(c) for _, c in f["params"]], variadic=variadic,
                    src=f["src"], sugar=[s for s, _ in f["params"]])


    # --- ABI classification -----------------------------------------------------------
    def layout(self, t, _depth=0):
        """(size, align, leaves) of an aggregate/scalar type string; leaves are (kind, offset)
        with kind i (integer class), f (float) or d (double). Raises ValueError when unsupported."""
        t = QUAL.sub("", t).strip()
        t = re.sub(r"\s+", " ", t)
        if _depth > 8:
            raise ValueError("nesting")
        m = re.match(r"^(.*?) ?\[(\d+)\]$", t)
        if m:
            size, align, leaves = self.layout(m.group(1), _depth + 1)
            n = int(m.group(2))
            return size * n, align, [(k, o + e * size) for e in range(n) for k, o in leaves]
        if "*" in t or "(^)" in t or "(*)" in t:
            return 8, 8, [("i", 0)]
        if t in INTS:
            bits = INTS[t][0] // 8
            return bits, bits, [("i", 0)] if bits <= 8 else [("i", 0), ("i", 8)]
        if t in ("_Bool", "bool"):
            return 1, 1, [("i", 0)]
        if t == "float":
            return 4, 4, [("f", 0)]
        if t == "double":
            return 8, 8, [("d", 0)]
        if t.startswith("enum ") or t in self.enums:
            return 4, 4, [("i", 0)]
        if t in self.typedefs and self.typedefs[t] != t:
            return self.layout(self.typedefs[t], _depth + 1)
        rec = self.records.get(t)
        if rec is None:
            raise ValueError("unknown type " + t)
        size = align = 0
        leaves = []
        off = 0
        for fname, ft, bit in rec["fields"]:
            if bit:
                raise ValueError("bit-field")
            fs, fa, fl = self.layout(ft, _depth + 1)
            if rec["kind"] == "union":
                size = max(size, fs)
                leaves += fl
            else:
                off = (off + fa - 1) // fa * fa
                leaves += [(k, o + off) for k, o in fl]
                off += fs
                size = off
            align = max(align, fa)
        align = max(align, 1)
        return (size + align - 1) // align * align, align, leaves

    def cdef(self, t, done, out):
        """Append the arm64-side C definition of record type t (and the records it nests) to out."""
        t = QUAL.sub("", t).strip()
        m = re.match(r"^(.*?) ?\[(\d+)\]$", t)
        if m:
            return self.cdef(m.group(1), done, out)
        if t in self.typedefs and self.typedefs[t] != t and t not in self.records:
            return self.cdef(self.typedefs[t], done, out)
        rec = self.records.get(t)
        if rec is None or t in done:
            return
        done.add(t)
        body = []
        for fname, ft, _ in rec["fields"]:
            self.cdef(ft, done, out)
            body.append("  " + self.cdecl(ft, fname or "anon%d" % len(body)) + ";")
        out.append("%s %s {\n%s\n};" % (rec["kind"], self.cname(t), "\n".join(body)))

    @staticmethod
    def cname(t):
        return "rc_" + re.sub(r"\W", "_", t.replace("struct ", "").replace("union ", ""))

    def cdecl(self, t, name):
        """C declaration of `name` with canonical type t (scalars by width, records by rc_ name)."""
        t = QUAL.sub("", t).strip()
        m = re.match(r"^(.*?) ?\[(\d+)\]$", t)
        if m:
            return "%s %s[%s]" % (self.cdecl(m.group(1), "").strip(), name, m.group(2))
        if "*" in t or "(^)" in t or "(*)" in t:
            return "void *" + name
        if t in INTS:
            bits, signed = INTS[t]
            return "%s%sint%d_t %s" % ("" if signed else "u", "", bits, name) if bits != 128 else \
                "%s__int128 %s" % ("" if signed else "unsigned ", name)
        if t in ("float", "double"):
            return "%s %s" % (t, name)
        if t in ("_Bool", "bool"):
            return "_Bool " + name
        if t.startswith("enum ") or t in self.enums:
            return "int32_t " + name
        if t in self.typedefs and t not in self.records and self.typedefs[t] != t:
            return self.cdecl(self.typedefs[t], name)
        rec = self.records.get(t)
        return "%s %s %s" % (rec["kind"], self.cname(t), name)

    def classify(self, t):
        """ABI description of a canonical type string.

        kind: void int bool ptr fnptr f32 f64 f80 i128 vec128 agg unknown
        """
        t = QUAL.sub("", t).strip()
        t = re.sub(r"\s+", " ", t)
        if t == "void":
            return dict(kind="void")
        # before the pointer test: "4 * sizeof(float)" contains a *.
        # 4 x float vectors: x86 vFloat spells "vector_size(4 * sizeof(float))",
        # the arm64 SDK float32x4_t / simd_float4 / __attribute__((neon_vector_type(4)))
        if ("vector_size" in t and "float" in t and ("4 * sizeof(float)" in t or "16" in t)) or \
                t in ("vFloat", "float32x4_t", "simd_float4", "__m128") or \
                ("neon_vector_type(4)" in t and "float" in t):
            return dict(kind="vec128")
        if "(*)" in t or "(^)" in t:
            return dict(kind="fnptr")
        if "*" in t or t == "__builtin_va_list" or t.endswith("[]"):
            return dict(kind="ptr", va=t == "__builtin_va_list")
        if t in ("_Bool", "bool"):
            return dict(kind="bool")
        if t in INTS:
            bits, signed = INTS[t]
            return dict(kind="i128" if bits == 128 else "int", bits=bits, signed=signed)
        if t == "float":
            return dict(kind="f32")
        if t == "double":
            return dict(kind="f64")
        if t == "long double":
            return dict(kind="f80")
        if t.startswith("enum ") or t in self.enums:
            return dict(kind="int", bits=32, signed=False)
        try:
            size, align, leaves = self.layout(t)
        except ValueError:
            return dict(kind="unknown", text=t)
        if size == 0:
            return dict(kind="unknown", text=t)
        defs = []
        self.cdef(t, set(), defs)
        return dict(kind="agg", name=self.cname(t)[3:], cdefs=defs, size=size, align=align,
                    cls=aggregate_class(size, leaves))


def aggregate_class(size, leaves):
    """SysV class of a struct: 0 = MEMORY, else (n eightbytes, sse0, sse1)."""
    if size > 16:
        return 0
    n = (size + 7) // 8
    sse = []
    for k in range(n):
        ks = [kind for kind, off in leaves if off // 8 == k]
        sse.append(1 if ks and all(x in "fd" for x in ks) else 0)
    return (n, sse[0], sse[1] if n > 1 else 0)
