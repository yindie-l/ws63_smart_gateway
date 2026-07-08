/**
* Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
*
* Description: ADC Sample Source. \n
*
* History: \n
* 2023-07-06, Create file. \n
*/
#include "pinctrl.h"
#include "adc.h"
#include "adc_porting.h"
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "tcxo.h"

#define ADC_TASK_PRIO                   26
#define ADC_TASK_STACK_SIZE             0x1000

#if defined(CONFIG_ADC_SUPPORT_AUTO_SCAN)
static void test_adc_callback(uint8_t ch, uint32_t *buffer, uint32_t length, bool *next)

{
   unused(next);
   for (uint32_t i = 0; i < length; i++) {
       osal_printk("[IRQ]channel: %d, voltage: %dmv\r\n", ch, buffer[i]);
       printf("[IRQ]channel: %d, voltage: %dmv\r\n", ch, buffer[i]);
   }
}
#endif

static void *adc_task(const char *arg)
{
   unused(arg);
   osal_printk("start adc sample\r\n");
#if defined(CONFIG_ADC_SUPPORT_AUTO_SCAN)
   uapi_adc_init(ADC_CLOCK_500KHZ);
   uint8_t adc_channel = 4;
   uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);
   adc_scan_config_t config = {
       .type = 0,
       .freq = 1,
#if defined(CONFIG_ADC_SUPPORT_LONG_SAMPLE)
       .long_sample_time = 0,
#endif
   };
   while(1)
   {
       uapi_adc_auto_scan_ch_enable(adc_channel, config, test_adc_callback);
       uapi_adc_auto_scan_ch_disable(adc_channel);
       osal_mdelay(2000);
   }
#endif
   return NULL;
}
static void adc_entry(void)
{
   osal_task *task_handle = NULL;
   osal_kthread_lock();
   task_handle = osal_kthread_create((osal_kthread_handler)adc_task, 0, "AdcTask", ADC_TASK_STACK_SIZE);
   if (task_handle != NULL) {
       osal_kthread_set_priority(task_handle, ADC_TASK_PRIO);
   }
   osal_kthread_unlock();
}
