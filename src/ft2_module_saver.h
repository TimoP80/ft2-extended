#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ft2_unicode.h"

void saveMusic(UNICHAR *filenameU);
bool saveXM(UNICHAR *filenameU);
bool saveXMV(UNICHAR *filenameU); // XM + custom VST3 reference/preset chunk
