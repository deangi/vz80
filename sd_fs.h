#pragma once
// Freenove-only SD alias. CrowPanel SDSPI is not supported in vZ80.
#include <SD_MMC.h>
#define SD_FS SD_MMC
