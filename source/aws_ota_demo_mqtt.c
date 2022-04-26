/******************************************************************************
 * File Name:   aws_ota_demo_mqtt.c
 *
 * Description: This file contains tasks and functions related to AWS OTA update
 * feature.
 *
 ********************************************************************************
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
#include "cy_log.h"
#include "cy_lwip.h"
#include "cy_nw_helper.h"
#include "cyabs_rtos.h"
#include <lwip/tcpip.h>
#include <lwip/api.h>

#include <FreeRTOS.h>
#include <task.h>

/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"

/* MQTT include. */
#include "cy_mqtt_api.h"
#include "mqtt_subscription_manager.h"

/* OTA Library include. */
#include "ota.h"
#include "ota_config.h"

/* OTA Library Interface include. */
#include "cy_ota_os_timer.h"
#include "cy_ota_storage.h"

/* Include firmware version struct definition. */
#include "ota_appversion32.h"

/* Include AWS port layer library header. */
#include "cy_tcpip_port_secure_sockets.h"

#include "credentials_config.h"

/*******************************************************************************
 * Macros
 ********************************************************************************/

/*
 * ALPN (Application-Layer Protocol Negotiation) protocol name for AWS IoT MQTT.
 *
 * This will be used if the AWS_MQTT_PORT is configured as 443 for
 * AWS IoT MQTT broker.
 *
 * Please see more details about the ALPN protocol for AWS IoT MQTT endpoint
 * in the link below.
 * https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/
 */
#define AWS_IOT_MQTT_ALPN                       "\x0ex-amzn-mqtt-ca"

/* Length of ALPN protocol name. */
#define AWS_IOT_MQTT_ALPN_LENGTH                (( uint16_t ) ( sizeof( AWS_IOT_MQTT_ALPN )))

/* Length of MQTT server host name. */
#define AWS_IOT_ENDPOINT_LENGTH                 (( uint16_t ) ( sizeof( AWS_IOT_ENDPOINT )))

/* Length of client identifier. */
#define CLIENT_IDENTIFIER_LENGTH                (( uint16_t ) ( sizeof( CLIENT_IDENTIFIER ) - 1))

/* The maximum time interval in seconds which is allowed to elapse
 * between two Control Packets.
 *
 * It is the responsibility of the Client to ensure that the interval between
 * Control Packets being sent does not exceed the this Keep Alive value. In the
 * absence of sending any other Control Packets, the Client MUST send a
 * PINGREQ Packet.
 */
#define OTA_MQTT_KEEP_ALIVE_INTERVAL_SECONDS    (0U)

/* @brief Timeout for MQTT_ProcessLoop function in milliseconds. */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS            (100U)

/* Maximum number or retries to publish a message in case of failures. */
#define MQTT_PUBLISH_RETRY_MAX_ATTEMPS          (3U)

/* Size of the network buffer to receive the MQTT message.
 *
 * The largest message size is data size from the AWS IoT streaming service,
 * otaconfigFILE_BLOCK_SIZE + extra for headers.
 */

#define OTA_NETWORK_BUFFER_SIZE                  (otaconfigFILE_BLOCK_SIZE + 128)

/* The delay used in the main OTA Demo task loop to periodically output the OTA
 * statistics like number of packets received, dropped, processed and queued
 * per connection.
 */
#define OTA_EXAMPLE_TASK_DELAY_MS                (1000U)

/* The timeout for waiting for the agent to get suspended after closing the
 * connection.
 */
#define OTA_SUSPEND_TIMEOUT_MS                   (5000U)

/* The timeout for waiting before exiting the OTA demo. */
#define OTA_DEMO_EXIT_TIMEOUT_MS                 (10000U)

/* The maximum size of the file paths used in the demo. */
#define OTA_MAX_FILE_PATH_SIZE                   (260U)

/* The maximum size of the stream name required for downloading update file
 * from streaming service.
 */
#define OTA_MAX_STREAM_NAME_SIZE                 (128U)

/* The common prefix for all OTA topics. */
#define OTA_TOPIC_PREFIX                        "$aws/things/+/"

/* The string used for jobs topics. */
#define OTA_TOPIC_JOBS                          "jobs"

/* The string used for streaming service topics. */
#define OTA_TOPIC_STREAM                        "streams"

#define OTA_THREAD_SIZE                         (1024 * 4)

#define OTA_THREAD_PRIORITY                     (configMAX_PRIORITIES - 4)

/***********************************************************
 * Global Variables
 ************************************************************/
/* Struct for firmware version. */
const AppVersion32_t appFirmwareVersion =
{
        .u.x.major = APP_VERSION_MAJOR,
        .u.x.minor = APP_VERSION_MINOR,
        .u.x.build = APP_VERSION_BUILD,
};

/* Keep a flag for indicating if the MQTT connection is alive. */
bool mqttSessionEstablished = false;

/* Semaphore for synchronizing buffer operations. */
SemaphoreHandle_t bufferSemaphore;
SemaphoreHandle_t mqtt_discon_Semaphore;

/* Enum for type of OTA job messages received. */
typedef enum jobMessageType
{
    jobMessageTypeNextGetAccepted = 0,
    jobMessageTypeNextNotify,
    jobMessageTypeMax
} jobMessageType_t;

/* The network buffer must remain valid when OTA library task is running. */
uint8_t otaNetworkBuffer[ OTA_NETWORK_BUFFER_SIZE ];

/* Update File path buffer. */
uint8_t updateFilePath[ OTA_MAX_FILE_PATH_SIZE ];

/* Certificate File path buffer. */
uint8_t certFilePath[ OTA_MAX_FILE_PATH_SIZE ];

/* Stream name buffer. */
uint8_t streamName[ OTA_MAX_STREAM_NAME_SIZE ];

/* Decode memory. */
uint8_t decodeMem[ otaconfigFILE_BLOCK_SIZE ];

/* Bitmap memory. */
uint8_t bitmap[ OTA_MAX_BLOCK_BITMAP_SIZE ];

/* Event buffer. */
OtaEventData_t eventBuffer[ otaconfigMAX_NUM_OTA_DATA_BUFFERS ];

/* The buffer passed to the OTA Agent from application while initializing. */
OtaAppBuffer_t otaBuffer =
{
        .pUpdateFilePath    = updateFilePath,
        .updateFilePathsize = OTA_MAX_FILE_PATH_SIZE,
        .pCertFilePath      = certFilePath,
        .certFilePathSize   = OTA_MAX_FILE_PATH_SIZE,
        .pStreamName        = streamName,
        .streamNameSize     = OTA_MAX_STREAM_NAME_SIZE,
        .pDecodeMemory      = decodeMem,
        .decodeMemorySize   = otaconfigFILE_BLOCK_SIZE,
        .pFileBitmap        = bitmap,
        .fileBitmapSize     = OTA_MAX_BLOCK_BITMAP_SIZE
};

cy_mqtt_t               mqtthandle;

/*******************************************************************************
 * Forward declaration
 ********************************************************************************/
cy_rslt_t connect_to_wifi_ap(void);
cy_rslt_t startOTADemo(void);
void otaAppCallback(OtaJobEvent_t event, const void * pData );
void setOtaInterfaces(OtaInterfaces_t * pOtaInterfaces );
OtaMqttStatus_t mqttSubscribe(const char * pTopicFilter,
        uint16_t topicFilterLength,
        uint8_t qos);
OtaMqttStatus_t mqttPublish(const char * const pacTopic,
        uint16_t topicLen,
        const char * pMsg,
        uint32_t msgSize,
        uint8_t qos);
OtaMqttStatus_t mqttUnsubscribe(const char * pTopicFilter,
        uint16_t topicFilterLength,
        uint8_t qos);
void otaEventBufferFree(OtaEventData_t * const pxBuffer);
void registerSubscriptionManagerCallback(const char * pTopicFilter,
        uint16_t topicFilterLength);
void mqttJobCallback(cy_mqtt_t handle, cy_mqtt_received_msg_info_t *pPublishInfo);
void mqttDataCallback(cy_mqtt_t handle, cy_mqtt_received_msg_info_t *pPublishInfo);
SubscriptionManagerCallback_t otaMessageCallback[] = {mqttJobCallback, mqttDataCallback};
jobMessageType_t getJobMessageType(const char * pTopicName,
        uint16_t topicNameLength);
OtaEventData_t * otaEventBufferGet(void);
void otaThread(void * pParam);
void create_mqtt_handle(void);
cy_rslt_t establishConnection(void);
void disconnect(void);
void mqtt_event_cb(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *user_data);


/*******************************************************************************
 * Function Name: ota_mqtt_app_task
 *******************************************************************************
 * Summary:
 *  Task to initialize required libraries and start OTA agent.
 *
 * Parameters:
 *  void *args: Task parameter defined during task creation (unused)
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void ota_mqtt_app_task( void *arg )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Semaphore initialization flag. */
    bool bufferSemInitialized = false;
    bool mqttDisconSemInitialized = false;

    /* Maximum time in milliseconds to wait before exiting demo . */
    int16_t waitTimeoutMs = OTA_DEMO_EXIT_TIMEOUT_MS;

    result = cy_awsport_ota_flash_init();
    if(result == CY_RSLT_SUCCESS)
    {
        printf("cy_awsport_ota_pal_flash_init completed. \n");
    }
    else
    {
        printf("cy_awsport_ota_pal_flash_init failed. \n");
        goto app_exit;
    }

    /* Connect to Wi-Fi AP */
    result = connect_to_wifi_ap();
    if( result != CY_RSLT_SUCCESS)
    {
        printf("\n Failed to connect to Wi-FI AP. \n");
        CY_ASSERT(0);
    }

    /* Initialize semaphore for buffer operations. */
    bufferSemaphore = xSemaphoreCreateCounting(1, 1);
    if(bufferSemaphore == NULL)
    {
        printf("Failed to initialize buffer semaphore. \n");
        result = !CY_RSLT_SUCCESS;
        goto app_exit;
    }
    else
    {
        printf("Initialized buffer semaphore. \n");
        bufferSemInitialized = true;
    }

    /* Initialize semaphore for buffer operations. */
    mqtt_discon_Semaphore = xSemaphoreCreateCounting(1, 0);
    if(mqtt_discon_Semaphore == NULL)
    {
        printf("Failed to initialize mqtt disconnect notification semaphore. \n");
        result = !CY_RSLT_SUCCESS;
        goto app_exit;
    }
    else
    {
        printf("Initialized mqtt disconnect notification semaphore. \n");
        mqttDisconSemInitialized = true;
    }

    if(result == CY_RSLT_SUCCESS)
    {
        /* Initialize MQTT library. Initialization of the MQTT library needs to be
         * done only once in this demo. */
        result = cy_mqtt_init();
        if(result == CY_RSLT_SUCCESS)
        {
            printf("Initialize MQTT library completed.. \n");
        }
        else
        {
            printf("Initialize MQTT library failed.. \n");
            goto app_exit;
        }
    }

    if(result == CY_RSLT_SUCCESS)
    {
        /* Start OTA demo. */
        result = startOTADemo();
    }

    app_exit :

    if(mqttSessionEstablished == true)
    {
        /* Disconnect from broker and close connection. */
        disconnect();
    }

    if(mqtthandle != NULL)
    {
        cy_mqtt_delete(mqtthandle);
        mqtthandle = NULL;
    }

    if(bufferSemInitialized == true)
    {
        /* Cleanup semaphore created for buffer operations. */
        vSemaphoreDelete( bufferSemaphore );
        printf("Destroyed buffer semaphore. \n");
    }

    if(mqttDisconSemInitialized == true)
    {
        /* Cleanup semaphore created for mqtt disconnect notification. */
        vSemaphoreDelete( mqtt_discon_Semaphore );
        printf("Destroyed mqtt disconnect notification semaphore. \n");
    }

    /* Wait and log message before exiting demo. */
    while(waitTimeoutMs > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(OTA_EXAMPLE_TASK_DELAY_MS));
        waitTimeoutMs -= OTA_EXAMPLE_TASK_DELAY_MS;
        printf("Exiting demo in %d sec", waitTimeoutMs / 1000);
    }

    if(result == CY_RSLT_SUCCESS)
    {
        printf("Demo status : Completed without any failures. \n");
    }
    else
    {
        printf("Demo status : Completed with failures. \n");
    }

    vTaskSuspend( NULL );
}

/*******************************************************************************
 * Function Name: connect_to_wifi_ap()
 *******************************************************************************
 * Summary:
 *  Connects to Wi-Fi AP using the user-configured credentials, retries up to a
 *  configured number of times until the connection succeeds.
 *
 * Return:
 *  CY_RSLT_SUCCESS: if a connection is established, other error code in case of
 *                  failure.
 *
 *******************************************************************************/
cy_rslt_t connect_to_wifi_ap(void)
{
    cy_wcm_config_t wifi_config = { .interface = CY_WCM_INTERFACE_TYPE_STA};
    cy_wcm_connect_params_t wifi_conn_param;
    cy_wcm_ip_address_t ip_address;
    cy_rslt_t result;

    /* Variable to track the number of connection retries to the Wi-Fi AP specified
     * by WIFI_SSID macro. */
    uint32_t conn_retries = 0;

    /* Initialize Wi-Fi connection manager. */
    cy_wcm_init(&wifi_config);

    /* Set the Wi-Fi SSID, password and security type. */
    memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
    memcpy(wifi_conn_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_conn_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifi_conn_param.ap_credentials.security = WIFI_SECURITY;

    /* Connect to the Wi-Fi AP */
    for(conn_retries = 0; conn_retries < MAX_CONNECTION_RETRIES; conn_retries++)
    {
        result = cy_wcm_connect_ap( &wifi_conn_param, &ip_address );

        if (result == CY_RSLT_SUCCESS)
        {
            printf( "Successfully connected to Wi-Fi network '%s'.\n",
                    wifi_conn_param.ap_credentials.SSID);
            return result;
        }

        printf( "Connection to Wi-Fi network failed with error code %d."
                "Retrying in %d ms...\n", (int) result, WIFI_CONN_RETRY_DELAY_MS );
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_DELAY_MS));
    }

    printf( "Exceeded maximum Wi-Fi connection attempts\n" );

    return result;
}

/*******************************************************************************
 * Function Name: startOTADemo()
 *******************************************************************************
 * Summary:
 *  The OTA task is created with initializing the OTA agent and
 *  setting the required interfaces. The demo loop then starts,
 *  establishing an MQTT connection with the broker and waiting
 *  for an update. After a successful update the OTA agent requests
 *  a manual reset to the downloaded executable.
 *
 * Return:
 *  CY_RSLT_SUCCESS - Application completed task, success.
 *  !CY_RSLT_SUCCESS - Application completed task, failure.
 *
 *******************************************************************************/
cy_rslt_t startOTADemo( void )
{

    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* OTA library return status. */
    OtaErr_t otaRet = OtaErrNone;

    /* OTA Agent state returned from calling OTA_GetAgentState.*/
    OtaState_t state = OtaAgentStateStopped;

    /* OTA event message used for sending event to OTA Agent.*/
    OtaEventMsg_t eventMsg = { 0 };

    /* OTA library packet statistics per job.*/
    OtaAgentStatistics_t otaStatistics = { 0 };

    /* OTA Agent thread handle.*/
    TaskHandle_t threadHandle = NULL;

    /* OTA interface context required for library interface functions.*/
    OtaInterfaces_t otaInterfaces;

    /* Maximum time to wait for the OTA agent to get suspended. */
    int16_t suspendTimeout;

    /* Set OTA Library interfaces.*/
    setOtaInterfaces(&otaInterfaces);

    printf("OTA over MQTT demo Application version \n\r");
    printf("Major version : %u \n\r", (appFirmwareVersion.u.x.major));
    printf("Minor version : %u \n\r", (appFirmwareVersion.u.x.minor));
    printf("Build version : %u \n\n\r", (appFirmwareVersion.u.x.build));

    /* Init OTA Library. */
    if(result == CY_RSLT_SUCCESS)
    {
        if((otaRet = OTA_Init( &otaBuffer, &otaInterfaces,
                ( const uint8_t * ) ( CLIENT_IDENTIFIER ),
                otaAppCallback ) ) != OtaErrNone)
        {
            printf("Failed to initialize OTA Agent, exiting = %u.\n\r", otaRet);
            result = !CY_RSLT_SUCCESS;
        }
    }

    /* Create OTA Task */
    if( result == CY_RSLT_SUCCESS )
    {
        if( (xTaskCreate(otaThread, "otaThread", OTA_THREAD_SIZE, NULL,
                OTA_THREAD_PRIORITY, &threadHandle)) != pdPASS)
        {
            printf("Failed to create OTA agent thread....!!!!!\n");
            result = !CY_RSLT_SUCCESS;
        }
        else
        {
            printf("OTA agent thread created ....\n");
        }
    }

    /* OTA Demo loop */
    if( result == CY_RSLT_SUCCESS )
    {
        printf("Calling create_mqtt_handle..\n");
        create_mqtt_handle();

        /* Wait till OTA library is stopped, output statistics for currently running
         * OTA job */
        while(( ( state = OTA_GetState() ) != OtaAgentStateStopped ))
        {
            if( mqttSessionEstablished != true )
            {
                /* Connect to MQTT broker and create MQTT connection. */
                printf("Calling establishConnection..\n");
                result = establishConnection();
                if(result == CY_RSLT_SUCCESS)
                {
                    /* Check if OTA process was suspended and resume if required. */
                    if( state == OtaAgentStateSuspended )
                    {
                        /* Resume OTA operations. */
                        OTA_Resume();
                    }
                    else
                    {
                        /* Send start event to OTA Agent.*/
                        eventMsg.eventId = OtaAgentEventStart;
                        OTA_SignalEvent( &eventMsg );
                    }
                }
            }

            if( mqttSessionEstablished == true )
            {
                if( pdTRUE == xSemaphoreTake(mqtt_discon_Semaphore, pdMS_TO_TICKS(500)))
                {
                    printf("Received MQTT disconnect notification...\n");
                    /* Disconnect from broker and close connection. */
                    disconnect();
                    /* Suspend OTA operations. */
                    otaRet = OTA_Suspend();
                    if( otaRet == OtaErrNone )
                    {
                        suspendTimeout = OTA_SUSPEND_TIMEOUT_MS;
                        while( ( ( state = OTA_GetState() ) != OtaAgentStateSuspended ) && ( suspendTimeout > 0 ) )
                        {
                            /* Wait for OTA Library state to suspend */
                            vTaskDelay(pdMS_TO_TICKS(OTA_EXAMPLE_TASK_DELAY_MS));
                            suspendTimeout -= OTA_EXAMPLE_TASK_DELAY_MS;
                        }
                    }
                    else
                    {
                        printf("OTA failed to suspend. StatusCode=%d.\n", otaRet);
                    }
                }
                else
                {
                    /* Get OTA statistics for currently executing job. */
                    OTA_GetStatistics( &otaStatistics );

                    /* Delay if mqtt process loop is set to zero.*/
                    if( MQTT_PROCESS_LOOP_TIMEOUT_MS > 0 )
                    {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                }
            }
        }
    }

    /* Wait for OTA Thread. */
    if( threadHandle != NULL )
    {
        vTaskDelete( threadHandle );
        printf("OTA thread terminated successfully..\n");
    }

    return result;
}

/*******************************************************************************
 * Function Name: otaAppCallback()
 *******************************************************************************
 * Summary:
 *  The OTA agent has completed the update job or it is in
 *  self test mode. If it was accepted, we want to activate the new image.
 *  This typically means we should reset the device to run the new firmware.
 *  If now is not a good time to reset the device, it may be activated later
 *  by your user code. If the update was rejected, just return without doing
 *  anything and we'll wait for another job. If it reported that we should
 *  start test mode, normally we would perform some kind of system checks to
 *  make sure our new firmware does the basic things we think it should do
 *  but we'll just go ahead and set the image as accepted for demo purposes.
 *  The accept function varies depending on your platform. Refer to the OTA
 *  PAL implementation for your platform in aws_ota_pal.c to see what it
 *  does for you.
 *
 * Parameters:
 *  event:  event Event from OTA lib.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void otaAppCallback( OtaJobEvent_t event, const void *pData )
{
    OtaErr_t err = OtaErrUninitialized;
    OtaFileContext_t *nw_ota_fs_ctx = NULL;

    switch( event )
    {
    case OtaJobEventActivate:
        printf("Received OtaJobEventActivate callback from OTA Agent.\n");
        /* Activate the new firmware image. */
        OTA_ActivateNewImage();

        /* Shutdown OTA Agent, if it is required that the unsubscribe
         * operations are not performed while shutting down please set
         * the second parameter to 0 instead of 1. */
        OTA_Shutdown(0, 1);

        /* Requires manual activation of new image.*/
        printf("New image activation failed.\n");

        break;

    case OtaJobEventFail:
        printf("Received OtaJobEventFail callback from OTA Agent.\n");
        /* Nothing special to do. The OTA agent handles it. */
        break;

    case OtaJobEventStartTest:

        /* This demo just accepts the image since it was a good OTA update
         * and networking and services are all working (or we would not have
         * made it this far). If this were some custom device that wants to
         * test other things before validating new image, this would be the
         * place to kick off those tests before calling OTA_SetImageState()
         * with the final result of either accepted or rejected. */

        printf("Received OtaJobEventStartTest callback from OTA Agent.\n");

        err = OTA_SetImageState(OtaImageStateAccepted);
        if(err != OtaErrNone)
        {
            printf("Failed to set image state as accepted.\n");
        }

        break;

    case OtaJobEventProcessed:
        printf("Received OtaJobEventProcessed callback from OTA Agent.\n");
        if(pData != NULL)
        {
            otaEventBufferFree(( OtaEventData_t * ) pData);
        }

        if(nw_ota_fs_ctx == NULL)
        {
            nw_ota_fs_ctx = (OtaFileContext_t *)cy_awsport_ota_flash_get_handle();
        }

        if(nw_ota_fs_ctx != NULL)
        {
            printf("\n\n==================================================================");
            printf("\nBlocks Remaining=%u", (unsigned int)nw_ota_fs_ctx->blocksRemaining);
            printf("\n==================================================================\n");
        }

        break;

    case OtaJobEventSelfTestFailed:
        printf("Received OtaJobEventSelfTestFailed callback from OTA Agent.\n");
        /* Requires manual activation of previous image as self-test for
         * new image downloaded failed.*/
        printf("Self-test failed, shutting down OTA Agent.\n");
        /* Shutdown OTA Agent, if it is required that the unsubscribe
         * operations are not performed while shutting down please set
         * the second parameter to 0 instead of 1. */
        OTA_Shutdown(0, 1);

        break;

    default:
        printf("Received invalid callback event from OTA Agent.\n");
    }
}

/*******************************************************************************
 * Function Name: setOtaInterfaces()
 *******************************************************************************
 * Summary:
 *  Set OTA interfaces.
 *
 * Parameters:
 *  pOtaInterfaces: pointer to OTA interface structure.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void setOtaInterfaces( OtaInterfaces_t * pOtaInterfaces )
{
    /* Initialize OTA library OS Interface. */
    pOtaInterfaces->os.event.init = cy_awsport_ota_event_init;
    pOtaInterfaces->os.event.send = cy_awsport_ota_event_send;
    pOtaInterfaces->os.event.recv = cy_awsport_ota_event_receive;
    pOtaInterfaces->os.event.deinit = cy_awsport_ota_event_deinit;
    pOtaInterfaces->os.timer.start = cy_awsport_ota_timer_create_start;
    pOtaInterfaces->os.timer.stop = cy_awsport_ota_timer_stop;
    pOtaInterfaces->os.timer.delete = cy_awsport_ota_timer_delete;
    pOtaInterfaces->os.mem.malloc = cy_awsport_ota_malloc;
    pOtaInterfaces->os.mem.free = cy_awsport_ota_free;

    /* Initialize the OTA library MQTT Interface.*/
    pOtaInterfaces->mqtt.subscribe = mqttSubscribe;
    pOtaInterfaces->mqtt.publish = mqttPublish;
    pOtaInterfaces->mqtt.unsubscribe = mqttUnsubscribe;

    /* Initialize the OTA library PAL Interface.*/
    pOtaInterfaces->pal.getPlatformImageState = cy_awsport_ota_flash_get_platform_imagestate;
    pOtaInterfaces->pal.setPlatformImageState = cy_awsport_ota_flash_set_platform_imagestate;
    pOtaInterfaces->pal.writeBlock = cy_awsport_ota_flash_write_block;
    pOtaInterfaces->pal.activate = cy_awsport_ota_flash_activate_newimage;
    pOtaInterfaces->pal.closeFile = cy_awsport_ota_flash_close_receive_file;
    pOtaInterfaces->pal.reset = cy_awsport_ota_flash_reset_device;
    pOtaInterfaces->pal.abort = cy_awsport_ota_flash_abort;
    pOtaInterfaces->pal.createFile = cy_awsport_ota_flash_create_receive_file;
}

/*******************************************************************************
 * Function Name: mqttSubscribe()
 *******************************************************************************
 * Summary:
 *  Subscribe to the MQTT topic filter, and registers the handler for the topic
 *  filter with the subscription manager.This function subscribes to the Mqtt
 *  topics with the Quality of service received as parameter. This function also
 *  registers a callback for the topicfilter.
 *
 * Parameters:
 *  pTopicFilter:       Mqtt topic filter.
 *  topicFilterLength:  Length of the topic filter.
 *  qos:                Quality of Service
 *
 * Return:
 *  OtaMqttSuccess: if success, other error code on failure.
 *
 *******************************************************************************/
OtaMqttStatus_t mqttSubscribe( const char *pTopicFilter,
        uint16_t topicFilterLength,
        uint8_t qos )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    cy_mqtt_subscribe_info_t sub_msg[1];

    if((pTopicFilter == NULL ) || (topicFilterLength == 0))
    {
        printf("Invalid parameters to MQTT subscribe. \n");
        return OtaMqttSubscribeFailed;
    }

    memset( &sub_msg, 0x00, sizeof( cy_mqtt_subscribe_info_t ));
    sub_msg[0].qos = (cy_mqtt_qos_t)qos;
    sub_msg[0].topic = pTopicFilter;
    sub_msg[0].topic_len = topicFilterLength;

    result = cy_mqtt_subscribe(mqtthandle, &sub_msg[0], 1);
    if(result != CY_RSLT_SUCCESS)
    {
        otaRet = OtaMqttSubscribeFailed;
        printf("cy_mqtt_subscribe failed with Error : [0x%X] \n\r",
                (unsigned int)result);
        printf("OTA MQTT subscribe failed. \n\r");
    }
    else
    {
        printf("OTA MQTT subscribe completed successfully. \n");
        printf("SUBSCRIBE topic %.*s to broker.\n\n", topicFilterLength,
                pTopicFilter);
    }

    registerSubscriptionManagerCallback(pTopicFilter, topicFilterLength);
    return otaRet;
}

/*******************************************************************************
 * Function Name: mqttPublish()
 *******************************************************************************
 * Summary:
 *  Publish message to a topic.This function publishes a message to a
 *  given topic & QoS.
 *
 * Parameters:
 *  pacTopic:   Mqtt topic filter.
 *  topicLen:   Length of the topic filter.
 *  pMsg:       Message to publish.
 *  msgSize:    Message size.
 *  qos:        Quality of Service
 *
 * Return:
 *  OtaMqttSuccess: if success, other error code on failure.
 *
 *******************************************************************************/
OtaMqttStatus_t mqttPublish( const char * const pacTopic,
        uint16_t topicLen,
        const char *pMsg,
        uint32_t msgSize,
        uint8_t qos )
{
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_publish_info_t pub_msg;

    if((pacTopic == NULL ) || (topicLen == 0 ) || (pMsg == NULL))
    {
        printf("Invalid parameters to MQTT Publish. \n");
        return OtaMqttPublishFailed;
    }

    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ));
    pub_msg.topic = pacTopic;
    pub_msg.topic_len = topicLen;
    pub_msg.qos = (cy_mqtt_qos_t)qos;
    pub_msg.payload = (const char *)pMsg;
    pub_msg.payload_len = msgSize;

    result = cy_mqtt_publish( mqtthandle, &pub_msg );
    if(result != CY_RSLT_SUCCESS)
    {
        otaRet = OtaMqttPublishFailed;
        printf("cy_mqtt_publish failed with Error : [0x%X]\n",
                (unsigned int)result);
        printf("OTA MQTT publish failed. \n");
    }
    else
    {
        printf("OTA MQTT publish completed successfully.\n");
        printf("Sent PUBLISH packet to broker %.*s to broker.\n", topicLen, pacTopic);
    }

    return otaRet;
}

/*******************************************************************************
 * Function Name: mqttUnsubscribe()
 *******************************************************************************
 * Summary:
 *  Unsubscribe to the Mqtt topics. This function unsubscribes to the Mqtt
 *  topics with the Quality of service received as parameter.
 *
 * Parameters:
 *  pTopicFilter:       Mqtt topic filter.
 *  topicFilterLength:  Length of the topic filter.
 *  qos:                Quality of Service
 *
 * Return:
 *  OtaMqttSuccess:     If success, other error code on failure.
 *
 *******************************************************************************/
OtaMqttStatus_t mqttUnsubscribe( const char * pTopicFilter,
        uint16_t topicFilterLength,
        uint8_t qos )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    cy_mqtt_unsubscribe_info_t unsub_msg[1];

    if( (pTopicFilter == NULL ) || (topicFilterLength == 0) )
    {
        printf("Invalid parameters to MQTT subscribe.\n");
        return OtaMqttUnsubscribeFailed;
    }

    memset( &unsub_msg, 0x00, sizeof( cy_mqtt_unsubscribe_info_t ));
    unsub_msg[0].qos = (cy_mqtt_qos_t)qos;
    unsub_msg[0].topic = pTopicFilter;
    unsub_msg[0].topic_len = topicFilterLength;

    result = cy_mqtt_unsubscribe(mqtthandle, &unsub_msg[0], 1);
    if(result != CY_RSLT_SUCCESS)
    {
        otaRet = OtaMqttUnsubscribeFailed;
        printf("cy_mqtt_unsubscribe failed with Error : [0x%X]\n",
                (unsigned int)result);
        printf("OTA MQTT unsubscribe failed.\n");
    }
    else
    {
        printf("OTA MQTT unsubscribe completed successfully.\n");
        printf("Unsubscribed topic %.*s from broker.\n", topicFilterLength,
                pTopicFilter);
    }

    return otaRet;
}

/*******************************************************************************
 * Function Name: otaEventBufferFree()
 *******************************************************************************
 * Summary:
 *  Function to frees OTA event buffer
 *
 * Parameters:
 *  pxBuffer:   Pointer to the event data structure.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void otaEventBufferFree( OtaEventData_t * const pxBuffer )
{
    if(pdTRUE == xSemaphoreTake(bufferSemaphore, pdMS_TO_TICKS(CY_RTOS_NEVER_TIMEOUT)))
    {
        pxBuffer->bufferUsed = false;
        ( void )xSemaphoreGive(bufferSemaphore);
        printf("otaEventBufferFree completed....!\n");
    }
    else
    {
        printf("Failed to get buffer semaphore\n");
        printf("otaEventBufferFree failed....!\n");
    }
}

/*******************************************************************************
 * Function Name: registerSubscriptionManagerCallback()
 *******************************************************************************
 * Summary:
 *  Registers a callback to subscription manager
 *
 * Parameters:
 *  pTopicFilter:       Mqtt topic filter.
 *  topicFilterLength:  Length of the topic filter.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void registerSubscriptionManagerCallback( const char * pTopicFilter,
        uint16_t topicFilterLength )
{
    bool isMatch = false;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    SubscriptionManagerStatus_t subscriptionStatus = SUBSCRIPTION_MANAGER_SUCCESS;

    uint16_t index = 0U;

    /* For suppressing compiler-warning: unused variable. */
    ( void ) mqttStatus;

    /* Lookup table for OTA message string. */
    static const char * const pWildCardTopicFilters[] =
    {
            OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/#",
            OTA_TOPIC_PREFIX OTA_TOPIC_STREAM "/#"
    };

    /* Match the input topic filter against the wild-card pattern of topics filters
     * relevant for the OTA Update service to determine the type of topic filter. */
    for( ; index < 2; index++ )
    {
        mqttStatus = MQTT_MatchTopic( pTopicFilter,
                topicFilterLength,
                pWildCardTopicFilters[ index ],
                strlen( pWildCardTopicFilters[ index ] ),
                &isMatch );
        if(mqttStatus != MQTTSuccess)
        {
            printf("MQTT_MatchTopic failed....\n");
        }
        else
        {
            if(isMatch)
            {
                /* Register callback to subscription manager. */
                subscriptionStatus = SubscriptionManager_RegisterCallback( pWildCardTopicFilters[ index ],
                        strlen( pWildCardTopicFilters[ index ] ),
                        otaMessageCallback[ index ] );

                if(subscriptionStatus != SUBSCRIPTION_MANAGER_SUCCESS)
                {
                    printf("Failed to register a callback to subscription "
                            "manager with error = %d.\n", subscriptionStatus);
                }
                else
                {
                    printf("Registered a callback to subscription manager "
                            "successfully.\n");
                }

                break;
            }
        }
    }
}

/*******************************************************************************
 * Function Name: mqttJobCallback()
 *******************************************************************************
 * Summary:
 *  Callback registered with the OTA library that notifies the OTA agent
 *  of an incoming PUBLISH containing a job document.
 *
 * Parameters:
 *  handle:         MQTT connection handle.
 *  pPublishInfo:   MQTT packet information which stores details of the job
 *                  document.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void mqttJobCallback( cy_mqtt_t handle, cy_mqtt_received_msg_info_t *pPublishInfo )
{
    OtaEventData_t * pData;
    OtaEventMsg_t eventMsg = { 0 };
    jobMessageType_t jobMessageType = jobMessageTypeNextGetAccepted;

    if( (pPublishInfo == NULL) || (handle == NULL) )
    {
        printf("Invalid input to mqttJobCallback....\n");
    }
    else
    {

        jobMessageType = getJobMessageType( pPublishInfo->topic, pPublishInfo->topic_len );
        switch( jobMessageType )
        {
        case jobMessageTypeNextGetAccepted:
        case jobMessageTypeNextNotify:
            pData = otaEventBufferGet();
            if( pData != NULL )
            {
                memcpy( pData->data, pPublishInfo->payload, pPublishInfo->payload_len );
                pData->dataLength = pPublishInfo->payload_len;
                eventMsg.eventId = OtaAgentEventReceivedJobDocument;
                eventMsg.pEventData = pData;

                /* Send job document received event. */
                OTA_SignalEvent( &eventMsg );
            }
            else
            {
                printf("No OTA data buffers available.\n");
            }

            break;

        default:
            printf("Received job message %s size %ld.\n",
                    pPublishInfo->topic,
                    (long int)pPublishInfo->payload_len);
        }
    }
}

/*******************************************************************************
 * Function Name: mqttDataCallback()
 *******************************************************************************
 * Summary:
 *  Callback that notifies the OTA library when a data block is received.
 *
 * Parameters:
 *  handle:         MQTT connection handle.
 *  pPublishInfo:   MQTT packet that stores the information of the file block.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void mqttDataCallback( cy_mqtt_t handle, cy_mqtt_received_msg_info_t *pPublishInfo )
{
    OtaEventData_t * pData;
    OtaEventMsg_t eventMsg = { 0 };

    if((pPublishInfo == NULL) || (handle == NULL))
    {
        printf("Invalid input to mqttDataCallback....\n");
    }
    else
    {
        printf("Received data message callback, size %u.\n", pPublishInfo->payload_len);

        pData = otaEventBufferGet();
        if( pData != NULL )
        {
            memcpy( pData->data, pPublishInfo->payload, pPublishInfo->payload_len );
            pData->dataLength = pPublishInfo->payload_len;
            eventMsg.eventId = OtaAgentEventReceivedFileBlock;
            eventMsg.pEventData = pData;

            /* Send job document received event. */
            OTA_SignalEvent(&eventMsg);
        }
        else
        {
            printf("No OTA data buffers available.\n");
        }
    }
}

/*******************************************************************************
 * Function Name: getJobMessageType()
 *******************************************************************************
 * Summary:
 *  Identify the type of job notification.
 *
 * Parameters:
 *  pTopicName:         input topic filter.
 *  topicNameLength:    length of input topic filter.
 *
 * Return:
 *  jobMessageType_t:   index of job message.
 *
 *******************************************************************************/
jobMessageType_t getJobMessageType( const char * pTopicName,
        uint16_t topicNameLength )
{
    uint16_t index = 0U;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    bool isMatch = false;
    jobMessageType_t jobMessageIndex = jobMessageTypeMax;

    /* For suppressing compiler-warning: unused variable. */
    ( void ) mqttStatus;

    /* Lookup table for OTA job message string. */
    static const char * const pJobTopicFilters[ jobMessageTypeMax ] =
    {
            OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/$next/get/accepted",
            OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/notify-next",
    };

    /* Match the input topic filter against the wild-card pattern of topics filters
     * relevant for the OTA Update service to determine the type of topic filter. */
    for( ; index < jobMessageTypeMax; index++ )
    {
        mqttStatus = MQTT_MatchTopic( pTopicName,
                topicNameLength,
                pJobTopicFilters[ index ],
                strlen( pJobTopicFilters[ index ] ),
                &isMatch );
        if( mqttStatus != MQTTSuccess )
        {
            printf("MQTT_MatchTopic failed....\n");
        }
        else
        {
            if( isMatch )
            {
                jobMessageIndex = (jobMessageType_t)index;
                break;
            }
        }
    }

    return jobMessageIndex;
}

/*******************************************************************************
 * Function Name: otaEventBufferGet()
 *******************************************************************************
 * Summary:
 *  Function retrieves unused OTA event buffer.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  OtaEventData_t: pointer to free event buffer location.
 *
 *******************************************************************************/
OtaEventData_t * otaEventBufferGet(void)
{
    uint32_t ulIndex = 0;
    OtaEventData_t * pFreeBuffer = NULL;

    if(pdTRUE == xSemaphoreTake(bufferSemaphore, pdMS_TO_TICKS(CY_RTOS_NEVER_TIMEOUT)))
    {
        for( ulIndex = 0; ulIndex < otaconfigMAX_NUM_OTA_DATA_BUFFERS; ulIndex++ )
        {
            if( eventBuffer[ ulIndex ].bufferUsed == false )
            {
                eventBuffer[ ulIndex ].bufferUsed = true;
                pFreeBuffer = &eventBuffer[ ulIndex ];
                break;
            }
        }

        (void)xSemaphoreGive(bufferSemaphore);
    }
    else
    {
        printf("Failed to get buffer semaphore\n");
    }

    return pFreeBuffer;
}

/*******************************************************************************
 * Function Name: otaThread()
 *******************************************************************************
 * Summary:
 *  Thread to call the OTA agent task.
 *
 * Parameters:
 *  pParam: Can be used to pass down functionality to the agent task.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void otaThread( void * arg )
{
    /* Calling OTA agent task. */
    OTA_EventProcessingTask( (void *)arg );
    printf("OTA Agent stopped.\n");
}

/*******************************************************************************
 * Function Name: create_mqtt_handle()
 *******************************************************************************
 * Summary:
 *  Function that creates an instance for the MQTT client. The network buffer
 *  needed by the MQTT library for MQTT send and receive operations is also
 *  allocated by this function.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void create_mqtt_handle( void )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_awsport_ssl_credentials_t credentials;
    cy_awsport_ssl_credentials_t *security = NULL;
    cy_mqtt_broker_info_t broker_info;

    memset( &credentials, 0x00, sizeof( cy_awsport_ssl_credentials_t ));
    memset( &broker_info, 0x00, sizeof( cy_mqtt_broker_info_t ));

#ifndef CY_SECURE_SOCKETS_PKCS_SUPPORT
    /* Set credential information. */
    credentials.client_cert = (const char *) &aws_client_cert;
    credentials.client_cert_size = sizeof( aws_client_cert );
    credentials.private_key = (const char *) &aws_client_key;
    credentials.private_key_size = sizeof( aws_client_key );
    credentials.root_ca = (const char *) &aws_root_ca_certificate;
    credentials.root_ca_size = sizeof( aws_root_ca_certificate );
#endif

#if (AWS_MQTT_PORT == 443)
    credentials.alpnprotos = AWS_IOT_MQTT_ALPN;
    credentials.alpnprotoslen = AWS_IOT_MQTT_ALPN_LENGTH;
#endif

    credentials.sni_host_name = AWS_IOT_ENDPOINT;
    credentials.sni_host_name_size = AWS_IOT_ENDPOINT_LENGTH;
    broker_info.hostname = AWS_IOT_ENDPOINT;
    broker_info.hostname_len = AWS_IOT_ENDPOINT_LENGTH - 1;
    broker_info.port = AWS_MQTT_PORT;
    security = &credentials;

    result = cy_mqtt_create((uint8_t *)&otaNetworkBuffer, OTA_NETWORK_BUFFER_SIZE,
            security, &broker_info, (cy_mqtt_callback_t)mqtt_event_cb, NULL,
            &mqtthandle);
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Failed to create MQTT handle..\n");
    }
    else
    {
        printf("Created MQTT handle successfully. Handle = %p \n",
                mqtthandle);
    }
}

/*******************************************************************************
 * Function Name: establishConnection()
 *******************************************************************************
 * Summary:
 *  Function to attempt connection to MQTT Broker.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  CY_RSLT_SUCCESS: if a connection is established, other error code in case of
 *                  failure.
 *
 *******************************************************************************/
cy_rslt_t establishConnection(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    cy_mqtt_connect_info_t connect_info;

    memset( &connect_info, 0x00, sizeof( cy_mqtt_connect_info_t ));

    connect_info.client_id = CLIENT_IDENTIFIER;
    connect_info.client_id_len = CLIENT_IDENTIFIER_LENGTH;
    connect_info.keep_alive_sec = OTA_MQTT_KEEP_ALIVE_INTERVAL_SECONDS;
    connect_info.will_info = NULL;
    connect_info.clean_session = true;

    result = cy_mqtt_connect( mqtthandle, &connect_info );
    if(result == CY_RSLT_SUCCESS)
    {
        printf("Established MQTT Connection......\n");
        printf("MQTT broker %.*s.\n", AWS_IOT_ENDPOINT_LENGTH, AWS_IOT_ENDPOINT);
        mqttSessionEstablished = true;
        result = CY_RSLT_SUCCESS;
    }
    else
    {
        printf("Failed to Establish MQTT Connection...\n");
        printf("MQTT broker %.*s.\n", AWS_IOT_ENDPOINT_LENGTH, AWS_IOT_ENDPOINT);
    }

    return result;
}

/*******************************************************************************
 * Function Name: disconnect()
 *******************************************************************************
 * Summary:
 *  Function to disconnect from the MQTT broker and close connection.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void disconnect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    /* Disconnect from broker. */
    printf("Disconnecting the MQTT connection with %s.\n", AWS_IOT_ENDPOINT);

    if(mqttSessionEstablished == true)
    {
        result = cy_mqtt_disconnect(mqtthandle);
        if(result == CY_RSLT_SUCCESS)
        {
            printf("MQTT connection close completed.... \n");
        }
        else
        {
            printf("MQTT connection close failed.... \n");
        }

        /* Clear the mqtt session flag. */
        mqttSessionEstablished = false;
    }
    else
    {
        printf("MQTT already disconnected.\n");
    }
}

/*******************************************************************************
 * Function Name: mqtt_event_cb()
 *******************************************************************************
 * Summary:
 *  Callback invoked by the MQTT library for events like MQTT disconnection,
 *  incoming MQTT subscription messages from the MQTT broker.
 *      1. In case of MQTT disconnection, the MQTT client task is communicated
 *       about the disconnection using a message queue.
 *      2. When an MQTT subscription message is received, the subscriber callback
 *       function implemented in subscriber_task.c is invoked to handle the
 *       incoming MQTT message.
 *
 * Parameters:
 *  mqtt_handle:    MQTT handle corresponding to the MQTT event (unused)
 *  event:          MQTT event information
 *  user_data:      User data pointer passed during cy_mqtt_create() (unused)
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void mqtt_event_cb( cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *user_data )
{
    cy_mqtt_received_msg_info_t *received_msg;

    (void)user_data;

    switch(event.type)
    {
    case CY_MQTT_EVENT_TYPE_DISCONNECT :
    {
        printf("\nEvent : Received MQTT Disconnect event.\n");
        switch(event.data.reason)
        {
        case CY_MQTT_DISCONN_TYPE_BROKER_DOWN :
            /* Keep-alive response not received from broker, possibly broker is down. */
            printf("Reason : MQTT Ping response not received within keep-alive "
                    "response timeout...\n");
            break;

        case CY_MQTT_DISCONN_TYPE_NETWORK_DOWN :
            /* Network is disconnected. */
            printf("Reason : Network is disconnected...\n");
            break;

        case CY_MQTT_DISCONN_TYPE_SND_RCV_FAIL :
            /* MQTT packet send or receive operation failed due to network latency
             * (or) send/receive related timeouts. */
            printf("Reason : MQTT packet send or receive operation failed...\n");
            break;

        case CY_MQTT_DISCONN_TYPE_BAD_RESPONSE :
            /* Bad response from MQTT broker. Possibly received MQTT packet with
             * invalid packet type ID. */
            printf("Reason : Bad response from MQTT broker...\n");
            break;

        default :
            printf("\n Unknown disconnect reason .....\n");
            break;
        }

        if( pdFALSE == xSemaphoreGive(mqtt_discon_Semaphore))
        {
            printf("Disconnect notification semaphore post failed..!!!\n");
        }
    }
    break;

    case CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE :
        /* Received MQTT messages on subscribed topic. */
        printf("\nEvent : Received MQTT subscribed message receive event.\n");
        received_msg = &(event.data.pub_msg.received_message);
        printf("Incoming Publish Topic Name: %.*s\n", received_msg->topic_len,
                received_msg->topic);
        printf("Incoming Publish message Packet Id is %u.\n", event.data.pub_msg.packet_id);
        printf("Incoming Publish message Payload length is %u.\n",
                (uint16_t) received_msg->payload_len);
        SubscriptionManager_DispatchHandler(mqtt_handle, received_msg);
        break;

    default :
        printf("Unknown event .....\n");
        break;
    }
}

/* [] END OF FILE */
