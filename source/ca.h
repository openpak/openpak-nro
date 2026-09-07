// Browser trust. The console's web applet keeps its own CA bundle in a LayeredFS romfs, quite
// separate from the ssl sysmodule the system services use — which is why account requests can
// succeed while the link page sits on a loading spinner forever.
#pragma once
#include <stdbool.h>

// Installs the OpenPak CA into the browser's bundle, or takes it back out. Both report the
// number of bundle paths written so the caller can tell the user something true.
int openpak_ca_install(char *err, int errlen);
int openpak_ca_remove(char *err, int errlen);
bool openpak_ca_installed(void);

// The web applet only skips certificate checking when Atmosphère has an NRO patch for the
// browser build it is running. Without one, no CA we install is enough — so the tool reports
// it rather than leaving the console spinning on a blank page.
bool openpak_browser_patch_present(void);
