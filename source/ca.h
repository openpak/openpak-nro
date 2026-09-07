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
