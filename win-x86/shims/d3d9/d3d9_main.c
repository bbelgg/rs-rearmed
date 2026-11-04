// Direct3DCreate9 and IDirect3D9: one HAL adapter with shader model 3 caps, the formats the
// layer implements, a fixed list of display modes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_internal.h"

typedef struct {
    d3d9_obj o;
    uint32_t sdk;
} d3d9_d3d;

static const uint8_t IID_IUnknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
// {81BDCBCA-64D4-426d-AE8D-AD0147F4275C}
static const uint8_t IID_IDirect3D9[16] = {0xCA, 0xCB, 0xBD, 0x81, 0xD4, 0x64, 0x6d, 0x42,
                                           0xAE, 0x8D, 0xAD, 0x01, 0x47, 0xF4, 0x27, 0x5C};

// Display modes offered for X8R8G8B8 (and R5G6B5, as D3D9 drivers do), 60 Hz: the common ones
// that fit the desktop, and the desktop's own (win32_user32.c: the host's main screen).
void w32_display_size(int32_t *w, int32_t *h);
static const uint16_t std_modes[][2] = {
    {640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 800}, {1280, 1024},
    {1366, 768}, {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440},
};
static uint32_t modes[sizeof std_modes / sizeof std_modes[0] + 1][2];

static void display_size(uint32_t *w, uint32_t *h) {
    int32_t a, b;
    w32_display_size(&a, &b);
    *w = (uint32_t)a;
    *h = (uint32_t)b;
}

// the mode list, smallest first; returns its length
static int mode_list(void) {
    uint32_t dw, dh;
    display_size(&dw, &dh);
    int n = 0, own = 0;
    for (size_t k = 0; k < sizeof std_modes / sizeof std_modes[0]; k++) {
        uint32_t w = std_modes[k][0], h = std_modes[k][1];
        if (w > dw || h > dh) continue;
        if (w == dw && h == dh) own = 1;
        else if (!own && (w > dw || (w == dw && h > dh))) { modes[n][0] = dw; modes[n][1] = dh; n++; own = 1; }
        modes[n][0] = w; modes[n][1] = h; n++;
    }
    if (!own) { modes[n][0] = dw; modes[n][1] = dh; n++; }
    return n;
}
#define NMODES mode_list()

int d3d9_format_is_block(uint32_t f) {
    return f == FMT_DXT1 || f == FMT_DXT2 || f == FMT_DXT3 || f == FMT_DXT4 || f == FMT_DXT5;
}

int d3d9_format_bpp(uint32_t f) {
    switch (f) {
    case FMT_A8R8G8B8: case FMT_X8R8G8B8: case FMT_A8B8G8R8: case FMT_X8B8G8R8: case FMT_A2R10G10B10:
    case FMT_G16R16: case FMT_D24S8: case FMT_D24X8: case FMT_D32: case FMT_R32F: case FMT_G16R16F:
    case FMT_INDEX32: case FMT_D24FS8: case FMT_INTZ:
        return 32;
    case FMT_R8G8B8: return 24;
    case FMT_R5G6B5: case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: case FMT_X4R4G4B4:
    case FMT_A8L8: case FMT_V8U8: case FMT_D16: case FMT_D16_LOCKABLE: case FMT_INDEX16: case FMT_R16F:
        return 16;
    case FMT_A8: case FMT_L8: return 8;
    case FMT_A16B16G16R16F: case FMT_A16B16G16R16: case FMT_G32R32F: return 64;
    case FMT_A32B32G32R32F: return 128;
    case FMT_DXT1: return 4;
    case FMT_DXT2: case FMT_DXT3: case FMT_DXT4: case FMT_DXT5: return 8;
    }
    return 0;
}

static int is_texture_format(uint32_t f) {
    switch (f) {
    case FMT_A8R8G8B8: case FMT_X8R8G8B8: case FMT_A8B8G8R8: case FMT_X8B8G8R8: case FMT_R5G6B5:
    case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: case FMT_A8: case FMT_L8: case FMT_A8L8:
    case FMT_DXT1: case FMT_DXT2: case FMT_DXT3: case FMT_DXT4: case FMT_DXT5: case FMT_A16B16G16R16F:
    case FMT_R32F:
        return 1;
    }
    return 0;
}
static int is_rt_format(uint32_t f) {
    return f == FMT_A8R8G8B8 || f == FMT_X8R8G8B8 || f == FMT_A8B8G8R8 || f == FMT_X8B8G8R8 ||
           f == FMT_A16B16G16R16F || f == FMT_R32F;
}
static int is_ds_format(uint32_t f) { return f == FMT_D24S8 || f == FMT_D24X8 || f == FMT_D16; }

void d3d9_fill_caps(uint8_t *p) {
    memset(p, 0, 304);
#define W(off, v) do { uint32_t _v = (uint32_t)(v); memcpy(p + (off), &_v, 4); } while (0)
#define F(off, v) do { float _f = (float)(v); memcpy(p + (off), &_f, 4); } while (0)
    W(0, 1);             // D3DDEVTYPE_HAL
    W(8, 0x20000);       // Caps: READ_SCANLINE
    W(12, 0x60020000);   // Caps2: CANAUTOGENMIPMAP | DYNAMICTEXTURES | FULLSCREENGAMMA
    W(16, 0x3A0);        // Caps3: ALPHA_FULLSCREEN_FLIP_OR_DISCARD | LINEAR_TO_SRGB | COPY_TO_*
    W(20, 0x8000000F);   // PresentationIntervals
    W(24, 1);            // CursorCaps: COLOR
    W(28, 0x1BBEF0);     // DevCaps: HW T&L, HW rasterization, pure device, ...
    W(32, 0x2FCEF2);     // PrimitiveMiscCaps (no clip planes, independent write masks)
    W(36, 0x07332191);   // RasterCaps: ZTEST, scissor, anisotropy, depth bias, ...
    W(40, 0xFF);         // ZCmpCaps
    W(44, 0x3FFF);       // SrcBlendCaps
    W(48, 0x3FFF);       // DestBlendCaps
    W(52, 0xFF);         // AlphaCmpCaps
    W(56, 0x84208);      // ShadeCaps
    W(60, 0x14C05);      // TextureCaps: no POW2 restriction, cube maps, mipmaps (no volumes)
    W(64, 0x03030700);   // TextureFilterCaps
    W(68, 0x03030300);   // CubeTextureFilterCaps
    W(76, 0x3F);         // TextureAddressCaps
    W(84, 0x1F);         // LineCaps
    W(88, 8192);         // MaxTextureWidth
    W(92, 8192);         // MaxTextureHeight
    W(96, 2048);         // MaxVolumeExtent
    W(100, 8192);        // MaxTextureRepeat
    W(104, 8192);        // MaxTextureAspectRatio
    W(108, 16);          // MaxAnisotropy
    F(112, 1e10);        // MaxVertexW
    F(116, -32768.0); F(120, -32768.0); F(124, 32768.0); F(128, 32768.0);  // guard band
    W(136, 0x1FF);       // StencilCaps (incl. TWOSIDED)
    W(140, 0x80008);     // FVFCaps: 8 texcoords, DONOTSTRIPELEMENTS
    W(144, 0x3FFFFFF);   // TextureOpCaps
    W(148, 8);           // MaxTextureBlendStages
    W(152, 8);           // MaxSimultaneousTextures
    W(156, 0x17B);       // VertexProcessingCaps
    W(160, 8);           // MaxActiveLights
    W(164, 0);           // MaxUserClipPlanes: not implemented
    W(168, 4);           // MaxVertexBlendMatrices
    F(176, 64.0);        // MaxPointSize
    W(180, 0xFFFFFF);    // MaxPrimitiveCount
    W(184, 0xFFFFFF);    // MaxVertexIndex
    W(188, 16);          // MaxStreams
    W(192, 255);         // MaxStreamStride
    W(196, 0xFFFE0300);  // VertexShaderVersion 3.0
    W(200, 256);         // MaxVertexShaderConst
    W(204, 0xFFFF0300);  // PixelShaderVersion 3.0
    F(208, 3.402823e38); // PixelShader1xMaxValue
    W(212, 0x51);        // DevCaps2: STREAMOFFSET | CAN_STRETCHRECT_FROM_TEXTURES | shared offsets
    W(232, 1);           // NumberOfAdaptersInGroup
    W(236, 0x33F);       // DeclTypes (no UDEC3 / DEC3N)
    W(240, 1);           // NumSimultaneousRTs
    W(244, 0x03000300);  // StretchRectFilterCaps
    W(248, 0); W(252, 24); W(256, 32); W(260, 4);              // VS20Caps (no predication)
    W(264, 0x1B); W(268, 24); W(272, 32); W(276, 4); W(280, 512);  // PS20Caps (no predication)
    W(284, 0);           // VertexTextureFilterCaps: no vertex textures
    W(288, 65535); W(292, 65535); W(296, 32768); W(300, 32768);
#undef W
#undef F
}

// --- IDirect3D9 ------------------------------------------------------------------------------
static d3d9_d3d *d3d_of(uint32_t g) { return (d3d9_d3d *)d3d9_obj_get(g, D3D9K_D3D); }

static uint64_t m_qi(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[2]) return D3DERR_INVALIDCALL;
    const uint8_t *iid = RC32_H(a[1]);
    if (!memcmp(iid, IID_IUnknown, 16) || !memcmp(iid, IID_IDirect3D9, 16)) {
        d3d9_obj_addref(&d3d_of(a[0])->o);
        d3d9_wr32(a[2], a[0]);
        return D3D_OK;
    }
    d3d9_wr32(a[2], 0);
    return E_NOINTERFACE_;
}
static uint64_t m_addref(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_obj *o = d3d9_obj_get(a[0], -1); return o ? d3d9_obj_addref(o) : 0; }
static uint64_t m_release(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_obj *o = d3d9_obj_get(a[0], -1); return o ? d3d9_obj_release(o) : 0; }

static uint64_t m_adapter_count(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 1; }

static void put_str(uint8_t *dst, size_t cap, const char *s) {
    memset(dst, 0, cap);
    strncpy((char *)dst, s, cap - 1);
}

static uint64_t m_adapter_identifier(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (a[1] != 0 || !a[3]) return D3DERR_INVALIDCALL;
    uint8_t *p = RC32_H(a[3]);
    memset(p, 0, 1100);
    // A widely supported D3D9-era identity: games key driver workarounds and minimum-spec
    // checks on the vendor / device ids.
    put_str(p, 512, "nvd3dum.dll");
    put_str(p + 512, 512, "NVIDIA GeForce GTX 660 (RS Re-ARMed Metal)");
    put_str(p + 1024, 32, "\\\\.\\DISPLAY1");
    uint32_t ver_lo = (13u << 16) | 3467, ver_hi = (9u << 16) | 18;  // 9.18.13.3467
    memcpy(p + 1056, &ver_lo, 4);
    memcpy(p + 1060, &ver_hi, 4);
    uint32_t vendor = 0x10DE, device = 0x11C0, subsys = 0, rev = 0xA1;
    memcpy(p + 1064, &vendor, 4);
    memcpy(p + 1068, &device, 4);
    memcpy(p + 1072, &subsys, 4);
    memcpy(p + 1076, &rev, 4);
    static const uint8_t guid[16] = {0x52, 0x53, 0x52, 0x41, 0x52, 0x4d, 0x45, 0x44, 0x6d, 0x74, 0x6c, 0x64, 0x33, 0x64, 0x39, 0x01};
    memcpy(p + 1080, guid, 16);
    uint32_t whql = 1;
    memcpy(p + 1096, &whql, 4);
    return D3D_OK;
}

static int fmt_has_modes(uint32_t f) { return f == FMT_X8R8G8B8 || f == FMT_R5G6B5; }

static uint64_t m_mode_count(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    return a[1] == 0 && fmt_has_modes(a[2]) ? NMODES : 0;
}

static uint64_t m_enum_modes(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (a[1] != 0 || !fmt_has_modes(a[2]) || a[3] >= (uint32_t)NMODES || !a[4]) return D3DERR_INVALIDCALL;
    uint32_t m[4] = {modes[a[3]][0], modes[a[3]][1], 60, a[2]};
    memcpy(RC32_H(a[4]), m, 16);
    return D3D_OK;
}

static uint64_t m_display_mode(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (a[1] != 0 || !a[2]) return D3DERR_INVALIDCALL;
    uint32_t m[4] = {0, 0, 60, FMT_X8R8G8B8};
    display_size(&m[0], &m[1]);
    memcpy(RC32_H(a[2]), m, 16);
    return D3D_OK;
}

static uint64_t m_check_type(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed
    if (a[1] != 0) return D3DERR_INVALIDCALL;
    if (a[3] != FMT_X8R8G8B8) return D3DERR_NOTAVAILABLE;
    uint32_t bb = a[4];
    if (bb == FMT_X8R8G8B8 || bb == FMT_A8R8G8B8 || (bb == FMT_UNKNOWN && a[5])) return D3D_OK;
    return D3DERR_NOTAVAILABLE;
}

static uint64_t m_check_format(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // Adapter, DevType, AdapterFormat, Usage, RType, CheckFormat
    uint32_t usage = a[4], rtype = a[5], f = a[6];
    if (a[1] != 0) return D3DERR_INVALIDCALL;
    if (usage & (USAGE_QUERY_VERTEXTEXTURE | USAGE_QUERY_SRGBREAD | USAGE_QUERY_SRGBWRITE)) return D3DERR_NOTAVAILABLE;
    if (usage & USAGE_DEPTHSTENCIL) return is_ds_format(f) && rtype == RTYPE_SURFACE ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (usage & USAGE_RENDERTARGET) {
        if (!is_rt_format(f)) return D3DERR_NOTAVAILABLE;
    } else if (rtype == RTYPE_VERTEXBUFFER) {
        return D3D_OK;
    } else if (rtype == RTYPE_INDEXBUFFER) {
        return f == FMT_INDEX16 || f == FMT_INDEX32 ? D3D_OK : D3DERR_NOTAVAILABLE;
    } else if (rtype == RTYPE_VOLUMETEXTURE || rtype == RTYPE_VOLUME) {
        return D3DERR_NOTAVAILABLE;
    } else if (!is_texture_format(f)) {
        return D3DERR_NOTAVAILABLE;
    }
    if ((usage & USAGE_AUTOGENMIPMAP) && (d3d9_format_is_block(f) || f == FMT_R32F)) return 0x0876086Fu;  // D3DOK_NOAUTOGEN
    return D3D_OK;
}

static uint64_t m_check_ms(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // Adapter, DevType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels
    if (a[1] != 0) return D3DERR_INVALIDCALL;
    if (a[5] != 0) return D3DERR_NOTAVAILABLE;  // multisampling is not implemented
    if (a[6]) d3d9_wr32(a[6], 1);
    return D3D_OK;
}

static uint64_t m_check_ds(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    return a[1] == 0 && is_ds_format(a[5]) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static uint64_t m_check_conversion(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // Adapter, DevType, SourceFormat, TargetFormat
    return a[1] == 0 && is_rt_format(a[3]) && (a[4] == FMT_X8R8G8B8 || a[4] == FMT_A8R8G8B8) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static uint64_t m_caps(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (a[1] != 0 || !a[3]) return D3DERR_INVALIDCALL;
    d3d9_fill_caps(RC32_H(a[3]));
    return D3D_OK;
}

static uint64_t m_monitor(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; return a[1] == 0 ? 0x00010001u : 0; }

static uint64_t m_create_device(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface
    if (!a[6]) return D3DERR_INVALIDCALL;
    d3d9_wr32(a[6], 0);
    if (a[1] != 0 || !a[5]) return D3DERR_INVALIDCALL;
    uint32_t dev = 0;
    uint32_t hr = d3d9_device_create(a[0], a, a[5], &dev);
    if (hr == D3D_OK) d3d9_wr32(a[6], dev);
    return hr;
}

static const d3d9_slot d3d_slots[] = {
    {"IDirect3D9::QueryInterface", 3, 0, m_qi},
    {"IDirect3D9::AddRef", 1, 0, m_addref},
    {"IDirect3D9::Release", 1, 0, m_release},
    {"IDirect3D9::RegisterSoftwareDevice", 2, 0},
    {"IDirect3D9::GetAdapterCount", 1, 0, m_adapter_count},
    {"IDirect3D9::GetAdapterIdentifier", 4, 0, m_adapter_identifier},
    {"IDirect3D9::GetAdapterModeCount", 3, 0, m_mode_count},
    {"IDirect3D9::EnumAdapterModes", 5, 0, m_enum_modes},
    {"IDirect3D9::GetAdapterDisplayMode", 3, 0, m_display_mode},
    {"IDirect3D9::CheckDeviceType", 6, 0, m_check_type},
    {"IDirect3D9::CheckDeviceFormat", 7, 0, m_check_format},
    {"IDirect3D9::CheckDeviceMultiSampleType", 7, 0, m_check_ms},
    {"IDirect3D9::CheckDepthStencilMatch", 6, 0, m_check_ds},
    {"IDirect3D9::CheckDeviceFormatConversion", 5, 0, m_check_conversion},
    {"IDirect3D9::GetDeviceCaps", 4, 0, m_caps},
    {"IDirect3D9::GetAdapterMonitor", 2, 0, m_monitor},
    {"IDirect3D9::CreateDevice", 7, 0, m_create_device},
};

uint32_t d3d9_create(uint32_t sdk) {
    // D3D_SDK_VERSION is 32 (9.0c); 31 is the older SDK. Anything else: what d3d9.dll does.
    if (sdk != 32 && sdk != 31 && !(sdk & 0x80000000u)) {
        fprintf(stderr, "d3d9: Direct3DCreate9(%u): unexpected SDK version\n", sdk);
    }
    d3d9_d3d *d = calloc(1, sizeof *d);
    d->o.kind = D3D9K_D3D;
    d->sdk = sdk;
    d->o.destroy = (void (*)(d3d9_obj *))free;
    uint32_t vt = d3d9_vtable(d3d_slots, (int)(sizeof d3d_slots / sizeof d3d_slots[0]));
    uint32_t g = vt ? d3d9_obj_publish(&d->o, vt) : 0;
    if (!g) free(d);
    return g;
}
