#pragma once

#include <Controllino.h>

#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)

// the next few macros will put the current git "stamp" into the code
#ifdef GITVERSION
static const char *gitVersion = STRINGIZE(GITVERSION);
#else
static const char *gitVersion = "No git version specified";
#endif
