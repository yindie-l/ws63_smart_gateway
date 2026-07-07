#ifndef BH1750_H
#define BH1750_H

#include <stdint.h>

uint32_t BH1750_Init(void);
uint32_t BH1750_ReadLux(float *lux);

#endif