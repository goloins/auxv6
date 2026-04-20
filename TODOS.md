Loosely formatted list of goals of various scope and effort levels

- TrueType Font Support
- All driver stubs filled out
- Workspace Manager-like file browser (way out)
- Multiple ttys spawned on separate framebuffers (F1-4)
- More coherent display technology for 6wm (take the place of PS in Rhapsody)
- Desktop Support cooked into 6wm
- Fine tune platinum further
- Build out xHCI/USB support further
- AF_UNIX for our x6
- "Detroit" font clone of old Chicago font
- Loginwindow handler for runlevel 3
- Fork `st` port into 6term, add menu to test menubar.
- Drop dwm, dmenu, lacc ports
- Make bash "semi-first-class," include in rootfs, license in /usr/share/lic/GPL<ver>
- Bring up basic c++ library, add c++ to toolchain
- GTK, qt, FOX, stub header support to prune features and wire main menu code into the OS
    on par with our UI plan docs.
- Improve our window decorations, bring them into more of a os9/rhapsody look
- Port C-only gcc.
- ext2 -> 3 -> 4

- Non-PCI follow-up: adopt bdev_unregister in other block providers/detach paths (loop, nvme, ahci where applicable)
- Non-PCI follow-up: add blockdev lifecycle tests for bdev_unregister (including parent-with-partitions refusal path)
- Non-PCI follow-up: add operator-facing blockdev registration/unregistration observability in proc/debug output

## fvwm2 port X11 gap work (see docs/fvwm2-x11-gap-plan.md)
- Implement Tier 1 core WM / event-loop gaps in user/x11.c
- Implement Tier 2 color management gaps in user/x11.c
- Implement Tier 3 text / font gaps in user/x11.c
- Implement Tier 4 image / bitmap gaps in user/x11.c
- Complete Shape extension (XShapeCombineRegion, XShapeGetRectangles, XShapeInputSelected, XShapeOffsetShape)
- Implement Tier X Xft gaps (37 symbols: XftInit, XftFontSet*, XftPattern{Add,Get,Dup,Find}, XftObjectSet*, XftListFonts*, XftNameParse, XftValue*) then re-enable HAVE_XFT
- Evaluate re-enabling HAVE_XRENDER after Render gaps are closed
