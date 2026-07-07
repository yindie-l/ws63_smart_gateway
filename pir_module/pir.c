#include "pinctrl.h"
#include "gpio.h"
#include "osal_debug.h"
#include "pir.h"

// 请根据实际硬件连接修改引脚号
#define PIR_REL_GPIO_PIN        1
#define PIR_GPIO_MODE           0

uint32_t PIR_Init(void)
{
   uapi_pin_init();
    // 1. 将引脚设置为GPIO功能
    uapi_pin_set_mode(PIR_REL_GPIO_PIN, (pin_mode_t)PIR_GPIO_MODE);

    // 2. 设置GPIO方向为输入
    uapi_gpio_set_dir(PIR_REL_GPIO_PIN, GPIO_DIRECTION_INPUT);

    // 可选：配置内部上拉电阻（根据传感器需求）uapi_pin_set_pull(PIR_REL_GPIO_PIN, PIN_PULL_UP);
    return 0;
}

static uint8_t PIR_ReadLevel(void)
{
    gpio_level_t level = uapi_gpio_get_val(PIR_REL_GPIO_PIN);
    // 高电平表示检测到人体移动
    return (level == GPIO_LEVEL_HIGH) ? 1 : 0;
}

uint32_t PIR_GetStatus(uint8_t *detected)
{
    if (detected == NULL) {
        return 1;   // 参数错误
    }
    *detected = PIR_ReadLevel();
    return 0;
}