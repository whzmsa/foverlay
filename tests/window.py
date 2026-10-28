#!/usr/bin/env python3
"""Check the actual X11 window contract while the rendering smoke test runs."""
import ctypes as c
import subprocess
import time

x = c.CDLL('libX11.so.6')
s = c.CDLL('libXext.so.6')
Window = c.c_ulong
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = Window
x.XQueryTree.argtypes = [c.c_void_p, Window, c.POINTER(Window), c.POINTER(Window), c.POINTER(c.POINTER(Window)), c.POINTER(c.c_uint)]
x.XFetchName.argtypes = [c.c_void_p, Window, c.POINTER(c.c_char_p)]
x.XFree.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
s.XShapeGetRectangles.argtypes = [c.c_void_p, Window, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int)]
s.XShapeGetRectangles.restype = c.c_void_p
x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
x.XInternAtom.restype = Window
x.XGetWindowProperty.argtypes = [c.c_void_p, Window, Window, c.c_long, c.c_long, c.c_int, Window, c.POINTER(Window), c.POINTER(c.c_int), c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.c_void_p)]
d = x.XOpenDisplay(None)
assert d, 'X display unavailable'

def descendants(window):
    root, parent, children, count = Window(), Window(), c.POINTER(Window)(), c.c_uint()
    x.XQueryTree(d, window, c.byref(root), c.byref(parent), c.byref(children), c.byref(count))
    result = [children[i] for i in range(count.value)]
    if children: x.XFree(children)
    for child in result:
        yield child
        yield from descendants(child)

def prop(window, name):
    atom = x.XInternAtom(d, name.encode(), 0)
    actual, fmt, count, remaining, data = Window(), c.c_int(), c.c_ulong(), c.c_ulong(), c.c_void_p()
    x.XGetWindowProperty(d, window, atom, 0, 128, 0, 0, c.byref(actual), c.byref(fmt), c.byref(count), c.byref(remaining), c.byref(data))
    result = list(c.cast(data, c.POINTER(c.c_ulong))[:count.value]) if data and fmt.value == 32 else []
    if data: x.XFree(data)
    return result

p = subprocess.Popen(['./build/foverlay', '--smoke-test', 'tests/overlay.html'])
try:
    checked = False
    deadline = time.monotonic() + 10
    while p.poll() is None and time.monotonic() < deadline:
        for window in descendants(x.XDefaultRootWindow(d)):
            name = c.c_char_p()
            x.XFetchName(d, window, c.byref(name))
            title = name.value
            if name: x.XFree(name)
            if title != b'foverlay': continue
            count, order = c.c_int(), c.c_int()
            rects = s.XShapeGetRectangles(d, window, 2, c.byref(count), c.byref(order))
            if rects: x.XFree(rects)
            motif = prop(window, '_MOTIF_WM_HINTS')
            above = x.XInternAtom(d, b'_NET_WM_STATE_ABOVE', 0)
            if count.value == 0 and len(motif) >= 3 and motif[2] == 0 and above in prop(window, '_NET_WM_STATE'):
                print('Empty input region: PASS; no decorations: PASS; keep-above: PASS', flush=True)
                checked = True
                break
        if checked: break
        time.sleep(.05)
    assert checked, 'Overlay window contract was not satisfied'
    assert p.wait(timeout=25) == 0, 'Rendering check failed'
finally:
    if p.poll() is None:
        p.terminate()
        p.wait(timeout=5)
    x.XCloseDisplay(d)
