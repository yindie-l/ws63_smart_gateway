#ifndef __BEEP_H
#define __BEEP_H

#include "define.h"

/* 移除了内部静态回调函数声明，仅保留外部依赖的公共接口 */
void Buzzer_Hardware_Start(uint32_t freq_hz);
void Buzzer_Hardware_Stop(void);

#endif