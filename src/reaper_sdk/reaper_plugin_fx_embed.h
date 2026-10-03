/*
 * Adapted subset of the REAPER plug-in SDK embedded UI declarations.
 * Copyright Cockos Incorporated. Distributed under the adjacent LICENSE.
 */
#pragma once

#define REAPER_FXEMBED_WM_IS_SUPPORTED 0x0000
#define REAPER_FXEMBED_WM_CREATE 0x0001
#define REAPER_FXEMBED_WM_DESTROY 0x0002

struct REAPER_FXEMBED_DrawInfo
{
    int context;
    int dpi;
    int mousewheel_amt;
    double reserved2;
    int width;
    int height;
    int mouse_x;
    int mouse_y;
    int flags;
    int reserved3;
    void* spare[6];
};

#define REAPER_FXEMBED_DRAWINFO_FLAG_PAINT_OPTIONAL 1
#define REAPER_FXEMBED_DRAWINFO_FLAG_IS_RETINA 0x00100
// REAPER 7.36+/macOS: width, height, and mouse coordinates are doubled.
#define REAPER_FXEMBED_DRAWINFO_FLAG_LBUTTON_CAPTURED 0x10000
#define REAPER_FXEMBED_DRAWINFO_FLAG_RBUTTON_CAPTURED 0x20000

#define REAPER_FXEMBED_WM_PAINT 0x000F
#define REAPER_FXEMBED_WM_SETCURSOR 0x0020
#define REAPER_FXEMBED_WM_GETMINMAXINFO 0x0024

struct REAPER_FXEMBED_SizeHints
{
    int preferred_aspect;
    int minimum_aspect;
    int flags;
    int dpi;
    int reserved3;
    int reserved4;
    int min_width;
    int min_height;
    int max_width;
    int max_height;
};

#define REAPER_FXEMBED_WM_MOUSEMOVE 0x0200
#define REAPER_FXEMBED_WM_LBUTTONDOWN 0x0201
#define REAPER_FXEMBED_WM_LBUTTONUP 0x0202
#define REAPER_FXEMBED_WM_LBUTTONDBLCLK 0x0203
#define REAPER_FXEMBED_WM_RBUTTONDOWN 0x0204
#define REAPER_FXEMBED_WM_RBUTTONUP 0x0205
#define REAPER_FXEMBED_WM_RBUTTONDBLCLK 0x0206
#define REAPER_FXEMBED_WM_MOUSEWHEEL 0x020A
#define REAPER_FXEMBED_WM_NOBORDER 0x100001
#define REAPER_FXEMBED_WM_HITTEST 0x100002

#define REAPER_FXEMBED_RETNOTIFY_HANDLED 0x0000001
#define REAPER_FXEMBED_RETNOTIFY_INVALIDATE 0x1000000

class REAPER_FXEMBED_IBitmap
{
public:
    virtual ~REAPER_FXEMBED_IBitmap() = default;
    virtual unsigned int* getBits() = 0;
    virtual int getWidth() = 0;
    virtual int getHeight() = 0;
    virtual int getRowSpan() = 0;
    virtual bool isFlipped() { return false; }
    virtual bool resize(int width, int height) = 0;
    virtual void* getDC() { return nullptr; }
    virtual INT_PTR Extended(int, void*) { return 0; }
};

#define REAPER_FXEMBED_EXT_GET_ADVISORY_SCALING 0x2003
