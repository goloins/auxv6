# X11 Code-Based Audit: dwm/st vs auxv6 Implementation

**Methodology**: Direct comparison of actual X11 function calls in dwm.c and st.c against implemented functions in user/x11.c.

---

## I. WHAT dwm & st ACTUALLY DO WITH X11

### dwm (Window Manager) - 37 unique X functions

**Must-Have WM Features**:
- SubstructureRedirectMask on root (trap client map/config/destroy)
- XGrabKey + XUngrabKey (global keyboard shortcut binding)
- XSetInputFocus / XGetInputFocus (focus management)
- XSelectInput on root window (receive events)
- XMaskEvent loops (block on specific events during interactive operations)
- XGrabPointer / XUngrabPointer (window move/resize operations)
- XReparentWindow (reparenting clients into frame window)
- XSendEvent (synthetic ConfigureNotify to clients)
- XGetWMHints / XSetWMHints (read/set urgency flags)
- XGetWMNormalHints (window geometry constraints)
- XGetClassHint (identify window for rules)
- XQueryTree (enumerate existing windows on startup)
- XChangeProperty (EWMH atoms: _NET_ACTIVE_WINDOW, etc.)
- XGetWindowProperty (query EWMH/ICCCM properties)
- XTransientForHint (dialog detection)

**Font Rendering** (text in status bar):
- Via drw (Xft library) - srw_fontset_getwidth(), drw_text()
- Xft font loading, color allocation, glyph rendering
- UTF-8 text support

**Color Management**:
- Via Xft: color allocation by name
- No direct XAllocColor usage (deferred to drw)

### st (Terminal) - 50+ unique X functions

**Core Display**:
- XCreateWindow (terminal window)
- XGetWindowAttributes (query window state)
- XMapWindow (show window)
- XDrawString / XDrawImageString (render text to pixmap)
- XFillRectangle (clear cells/fill background)
- XCreatePixmap / XCopyArea (double-buffering)
- XCreateGC / XSetForeground (graphics context)

**Font Rendering** (critical):
- XftFontOpen / XftFontClose (load fonts by name)
- XftTextExtentsUtf8 (measure text)
- XftDrawGlyphFontSpec (render glyphs)
- Xft color allocation

**Input Methods** (critical for non-ASCII):
- XOpenIM (open input method)
- XCreateIC (input context for window)
- Xutf8TextListToTextProperty / XmbTextPropertyToTextList (UTF-8 conversion)

**Clipboard/Selection**:
- XSetSelectionOwner (PRIMARY, CLIPBOARD)
- XConvertSelection (request paste from owner)
- XGetSelectionOwner (query owner)
- SelectionRequest event handling

**Mouse**:
- XQueryPointer (poll position)
- XGrabButton (drag to select)
- XCreateFontCursor (show cursor)
- XParseColor (cursor colors)
- ButtonPress / MotionNotify events

**Window Management**:
- XSetWMNormalHints / XSetWMHints (set window sizing constraints)
- XInternAtom + XChangeProperty (set window title via _NET_WM_NAME)
- WM_DELETE_WINDOW protocol (graceful shutdown)

**Multi-Monitor** (dwm specific):
- XineramaIsActive / XineramaQueryScreens (detect multi-monitor)

---

## II. WHAT WE IMPLEMENT IN user/x11.c

### ✅ FULLY WORKING (50 functions)

#### Display & Connection (3)
- ✅ XOpenDisplay - TCP connect to x6
- ✅ XCloseDisplay - disconnect
- ✅ XSync - PING/PONG roundtrip

#### Windows (8)
- ✅ XCreateWindow - CREATE command
- ✅ XCreateSimpleWindow - wrapper
- ✅ XDestroyWindow - DESTROY command
- ✅ XMapWindow - MAP command (respects SubstructureRedirect)
- ✅ XUnmapWindow - UNMAP command
- ✅ XMoveResizeWindow - CONFIGURE command
- ✅ XConfigureWindow - parse valuemask + CONFIGURE
- ✅ XSelectInput - SELECT_EVENTS command

#### Events (3)
- ✅ XNextEvent - blocking read from x6
- ✅ XMaskEvent - filtered event read
- ✅ XPending - check if events available

#### Focus & Keyboard Grab (10)
- ✅ XSetInputFocus - SET_FOCUS command
- ✅ XGetInputFocus - GET_FOCUS command
- ✅ XGrabKeyboard - GRAB_KEYBOARD command
- ✅ XUngrabKeyboard - UNGRAB_KEYBOARD command
- ✅ XGrabKey - GRAB_KEY command (per-key grabs)
- ✅ XUngrabKey - UNGRAB_KEY command
- ✅ XGrabPointer - GRAB_POINTER command
- ✅ XUngrabPointer - UNGRAB_POINTER command
- ✅ XWarpPointer - WARP_POINTER command
- ✅ XQueryPointer - QUERY_POINTER command

#### Graphics (5)
- ✅ XCreateGC - allocate GC state (foreground only)
- ✅ XFreeGC - free GC
- ✅ XSetForeground - update GC.fg
- ✅ XFillRectangle - DRAW_RECT command (x6)
- ✅ XDrawString - DRAW_TEXT command (x6)

#### Atoms & Properties (6)
- ✅ XInternAtom - lookup/create in atom table
- ✅ XGetAtomName - reverse lookup
- ✅ XChangeProperty - SET_PROPERTY command
- ✅ XGetWindowProperty - GET_PROPERTY command
- ✅ XDeleteProperty - DELETE_PROPERTY command
- ✅ XGetTextProperty - parse STRING property

#### WM Hints (6)
- ✅ XGetWMHints - parse from property
- ✅ XSetWMHints - serialize to property
- ✅ XGetWMNormalHints - parse geometry constraints
- ✅ XSetWMNormalHints - serialize constraints
- ✅ XGetClassHint - parse WM_CLASS property
- ✅ XSetClassHint - set WM_CLASS property

#### Keyboard (7)
- ✅ XKeycodeToKeysym - keycode → keysym lookup (5 special + ASCII)
- ✅ XKeysymToKeycode - reverse lookup
- ✅ XLookupString - keysym → character conversion
- ✅ XGetKeyboardMapping - return full keysym table
- ✅ XGetModifierMapping - return modifier table
- ✅ XFreeModifiermap - free modifier map
- ✅ XDisplayKeycodes - return min/max keycodes

#### WM Protocols (2)
- ✅ XGetWMProtocols - parse WM_PROTOCOLS
- ✅ XSetTransientForHint - set TRANSIENT_FOR property
- ✅ XGetTransientForHint - read TRANSIENT_FOR property

#### Cursor (2)
- ✅ XDefineCursor - SET_CURSOR command
- ✅ XUndefineCursor - UNSET_CURSOR command

#### Other (3)
- ✅ XFree - free malloc'd memory
- ✅ XSetErrorHandler - store error handler
- ✅ XFreeStringList - free string list

### 🟡 PARTIAL/STUBBED (20 functions)

| Function | Gap | Impact |
|----------|-----|--------|
| XFlush | Always no-op | Implicit in TCP send |
| XMapRaised | Just calls XMapWindow | Z-order unmanaged |
| XCheckMaskEvent | Only checks pending queue | Won't wait for network |
| XChangeWindowAttributes | Only CWEventMask, CWOverride, CWCursor | Missing background, etc. |
| XDrawRectangle | Emulates with 4 XFillRectangles | Wrong appearance (outline only) |
| XSetLineAttributes | Complete no-op | Line width/style ignored |
| XCopyArea | Complete no-op | Double-buffering broken |
| XCreatePixmap / XFreePixmap | Return stub values | Pixmaps never work |
| XCreateFontCursor | Returns shape+1 | No actual cursors |
| XFreeCursor | No-op | Leak cursors (not critical) |
| XQueryTree | Returns dummy (root=1, no children) | Not critical for dwm |
| XGetWindowAttributes | Works but minimal fields | Most fields correct |
| XRaiseWindow / XLowerWindow | No-op | Z-order not managed |
| XMoveWindow | Queries attrs then calls XMoveResizeWindow | Correct but roundabout |
| XSupportsLocale | Hardcoded return 1 | Wrong: we only support C/ASCII |
| XRefreshKeyboardMapping | No-op | Would need XEVENT parsing |
| XGrabButton / XUngrabButton | No-op | Buttons unconstrained |
| XAllowEvents | No-op | Async pointer ops not needed |
| XSetCloseDownMode | No-op | Not relevant to TCP protocol |
| XmbTextPropertyToTextList | Only single-value case | Missing multi-value parsing |

---

## III. 🔴 CRITICAL MISSING FEATURES

### A. FONTS & TEXT RENDERING (BLOCKS st completely)

**Missing from x11.c**:
- ❌ XLoadQueryFont()
- ❌ XFreeFont()
- ❌ XTextWidth() - **st needs to measure text**
- ❌ XTextExtents()
- ❌ XSetFont() - apply font to GC
- ❌ XDrawImageString() - draw with background
- ❌ XListFonts()
- ❌ XFontStruct operations

**Missing from x6.c**:
- ❌ No font loading protocol commands
- ❌ No metrics query (text width, height, baseline)
- ❌ DRAW_TEXT command is primitive (no font control)

**Current situation**:
- XDrawString is hardcoded to Montecarlo 8×16
- No way to specify different font
- st can't query text width (breaks layout)
- st defaults to Xft which we don't support

**st Impact**: Terminal won't render text properly without font support.

---

### B. Xft (ANTI-ALIASED FONTS) - ENTIRE LIBRARY MISSING

**Missing from x11.c**:
- ❌ XftFontOpen / XftFontOpenName()
- ❌ XftFontClose
- ❌ XftDraw* functions (XftDrawCreate, XftDrawChange, etc.)
- ❌ XftDrawString8 / XftDrawStringUtf8()
- ❌ XftTextExtents* (measure UTF-8 text)
- ❌ XftColorAlloc* (allocate color by name)
- ❌ XftColor* struct handling
- ❌ XftCharIndex
- ❌ XftDefaultSubstitute / pattern handling
- ❌ FontConfig integration

**dwm Impact**: dwm drw library depends on Xft for smooth fonts (used via Xft in config).

**st Impact**: st DEFAULTS to Xft rendering. Without it, falls back to core X fonts (which we also don't support).

---

### C. COLOR ALLOCATION (BLOCKS graphical apps)

**Missing from x11.c**:
- ❌ XAllocColor() - **allocate color from name like "red"**
- ❌ XParseColor() - parse color strings
- ❌ XAllocColorCells()
- ❌ XFreeColorCells()
- ❌ XAllocNamedColor()
- ❌ XQueryColor()
- ❌ XStoreColor()
- ❌ XCreateColormap()
- ❌ Colormap operations

**Missing from x6.c**:
- No color allocation server-side
- Colors are raw RGB pixels only

**Current situation**:
- dwm/st hardcode colors as RGB hex
- Any app trying to use named colors (e.g., "red") fails silently
- st x-auxv6.c has hardcoded palette (170 colors) but can't allocate new ones

**dwm Impact**: dwm bar colors work if app doesn't use XAllocColor (uses hex directly).

**st Impact**: st uses hardcoded palette; limited to 256 colors max.

---

### D. SELECTION & CLIPBOARD (BLOCKS copy/paste)

**Missing from x11.c**:
- ❌ XSetSelectionOwner() - claim PRIMARY/CLIPBOARD ownership
- ❌ XGetSelectionOwner() - query who owns selection
- ❌ XConvertSelection() - request conversion from owner
- ❌ SelectionRequest event (no handler)
- ❌ SelectionNotify event (never generated)

**Missing from x6.c**:
- No selection protocol at all
- No per-window selection registry
- No inter-process selection mechanism

**Current situation**:
- st has local clipboard (aux_clipboard in x-auxv6.c)
- Can't copy from st to external tool
- Can't paste from external tool to st
- dwm menu (dmenu) can't access clipboard

**Impact**: Severely limits usability. Copy/paste is broken for anything beyond in-process.

---

### E. INPUT METHODS (BLOCKS non-ASCII input)

**Missing from x11.c**:
- ❌ XOpenIM() - open input method
- ❌ XCreateIC() - create input context
- ❌ XDestroyIC()
- ❌ XFilterEvent() - pre-filter events through IM
- ❌ XmbLookupString() - multibyte key lookup
- ❌ Xutf8LookupString() - UTF-8 lookup
- ❌ XRegisterIMInstantiateCallback()
- ❌ XIM spot location, preedit/status

**Missing from x6.c**:
- No IM protocol
- Events don't carry compose state

**Current situation**:
- XLookupString only handles ASCII (32-127) + 5 special keys
- Japanese, Chinese, accents, Latin-1 extended characters can't be typed
- st can't receive UTF-8 input (falls back to ASCII)

**st Impact**: Terminal can only accept ASCII input. Non-ASCII languages blocked.

---

### F. IMAGES & PIXMAP OPERATIONS (BLOCKS rendering optimization)

**Missing from x11.c**:
- ❌ XImage struct/functions (XCreateImage, XGetImage, XPutImage, XDestroyImage)
- ❌ XGetImage() - read pixel data from window/pixmap
- ❌ XPutImage() - write pixel data to drawable

**Missing from x6.c**:
- No pixmap data storage
- DRAW_* commands only for primitives

**Current situation**:
- XCopyArea is a no-op (st attempts double-buffering with pixmaps)
- st can't copy pixmap to window (no redraw)
- st visible content never reaches screen

**st Impact**: CRITICAL. st allocates pixmap buffer but can't render it. Text invisible.

---

### G. REGIONS & CLIPPING (DEGRADES drawing quality)

**Missing from x11.c**:
- ❌ XCreateRegion()
- ❌ XSetClipRectangles() - constrain drawing to region
- ❌ XPolygonRegion()
- ❌ XClipBox()
- ❌ Region operations (AND, OR, XOR, copy, etc.)

**Missing from x6.c**:
- No clipping protocol

**Current situation**:
- Drawing can overflow window bounds
- No support for arbitrary clip regions

**Impact**: Low for MVP (dwm/st don't use clipping heavily). Could cause visual artifacts.

---

### H. MISSING EVENT TYPES (6 types)

| Event | Use Case | Missing |
|-------|----------|---------|
| EnterNotify / LeaveNotify | Mouse enter/leave window | ❌ |
| PropertyNotify | Property changed (EWMH) | ❌ |
| SelectionRequest / SelectionNotify | Clipboard | ❌ |
| VisibilityNotify | Window obscured | ❌ |
| CirculateNotify | Z-order change | ❌ |
| MapNotify / UnmapNotify | Map state notifications | ❌ |
| ReparentNotify | Reparenting notification | ❌ |
| GraphicsExpose / NoExpose | Copy failures | ❌ |

---

### I. INCOMPLETE GC (Graphics Context)

**Current state** (in x11.c):
- ✅ Foreground color
- ❌ Background color (stored but not used)
- ❌ Fill style (solid, stipple, tiled)
- ❌ Line width / line style / cap / join
- ❌ Function (GXcopy, GXxor, GXor, GXand, etc.)
- ❌ Dashing (line patterns)
- ❌ Tile / Stipple pixmaps
- ❌ Clip mask / clip rectangles

**Impact**: Drawing only in solid foreground. No patterns, stipples, XOR, etc.

---

## IV. IMPACT SUMMARY BY APPLICATION

### **dwm (Window Manager)**

| Feature | Status | Impact |
|---------|--------|--------|
| Window mapping/config | ✅ | WM core works |
| SubstructureRedirect | ✅ | Can manage windows |
| Keyboard shortcuts | ✅ | Mod+Key works |
| Focus management | ✅ | Focus changes work |
| Pointer grab (move/resize) | ✅ | Should work |
| Text rendering (bar) | 🟠 Partial | Hardcoded font only; assumes 8px width |
| Color scheme | 🟠 Partial | RGB hex works; named colors don't |
| XAllocColor | ❌ | Stub; bar colors fail |
| **Runnable?** | **✅** | **Yes, but limited UI** |

---

### **st (Simple Terminal)**

| Feature | Status | Impact |
|---------|--------|--------|
| Window creation | ✅ | Window appears |
| Events (keyboard, expose) | ✅ | Input/redraw work |
| Font rendering | 🔴 Critical | Only Montecarlo hardcoded; no XLoadFont |
| Text measurements | ❌ | No XTextWidth (grid layout broken) |
| Pixmap/XCopyArea | 🔴 Critical | Double-buffering no-op; content invisible |
| XDrawString | 🟠 Partial | Works but hardcoded font |
| XFillRectangle | ✅ | Cell fill works |
| Color allocation | ❌ | Hardcoded palette (170 colors) |
| Clipboard (copy/paste) | ❌ | Local only; no inter-process |
| Input methods (Xft) | 🔴 Critical | st defaults to Xft (missing) |
| Non-ASCII input | ❌ | ASCII only; no IM |
| **Runnable?** | **🔴 No** | **Text not visible without XCopyArea** |

---

## V. KNOWN IMPLEMENTATION PATTERNS vs GAPS

### What x6.c *Can* Do:
- Window creation and state tracking ✅
- Event queueing and routing ✅
- Property storage ✅
- Framebuffer direct rendering (ANSI/FB backend) ✅
- Keyboard input routing ✅

### What x6.c *Cannot* Do:
- Font metrics queries ❌
- Color allocation ❌
- Pixmap/image rendering (XCopyArea) ❌
- Selection management ❌
- Input method composition ❌

---

## VI. REQUIRED TO GET dwm + st FUNCTIONAL

| Priority | Feature | Effort | Blocks |
|----------|---------|--------|--------|
| 🔴 P0 | XCopyArea protocol | Medium | st text visibility |
| 🔴 P0 | XLoadFont + XTextWidth | Medium | dwm bar layout, st glyph sizing |
| 🔴 P0 | Xft basic support or fallback | High | st rendering (uses Xft by default) |
| 🟠 P1 | Color allocation (name → RGB) | Low | dwm/st color schemes |
| 🟠 P1 | Selection/Clipboard (PRIMARY) | Medium | Copy/paste (UX blocker) |
| 🟡 P2 | Input Methods (XIM/UTF-8) | High | Non-ASCII input |
| 🟡 P2 | XImage operations | Medium | Complex rendering |
| 🟢 P3 | Regions/clipping | Low | Edge cases |

---

## BLOCKERS PREVENTING "WORKS"

1. **st text invisible** - XCopyArea no-op means pixmap never reaches screen
2. **st layout wrong** - No XTextWidth means grid calculation impossible
3. **st rendering** - Xft missing (st default); core X fonts also missing
4. **dwm bar text** - Assumes 8px width (no XTextWidth); no color allocation
5. **Copy/paste** - Selection not implemented at all
6. **Non-ASCII** - Input methods not supported

---

## CONCLUSION

**Current x11.c Coverage**: ~45% of core X11 used by dwm/st.

**Functional Status**:
- ✅ dwm *should* run (but UI limited by fonts/colors)
- 🔴 st *cannot* display text (XCopyArea critical missing piece)
- 🔴 Both: no clipboard, no color allocation, no IM

**Recommended Fixes (in order)**:
1. Implement XCopyArea (unblock st rendering)
2. Add font protocol + XLoadFont / XTextWidth (unblock text layout)
3. Add color allocation (unblock color schemes)
4. Add selection/clipboard (unblock copy/paste)
5. Add IM support (unblock non-ASCII input)
