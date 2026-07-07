#ifndef PIR_H
#define PIR_H

#include <stdint.h>

uint32_t PIR_Init(void);
uint32_t PIR_GetStatus(uint8_t *detected);

#endif