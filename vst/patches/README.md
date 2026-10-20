# Patches against the vendored SDK

`vst/sdk` is not in this repository - it is downloaded - so anything that has to
change inside it lives here instead, and has to be reapplied after the SDK is
fetched or updated.

    cd vst/sdk/vstgui4 && patch -p1 < ../../patches/vstgui-keep-x-connection.patch

## vstgui-keep-x-connection.patch

VSTGUI's X11 backend disconnected from the X server whenever the last editor
closed, and connected again on the next open. Cairo caches a device per xcb
connection, so the second open handed cairo a screen belonging to a connection
that no longer existed, and cairo does not fail politely:

    cairo-xcb-screen.c:219: _get_screen_index: Assertion `!"reached"' failed

In a plug-in host that is the whole process going down, taking every other
plug-in in it along. It is the "the window disappears and I have to reload it"
that these plug-ins had for as long as they have run on Linux.

The patch makes the connection once and keeps it for the life of the process.
One file descriptor stays open; reconnecting cost the process.
