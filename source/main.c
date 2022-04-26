/******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the "AWS IoT: Over-the-air firmware
 * update using MQTT" code example. The code example demonstrates
 * AWS OTA update feature using MQTT protocol.
 *
 * Related Document: See README.md
 *
 *******************************************************************************
 * Copyright 2022, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_log.h"
#include <FreeRTOS.h>
#include <task.h>
#include "cy_ota_storage.h"

#include "aws_ota_demo_mqtt.h"

#ifdef CY_TFM_PSA_SUPPORTED
#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"
#endif

/*******************************************************************************
 * Macros
 ********************************************************************************/
/* OTA task configurations */
#define OTA_MQTT_APP_TASK_SIZE              (1024 * 10)
#define OTA_MQTT_APP_TASK_PRIORITY          (configMAX_PRIORITIES - 2)

#ifdef CY_TFM_PSA_SUPPORTED
struct ns_mailbox_queue_t ns_mailbox_queue;
#endif

#ifdef CY_TFM_PSA_SUPPORTED
static void tfm_ns_multi_core_boot(void)
{
    int32_t ret;

    if (tfm_ns_wait_for_s_cpu_ready())
    {
        /* Error sync'ing with secure core */
        /* Avoid undefined behavior after multi-core sync-up failed. */
        for (;;)
        {
        }
    }

    ret = tfm_ns_mailbox_init(&ns_mailbox_queue);
    if (ret != MAILBOX_SUCCESS)
    {
        /* Non-secure mailbox initialization failed. */
        /* Avoid undefined behavior after NS mailbox initialization failed. */
        for (;;)
        {
        }
    }
}
#endif

int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cyhal_wdt_t wdt_obj;

    /* Unlock the WDT */
    if(Cy_WDT_Locked())
    {
        Cy_WDT_Unlock();
    }

    /* Initialize the board support package */
    result = cybsp_init();
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    result = cy_log_init( CY_LOG_INFO, NULL, NULL );
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize retarget-io to use the debug UART port. */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
             CY_RETARGET_IO_BAUDRATE);
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("======================================================\n");
    printf("Welcome to the AWS IoT OTA demo\n");
    printf("======================================================\n");

#if (APP_VERSION_BUILD != 0) || (APP_VERSION_MINOR != 0) || (APP_VERSION_MAJOR != 1)
    printf("\n===========================================================\n");
    printf("\n Updated Image v%d.%d.%d\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);
    printf("\n===========================================================\n");
#else
    printf("\n===========================================================\n");
    printf("\n Stock Image v1.0.0 \n");
    printf("\n===========================================================\n");
#endif

#ifdef TEST_REVERT
    printf("===============================================================\n");
    printf("Testing revert feature, entering infinite loop !!!\n\n");
    printf("===============================================================\n\n");
    while(true);
#endif

    /* Clear watchdog timer so that it doesn't trigger a reset */
    cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    cyhal_wdt_free(&wdt_obj);

    printf("\nWatchdog timer started by the bootloader is now turned off!!!\n\n");

    cy_awsport_ota_flash_image_validate();

#ifdef CY_TFM_PSA_SUPPORTED
    tfm_ns_multi_core_boot();

    /* Initialize the TFM interface */
    tfm_ns_interface_init();
#endif

    cy_log_init(CY_LOG_INFO, NULL, NULL);

    xTaskCreate(ota_mqtt_app_task, "OTA MQTT APP TASK", OTA_MQTT_APP_TASK_SIZE,
            NULL, OTA_MQTT_APP_TASK_PRIORITY, NULL);

    vTaskStartScheduler();

    return 0;
}

/* [] END OF FILE */
