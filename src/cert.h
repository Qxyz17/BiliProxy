#pragma once

#include "common.h"

bool genKeyCert();
bool loadCerts();
bool rootCaPresent();
bool installRootCa();
void removeRootCa();
