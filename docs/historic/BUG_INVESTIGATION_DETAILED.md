# Detailed Bug Report & Source Investigation

**Date**: April 8-9, 2026  
**Status**: Runtime Issues Found & Documented  
**Severity**: 3 Critical, Must Fix for Functional GUI

---

## Issue #1: Missing X11 Color Names 🔴 CRITICAL

### Symptom
st startup error: `could not allocate color 'red3'`

### Root Cause  
**File**: [user/x11.c](user/x11.c#L1381-L1410)  
**Function**: `XParseColor()`

Current implementation **only handles**:
- Hex colors: `#RRGGBB` format
- `black` (hardcoded to 0x000000)
- `white` (hardcoded to 0xffffff)

**Returns 0 (failure) for everything else**, which causes st to error out.

### What st Actually Needs
**File**: [ports/st-0.9.3/config.h](ports/st-0.9.3/config.h#L97-L119)

st's colorname array includes:
```c
"red3"      /* Index 1 - Red shade */
"green3"    /* Index 2 - Green shade */
"yellow3"   /* Index 3 - Yellow shade */
"blue2"     /* Index 4 - Blue shade (different level!) */
"magenta3"  /* Index 5 */
"cyan3"     /* Index 6 */
"gray90"    /* Index 7 */
"gray50"    /* Index 9 */
"#5c5cff"   /* Index 14 - Bright blue (hex) */
```

### Why This Fails
1. st calls `XftColorAllocName(dpy, vis, cmap, "red3", result)` 
2. Which internally calls `XParseColor(dpy, cmap, "red3", &xc)`
3. `XParseColor()` sees it doesn't start with `#`, isn't "black" or "white"
4. Returns 0 (failure)
5. `XftColorAllocName()` returns 0
6. st's `xloadcolor()` fails, calls `die()` with error message

### The Fix Needed
Implement X11 color name parsing for common colors:
```c
/* Add to XParseColor() after line 1407 */
else if (!strcmp(spec, "red3")) { /* #cd0000 in X11 */
    exact_def_return->red = 0xcdcd;
    exact_def_return->green = 0x0000;
    exact_def_return->blue = 0x0000;
    exact_def_return->pixel = 0xcd0000;
    exact_def_return->flags = DoRed | DoGreen | DoBlue;
    return 1;
}
/* ... and similar for green3, blue2, yellow3, etc. */
```

### X11 Color Database Reference
Standard X11 named colors needed for st:
- `red3` = RGB(205, 0, 0)     = #cd0000
- `green3` = RGB(0, 205, 0)   = #00cd00
- `yellow3` = RGB(205, 205, 0) = #cdcd00
- `blue2` = RGB(0, 0, 238)    = #0000ee (NOT blue3!)
- `magenta3` = RGB(205, 0, 205) = #cd00cd
- `cyan3` = RGB(0, 205, 205)  = #00cdcd
- `gray90` = RGB(230, 230, 230) = #e6e6e6
- `gray50` = RGB(127, 127, 127) = #7f7f7f

---

## Issue #2: Text Not Rendering (XftDrawStringUtf8 is NO-OP) 🔴 CRITICAL

### Symptom
dwm/st windows appear but text is invisible or completely missing

### Root Cause
**File**: [user/x11.c](user/x11.c#L2268-L2276)  
**Function**: `XftDrawStringUtf8()`

Current implementation:
```c
void XftDrawStringUtf8(XftDraw *draw, XftColor *color, XftFont *font, 
                       int x, int y, const XftChar8 *string, int len) {
  (void)draw;
  (void)color;
  (void)font;
  (void)x;
  (void)y;
  (void)string;
  (void)len;
  /* Stub: text rendering to be implemented when pixmap text support is added */
}
```

**This is a complete NO-OP - it does nothing.**

### Where It's Called
- **st/x.c [line 1384-1442]**: Text rendering in `xdrawglyphfontspecs()`
- **st/x.c [line 1430-1442]**: Actually calls `XftDrawStringUtf8()` to render character glyphs
- **dwm/drw.c**: Similar glyph/text rendering pipeline

### Impact
All text is rendered but **never reaches display** because the drawing command is silently ignored.

### The Fix Needed
Implement actual text rendering by communicating with x6 display server:
```c
void XftDrawStringUtf8(XftDraw *draw, XftColor *color, XftFont *font, 
                       int x, int y, const XftChar8 *string, int len) {
    if (!draw || !color || !font || !string || len <= 0)
        return;
    
    /* Send text rendering command to x6 display server */
    /* Format: DRAW_TEXT x y r g b "..." */
    /* Implementation would send socket command to x6 */
}
```

---

## Issue #3: Font Metrics Hardcoded, Font Name Ignored 🔴 CRITICAL

### Symptom
Either:
1. Font doesn't load and uses hardcoded metrics (12pt ascent, 4pt descent, 16px height)
2. dwm shows wrong font or missing font fallback
3. Text doesn't align properly with expected character dimensions

### Root Cause
**File**: [user/x11.c](user/x11.c#L2207-L2221)  
**Function**: `XftFontOpenName()`

Current implementation:
```c
XftFont *XftFontOpenName(Display *display, int screen, const char *xlfd) {
  XftFont *f;
  (void)screen;
  (void)xlfd;  /* <-- FONT NAME IS IGNORED */
  
  f = (XftFont *)malloc(sizeof(*f));
  if (f) {
    f->pattern = 0;
    f->charset = 0;
    f->ascent = 12;              /* Hardcoded */
    f->descent = 4;              /* Hardcoded */
    f->height = 16;              /* Hardcoded */
    f->max_advance_width = 8;    /* Hardcoded */
  }
  return f;
}
```

### Expected Font
**File**: [ports/st-0.9.3/config.h](ports/st-0.9.3/config.h#L7)
```c
static char *font = "montecarlo-8x16";
```

**File**: [ports/dwm-6.8/config.h](ports/dwm-6.8/config.h) - likely has similar font spec

### Problem Chain
1. st/dwm call `XftFontOpenName(dpy, scr, "montecarlo-8x16")`
2. Our stub **ignores** the font name parameter
3. Returns hardcoded 12/4/16 metrics (ascent/descent/height)
4. st/dwm use these hardcoded metrics for text layout
5. If montecarlo-8x16 has different metrics, text misaligns
6. If font file doesn't exist, no proper fallback mechanism

### The Fix Needed
1. Load actual system font or provide PCF loader
2. Parse font name to detect actual metrics
3. Or at minimum, return correct metrics for montecarlo-8x16:
   - Field Width: 8px
   - Field Height: 16px  
   - Ascent: ~13px
   - Descent: ~3px

### Font Availability Check
```bash
# Check if montecarlo-8x16 is available
fc-list | grep -i montecarlo
# Check default fonts
fc-list | head -20
```

---

## Issue #4: Font Metrics Return Values 🟡 MINOR BUT RELATED

### Location
**File**: [user/x11.c](user/x11.c#L2282-L2295)  
**Function**: `XftTextExtentsUtf8()`

Current:
```c
void XftTextExtentsUtf8(Display *display, XftFont *font, 
                        const XftChar8 *string, int len, XGlyphInfo *extents) {
  (void)display;
  (void)font;
  (void)string;
  
  if (extents) {
    /* Stub: return reasonable defaults (8x16 font metrics) */
    // Likely hardcodes metrics instead of calculating from actual text
  }
}
```

This also returns hardcoded values instead of calculating actual text extents for the string provided.

---

## Summary Table: All Critical Issues

| # | Issue | File | Function | Line | Severity | Impact |
|---|-------|------|----------|------|----------|--------|
| 1 | Missing color names (red3, green3, etc.) | user/x11.c | XParseColor() | 1381 | 🔴 CRITICAL | st crashes on startup |
| 2 | Text rendering is NO-OP stub | user/x11.c | XftDrawStringUtf8() | 2268 | 🔴 CRITICAL | No text visible anywhere |
| 3 | Font name ignored, hardcoded metrics | user/x11.c | XftFontOpenName() | 2207 | 🔴 CRITICAL | Wrong font, misaligned text |
| 4 | Text extent methods hardcoded | user/x11.c | XftTextExtentsUtf8() | 2282 | 🟡 MEDIUM | Text layout broken |

---

## Step-by-Step Fixes (Priority Order)

### 1️⃣ First: Add Color Names (Makes st not crash)
- Add ~15 common X11 color names to XParseColor()
- Hardcode RGB values for red3, green3, yellow3, blue2, magenta3, cyan3, gray50, gray90
- Test: st should start without color allocation error

### 2️⃣ Second: Implement XftDrawStringUtf8 (Makes text visible)
- Receive draw parameters (position, color, text)
- Send socket command to x6 display server
- x6 renders text to framebuffer
- Test: Text should appear in windows

### 3️⃣ Third: Fix Font Loading (Makes text align correctly)
- Either load actual montecarlo-8x16 PCF file
- Or query fontconfig for font metrics
- If unavailable, use correct fallback metrics
- Test: Text should size/align correctly

### 4️⃣ Fourth: Implement XftTextExtentsUtf8 (Fine-tuning)
- Calculate actual text extents based on font and string
- Used by st for text layout precision
- Test: Text layout matches expected positions

---

## Quick Verification Commands

### Check st's color usage
```bash
grep -n "XftColorAllocName\|xloadcolor" /Users/bird/auxv6/ports/st-0.9.3/x.c | head -10
```

### Check if montecarlo font exists
```bash
fc-list | grep -i montecarlo
fclist "montecarlo"
```

### Check x11.c stub status
```bash
grep -A 5 "XftDrawStringUtf8\|XParseColor\|XftFontOpenName" /Users/bird/auxv6/user/x11.c | head -30
```

### Find all color references in st
```bash
grep -n "colorname\[" /Users/bird/auxv6/ports/st-0.9.3/x.c
```

---

## Notes for Implementation

1. **Color Naming Standard**: X11 uses specific RGB values for named colors - don't guess
2. **Text Rendering**: Requires x6 integration - must send socket commands
3. **Font Metrics**: montecarlo-8x16 is 8 pixels wide, 16 pixels tall
4. **FCList Database**: You can query with `fc-list` or integrate fontconfig if needed
5. **Socket Protocol**: x6 needs text rendering command format defined (see x6 source)

These three issues (colors, text rendering, font metrics) form an **integrated failure chain**:
- Even if colors work, text won't appear (Issue #2 NO-OP)
- Even if text appears, it'll be misaligned (Issue #3 wrong metrics)
- st crashes before getting to any text issues (Issue #1 fatal error)

Fix them in order: Colors → Text Rendering → Font Metrics.
