#pragma once

#include <Controllino.h>

// GIT_VERSION is written at BUILD time by cmake/git_version.cmake into
// build/generated/version_git.h, so it always matches the flashed firmware
// (including a "-dirty" suffix when built from uncommitted changes).
#if defined(__has_include)
#  if __has_include("version_git.h")
#    include "version_git.h"
#  endif
#endif

#ifdef GIT_VERSION
static const char *gitVersion = GIT_VERSION;
#else
static const char *gitVersion = "No git version specified";
#endif
