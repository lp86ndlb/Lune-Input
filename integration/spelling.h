#pragma once

#include <rime_api.h>

// Called after each Rime initialization, including redeployment.
void registerLuneSpelling();
bool applyLuneSpelling(RimeApi *api, RimeSessionId session, const char *commit);
