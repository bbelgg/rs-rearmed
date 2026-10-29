// Hand-written GLSL 1.20 programs for msl_verify --builtin: language features and built-ins
// that the game's shaders don't use (or use rarely), each rendered with GL and Metal and compared.
// Not game-derived; the game's own shaders are checked from the dumps (tools/metal/).
#pragma once

typedef struct {
    const char *name;
    const char *vs, *fs;
    const char *bindings;  // "name=loc,..." or NULL
} msl_case;

#define MSL_CASE_VS_BASIC                                                                       \
    "#version 120\n"                                                                            \
    "attribute vec4 pos;\nattribute vec4 col;\nattribute vec2 uv;\n"                            \
    "varying vec4 v_col;\nvarying vec2 v_uv;\n"                                                 \
    "void main() { gl_Position = pos; v_col = col; v_uv = uv * 2.0 - 0.5; }\n"

static const msl_case msl_cases[] = {
    {"basic-texture", MSL_CASE_VS_BASIC,
     "#version 120\nuniform sampler2D tex;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() { gl_FragColor = texture2D(tex, v_uv) * v_col; }\n",
     "pos=0,col=1,uv=2"},
    // Generic attributes left to the translator's (GL linker-like) assignment.
    {"auto-locations", MSL_CASE_VS_BASIC,
     "#version 120\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() { gl_FragColor = vec4(v_uv, v_col.b, 1.0); }\n",
     "pos=3"},
    {"builtin-attribs-matrices",
     "#version 120\nvarying vec4 c;\nvarying vec3 n;\nvarying vec4 t;\n"
     "void main() {\n"
     "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
     "  n = gl_NormalMatrix * gl_Normal;\n"
     "  c = gl_Color;\n"
     "  t = gl_TextureMatrix[1] * gl_MultiTexCoord1 + gl_MultiTexCoord0 * 0.25;\n"
     "}\n",
     "#version 120\nvarying vec4 c;\nvarying vec3 n;\nvarying vec4 t;\n"
     "void main() { gl_FragColor = vec4(c.rg, normalize(n).z * 0.5 + 0.5, fract(t.x + t.w)); }\n",
     NULL},
    {"ftransform-ff-varyings",
     "#version 120\n"
     "void main() {\n"
     "  gl_Position = ftransform();\n"
     "  gl_FrontColor = gl_Color * 1.5 - 0.25;\n"
     "  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
     "  gl_TexCoord[2] = gl_ModelViewMatrix * gl_Vertex;\n"
     "}\n",
     "#version 120\nuniform sampler2D tex;\n"
     "void main() { gl_FragColor = gl_Color * texture2D(tex, gl_TexCoord[0].xy) + gl_TexCoord[2] * 0.1; }\n",
     NULL},
    {"fragcoord-facing-discard",
     "#version 120\nattribute vec4 pos;\nvarying float k;\n"
     "void main() { gl_Position = vec4(pos.xy, pos.z * 0.5, 1.0); k = pos.x; }\n",
     "#version 120\nvarying float k;\n"
     "void main() {\n"
     "  if (mod(gl_FragCoord.x + gl_FragCoord.y, 7.0) < 1.0) discard;\n"
     "  gl_FragColor = vec4(gl_FragCoord.xy / 64.0, gl_FragCoord.z, gl_FrontFacing ? 1.0 : 0.25);\n"
     "  if (k > 0.5) gl_FragColor.b = gl_FragCoord.w * 0.5;\n"
     "}\n",
     "pos=0"},
    {"perspective-fragcoord-w",
     "#version 120\nattribute vec4 pos;\nvarying vec2 q;\n"
     "void main() { float w = 1.0 + 0.6 * (pos.x + 1.0); gl_Position = vec4(pos.xy * w, pos.z * w * 0.5, w); q = pos.xy; }\n",
     "#version 120\nvarying vec2 q;\n"
     "void main() { gl_FragColor = vec4(gl_FragCoord.w * 0.8, gl_FragCoord.z, q * 0.5 + 0.5); }\n",
     "pos=0"},
    {"frag-depth",
     "#version 120\nattribute vec4 pos;\nvarying float d;\n"
     "void main() { gl_Position = vec4(pos.xy, 0.25, 1.0); d = pos.y * 0.5 + 0.5; }\n",
     "#version 120\nvarying float d;\n"
     "void main() { gl_FragColor = vec4(d); gl_FragDepth = d * 0.9; }\n",
     "pos=0"},
    {"builtin-functions", MSL_CASE_VS_BASIC,
     "#version 120\nuniform vec4 u0;\nuniform vec4 u1;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  vec4 a = v_col * 4.0 - 2.0;\n"
     "  vec4 r;\n"
     "  r.x = mod(a.x, 0.7) + step(0.3, a.y) * 0.1 + smoothstep(-1.0, 1.0, a.z) * 0.2;\n"
     "  r.y = atan(a.y, a.x) / 6.3 + 0.5 + sign(a.w) * 0.05 + abs(a.w) * 0.01;\n"
     "  r.z = clamp(a.z, -0.5, 0.5) + mix(a.x, a.y, 0.25) * 0.1 + length(a.xy) * 0.1 + distance(a, u0) * 0.01;\n"
     "  r.w = fract(a.w * 3.3) * 0.5 + max(a.x, 0.2) * 0.1 + min(a, u1).y * 0.1 + dot(a.xyz, u1.xyz) * 0.01;\n"
     "  vec3 c = cross(normalize(a.xyz + 0.01), vec3(u0.x, 1.0, 0.0));\n"
     "  r.xyz += reflect(c, vec3(0.0, 1.0, 0.0)) * 0.05 + vec3(pow(abs(a.x), 1.5), exp2(a.y * 0.5), log2(abs(a.z) + 1.0)) * 0.05;\n"
     "  r += vec4(sin(a.x), cos(a.y), sqrt(abs(a.z)), inversesqrt(abs(a.w) + 0.5)) * 0.05;\n"
     "  r += vec4(floor(a.x), ceil(a.y), radians(a.z * 30.0), degrees(a.w) * 0.01) * 0.02;\n"
     "  r += vec4(faceforward(vec2(1.0, 0.5), a.xy, vec2(0.3, -0.2)), refract(vec2(0.6, -0.8), vec2(0.0, 1.0), 0.9)) * 0.05;\n"
     "  bvec4 lt = lessThan(a, u0);\n"
     "  if (any(lt) && !all(lt)) r.w += 0.05;\n"
     "  r += vec4(lt) * 0.03 + vec4(not(greaterThanEqual(a, u1))) * 0.02 + vec4(equal(ivec2(a.xy), ivec2(0)).xyxy) * 0.01;\n"
     "  gl_FragColor = r;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"functions-structs-loops", MSL_CASE_VS_BASIC,
     "#version 120\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "struct Light { vec3 dir; float power; };\n"
     "const int N = 3;\n"
     "const float K = 0.5 * 0.5;\n"
     "uniform vec4 lights[N];\n"
     "float g_acc = 0.0;\n"
     "void accumulate(in float x, inout vec4 acc, out float last) { acc += vec4(x); last = x; g_acc += x; }\n"
     "float shade(Light l, vec3 n) { return max(dot(n, normalize(l.dir)), 0.0) * l.power; }\n"
     "vec4 shade(vec4 c) { return c * K; }\n"
     "void main() {\n"
     "  vec3 n = normalize(vec3(v_uv - 0.5, 1.0));\n"
     "  vec4 acc = vec4(0.0);\n"
     "  float last;\n"
     "  for (int i = 0; i < N; i++) {\n"
     "    Light l = Light(lights[i].xyz, lights[i].w);\n"
     "    accumulate(shade(l, n) * 0.3, acc, last);\n"
     "    if (i == 1) continue;\n"
     "  }\n"
     "  int j = 0;\n"
     "  while (j < 5) { j++; if (j > 3) break; }\n"
     "  float f = 0.0;\n"
     "  do { f += 0.125; } while (f < 0.3);\n"
     "  gl_FragColor = acc + shade(v_col) + vec4(float(j) * 0.05, f, last, g_acc * 0.1);\n"
     "  gl_FragColor.ar = gl_FragColor.ra * 0.5;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"matrices", MSL_CASE_VS_BASIC,
     "#version 120\nuniform mat3 m3;\nuniform mat4 m4;\nuniform mat2 m2;\nuniform mat2x3 m23;\n"
     "uniform mat4x2 m42;\nuniform vec3 v3s[2];\nuniform float fs[3];\n"
     "varying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  vec3 a = m3 * v_col.rgb + v3s[1] * fs[2];\n"
     "  vec4 b = v_col * m4;\n"
     "  mat4 mm = m4 * mat4(1.0);\n"
     "  mat3 m3b = mat3(mm);\n"
     "  mat2 r2 = m2 * mat2(v_uv.x, 0.5, -0.5, v_uv.y);\n"
     "  vec3 c = m23 * v_uv;\n"
     "  vec2 d = m42 * v_col;\n"
     "  mat3 cm = matrixCompMult(m3, m3b);\n"
     "  mat4 t = transpose(m4);\n"
     "  mat2 op = outerProduct(v_uv, vec2(1.0, -1.0));\n"
     "  b += t[1] * 0.1 + mm[2] * fs[0];\n"
     "  a += (m3b * a) * 0.1 + cm[0] * 0.05 + c * 0.1;\n"
     "  gl_FragColor = vec4(a, b.x) * 0.25 + vec4(r2[0], r2[1]) * 0.1 + vec4(d, op[1]) * 0.1;\n"
     "  if (m2 == mat2(1.0)) gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"ints-bools-conversions", MSL_CASE_VS_BASIC,
     "#version 120\nuniform int mode;\nuniform bool flag;\nuniform ivec2 iv;\nuniform bvec2 bv;\n"
     "varying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  float x = 1;\n"
     "  int k = int(v_uv.x * 8.0);\n"
     "  vec2 w = vec2(iv) * 0.1 + float(k) * 0.05;\n"
     "  vec4 c = vec4(x * 0.5, w, 0);\n"
     "  if (mode == 2) c.r += 0.25;\n"
     "  c.g += flag ? 0.2 : 0.0;\n"
     "  if (bv.y) c.b += 0.3;\n"
     "  c.a = max(k, 3) * 0.1 + clamp(k, 1, 4) * 0.05;\n"
     "  c += vec4(ivec4(v_col * 10.0)) * 0.02 + vec4(bvec4(v_col.r > 0.5, true, false, flag));\n"
     "  c.rg *= 2; c.b /= 2; c -= 1 * 0.1;\n"
     "  gl_FragColor = c * (k >= 4 ^^ mode > 1 ? 0.5 : 1.0);\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"sampler-arrays-bias", MSL_CASE_VS_BASIC,
     "#version 120\nuniform sampler2D tex[3];\nuniform sampler2D single;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "vec4 fetch(sampler2D s, vec2 c) { return texture2D(s, c, 0.5); }\n"
     "void main() {\n"
     "  int k = int(v_col.r * 2.99);\n"
     "  gl_FragColor = texture2D(tex[int(1.0)], v_uv) * 0.4 + fetch(tex[2], v_uv.yx) * 0.3\n"
     "      + texture2DProj(single, vec3(v_uv, 1.5)) * 0.2 + texture2DProj(tex[0], vec4(v_uv, 0.0, 2.0)) * 0.1;\n"
     "  if (k == 1) gl_FragColor.r = texture2D(tex[0], v_uv * 3.0).g;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"cube-1d-rect-textures", MSL_CASE_VS_BASIC,
     "#version 120\nuniform samplerCube cube;\nuniform sampler1D ramp;\nuniform sampler2DRect rect;\n"
     "varying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  vec3 d = vec3(v_uv * 2.0 - 1.0, v_col.b - 0.5) * vec3(1.0, 1.0, 2.0);\n"
     "  gl_FragColor = textureCube(cube, d) * 0.5 + texture1D(ramp, v_uv.x) * 0.25\n"
     "      + texture2DRect(rect, v_uv * 32.0) * 0.25 + texture1DProj(ramp, vec2(v_uv.y, 2.0)) * 0.1;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"vertex-texture-fetch",
     "#version 120\nattribute vec4 pos;\nuniform sampler2D height;\nvarying vec4 h;\n"
     "void main() {\n"
     "  vec2 c = pos.xy * 0.5 + 0.5;\n"
     "  h = texture2D(height, c) + texture2DLod(height, c.yx, 0.0) * 0.5;\n"
     "  gl_Position = vec4(pos.xy, h.r - 0.5, 1.0);\n"
     "}\n",
     "#version 120\nuniform sampler2D height;\nvarying vec4 h;\n"
     "void main() { gl_FragColor = h * 0.5 + texture2D(height, h.ba) * 0.25; }\n",
     "pos=0"},
    {"shared-uniforms-fragdata",
     "#version 120\nattribute vec4 pos;\nuniform vec4 tint;\nuniform float scale;\nvarying vec4 c;\n"
     "void main() { gl_Position = vec4(pos.xy * scale, 0.0, 1.0); c = tint * pos.x; }\n",
     "#version 120\nuniform vec4 tint;\nuniform vec3 extra;\nvarying vec4 c;\n"
     "void main() { gl_FragData[0] = c + tint * 0.5 + vec4(extra, 0.0); }\n",
     "pos=0"},
    // Scaleform's batched shaders (GL_ARB_draw_instanced); drawn with one instance here.
    {"instance-vertex-id",
     "#version 120\n#extension GL_ARB_draw_instanced : enable\n#extension GL_EXT_gpu_shader4 : enable\nattribute vec4 pos;\nuniform vec4 rows[4];\nvarying vec4 c;\n"
     "void main() { gl_Position = pos + rows[gl_InstanceIDARB] * 0.1; c = vec4(float(gl_VertexID % 7) / 7.0, rows[gl_InstanceIDARB].yzw); }\n",
     "#version 120\nvarying vec4 c;\nvoid main() { gl_FragColor = c; }\n",
     "pos=0"},
    {"scalar-swizzles-precedence", MSL_CASE_VS_BASIC,
     "#version 120\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  float s = v_uv.x;\n"
     "  vec4 v = vec4(s, s, s, s);\n"
     "  v.stp = v_col.bgr;\n"
     "  v.q = -v_uv.y + 2.0 * 3.0 / 4.0 - 1.0;\n"
     "  float i = 0.0;\n"
     "  for (i = -1.0; i <= 1.0; i++) { v.x += i * 0.01; }\n"
     "  vec2 t = vec2(0.0); t.y++; t.x--;\n"
     "  gl_FragColor = v * 0.9 + vec4(t, -t) * 0.05 + vec4(1.0 - 2.0 - 3.0, 8.0 / 2.0 / 2.0, -(-0.5), 0.0) * 0.1;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    // GLSL's copy-in / copy-out of out and inout arguments: swizzles and components (which can't
    // bind to an MSL reference), a global the callee also writes, gl_FragColor, a global array.
    {"out-inout-copy", MSL_CASE_VS_BASIC,
     "#version 120\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "float G = 0.0;\nfloat A[3];\n"
     "void f(out vec2 r) { r = vec2(0.2, 0.7) + v_uv * 0.1; }\n"
     "float g(inout float x) { x *= 0.5; return x + 0.1; }\n"
     "void h(inout float x) { x = 0.3 * v_col.r; G = 0.4; x += G; }\n"
     "void fill(out float a[3], float x) { a[0] = x; a[1] = x * 0.5; a[2] = 0.25; }\n"
     "void sw(inout float a, inout float b) { float t = a; a = b; b = t; }\n"
     "void paint(out vec4 c) { c = v_col.bgra; }\n"
     "void main() {\n"
     "  vec4 c = v_col;\n"
     "  f(c.yx); float r = 0.2 + g(c.z) * 0.5 + g(c[3]);\n"
     "  G = 0.1; h(G);\n"
     "  fill(A, v_col.r);\n"
     "  float b = 0.25; sw(G, b);\n"
     "  paint(gl_FragColor);\n"
     "  gl_FragColor = gl_FragColor * 0.25 + c * 0.25 + vec4(r, G, b, A[1]) * 0.4;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"vs-out-args",
     "#version 120\nattribute vec4 pos;\nattribute vec4 col;\nvarying vec4 v_col;\nvarying vec2 w;\n"
     "void set(out vec4 c, out vec2 x, inout float s) { c = col; x = col.zw; s += 0.1; }\n"
     "vec4 tx(mat4 m[8], vec4 v) { return m[1] * v; }\n"
     "void main() { gl_Position = pos; vec4 k = vec4(0.0, 0.0, col.z, 1.0); set(v_col, w, k.z); v_col.a = w.x + k.z;\n"
     "  v_col += tx(gl_TextureMatrix, gl_MultiTexCoord0) * 0.1; }\n",
     "#version 120\nvarying vec4 v_col;\nvarying vec2 w;\nvoid main() { gl_FragColor = v_col + vec4(w, 0.0, 0.0) * 0.1; }\n",
     "pos=0,col=1"},
    // Arrays: uniform arrays as `in` arguments (constant address space), parameters the callee
    // writes (passed by value), array assignment, .length() of a parameter.
    {"array-params-assign", MSL_CASE_VS_BASIC,
     "#version 120\nuniform float fs[3];\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "float sum(float a[3]) { return a[0] + a[1] + a[2]; }\n"
     "float sum2(float a[3]) { a[1] = 0.0; a[0] += 0.5; return a[0] + a[1]; }\n"
     "float len(vec2 a[2]) { float t = 0.0; for (int i = 0; i < a.length(); i++) t += a[i].x + a[i].y; return t; }\n"
     "void main() {\n"
     "  float l[3]; l[0] = v_col.r; l[1] = v_col.g; l[2] = 0.1;\n"
     "  float m[3]; m = l; float c[3]; c = fs;\n"
     "  vec2 v[2]; v[0] = v_uv; v[1] = v_col.xy;\n"
     "  gl_FragColor = vec4(sum(fs) * 0.2 + 0.5, sum(l) * 0.3, sum2(l) * 0.5 + l[0] * 0.1, (m[1] + c[2]) * 0.25 + len(v) * 0.1);\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    // Matrix arithmetic MSL has no operator for: unary minus, ++/--, + - / with a scalar,
    // matrix / matrix, and their compound assignments; vector *= matrix.
    {"matrix-componentwise", MSL_CASE_VS_BASIC,
     "#version 120\nuniform mat2 m2;\nuniform mat4 m4;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  mat2 a = -m2; mat2 b = m2 + 0.5; mat2 c = 1.0 - m2; mat2 d = 0.5 / m2; mat2 e = m2 / mat2(2.0, 3.0, 4.0, 5.0);\n"
     "  mat2 m = m2; m += 0.5; m /= 2.0; m -= 0.25; m /= mat2(2.0); m *= 2.0; m++; ++m; mat2 o = m--;\n"
     "  vec4 v = v_col; v *= m4; vec2 q = v_uv; q *= m2;\n"
     "  gl_FragColor = vec4(a[0] + b[1], c[0] + e[1]) * 0.25 + vec4(d[1], m[0]) * 0.1 + vec4(o[1], q) * 0.1 + v * 0.1 + 0.5;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    // Constructors from any mix of scalars, vectors and matrices (consumed column by column).
    {"constructor-forms", MSL_CASE_VS_BASIC,
     "#version 120\nuniform mat2 m2;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() {\n"
     "  vec4 a = vec4(m2); vec3 b = vec3(v_col); float e = float(v_col);\n"
     "  mat2 c = mat2(v_col.xyz, 0.5); mat3 d = mat3(v_uv, v_col, 0.1, v_col.wzy); vec3 f = vec3(d); vec2 g = vec2(c);\n"
     "  mat3 h = mat3(vec2(0.1, 0.2), 0.3, vec3(0.4), 0.5, vec2(0.6));\n"
     "  gl_FragColor = (a + vec4(b, e)) * 0.25 + vec4(c[1], d[2].xz) * 0.25 + vec4(f, g.y) * 0.25 + vec4(h[1], h[2].z) * 0.1;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    // Scopes: a local initialised from the outer variable it shadows (C++ would read the new one),
    // a for body redeclaring a function parameter's name, several for-init declarators, global
    // struct variables and global initialisers that read uniforms.
    {"scopes-globals", MSL_CASE_VS_BASIC,
     "#version 120\nuniform vec4 u0;\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "struct S { vec3 a; float b[2]; };\nS gs;\nS arr[2];\n"
     "float x = 0.25;\nvec4 gi = u0 * 0.5 + 0.5;\nconst float K = 2.0;\nfloat h = K * 0.1;\n"
     "float f(float x) { { float x = x * 0.5 + 0.125; return x; } }\n"
     "void main() {\n"
     "  float y = x; { float x = x + 0.5; y += x; }\n"
     "  float s = 0.0; for (int i = 0, j = 3; i < j; i++) s += 0.05;\n"
     "  gs.a = v_col.rgb; gs.b[1] = v_col.a; arr[1] = gs; arr[0].b[0] = 0.5;\n"
     "  gl_FragColor = vec4(y * 0.5, f(v_col.r), s, x) * 0.5 + gi * 0.25 * h + vec4(arr[1].a.xy, arr[1].b[1], arr[0].b[0]) * 0.25;\n"
     "}\n",
     "pos=0,col=1,uv=2"},
    {"swizzle-incdec", MSL_CASE_VS_BASIC,
     "#version 120\nvarying vec4 v_col;\nvarying vec2 v_uv;\n"
     "void main() { vec4 c = v_col; vec2 o = c.xy++; ++c.zw; c.y--; ivec2 i = ivec2(2, 3); ivec2 j = i--;\n"
     "  gl_FragColor = vec4(o, c.zw) * 0.4 + vec4(c.x, c.y, vec2(i + j) * 0.05) * 0.1; }\n",
     "pos=0,col=1,uv=2"},
    // Drawn as point sprites (the vertex shader writes gl_PointSize): gl_PointCoord's origin is
    // the upper left in GL, the top of the target (GL's bottom) in Metal.
    {"point-coord",
     "#version 120\nattribute vec4 pos;\nattribute vec4 col;\nvarying vec4 v_col;\n"
     "void main() { gl_Position = vec4(pos.xy, 0.0, 1.0); gl_PointSize = 7.0; v_col = col; }\n",
     "#version 120\nvarying vec4 v_col;\nvoid main() { gl_FragColor = vec4(gl_PointCoord, v_col.b, 1.0); }\n",
     "pos=0,col=1"},
};
