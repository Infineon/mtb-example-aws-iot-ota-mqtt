/******************************************************************************
 * File Name: mqtt_main.h
 *
 * Description: Contains all the AWS IoT device configurations required by the
 * AWS OTA feature demo.
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

#ifndef SOURCE_CREDENTIALS_CONFIG_H_
#define SOURCE_CREDENTIALS_CONFIG_H_

/*******************************************************************************
 * Macros
 ********************************************************************************/
/* Macro for Wi-Fi connection */
/* Wi-Fi SSID */
#define WIFI_SSID                               "Enter your Wi-Fi SSID here"

/* Wi-Fi PASSWORD */
#define WIFI_PASSWORD                           "Enter your Wi-Fi password here"

/* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
 * in "cy_wcm.h" for more details.
 */
/* default option - CY_WCM_SECURITY_WPA2_AES_PSK */
#define WIFI_SECURITY                           (CY_WCM_SECURITY_WPA2_AES_PSK)

/* MAX connection retries to join WI-FI AP */
#define MAX_CONNECTION_RETRIES                  (10u)

/* Wait between Wi-Fi connection retries */
#define WIFI_CONN_RETRY_DELAY_MS                (500)

/* AWS IoT thing name */
#define CLIENT_IDENTIFIER                       "Enter you AWS IoT thing name here"

/* AWS IoT device data endpoint */
#define AWS_IOT_ENDPOINT                        "Enter your AWS IoT device data endpoint here"

/* AWS IoT MQTT port number*/
#define AWS_MQTT_PORT                           (8883)

/**
 * MQTT supported QoS levels.
 */
typedef enum cy_demo_mqtt_qos
{
    CY_MQTT_QOS_0 = 0, /**< Delivery at most once. */
    CY_MQTT_QOS_1 = 1, /**< Delivery at least once. */
    CY_MQTT_QOS_2 = 2  /**< Delivery exactly once. */
} cy_demo_mqtt_qos_t;

/* AWS Broker connection Info */
/* The aws_root_ca_certificate field requires Amazon Root CA 1
 * certificate for AWS OTA update feature. AmazonRootCA1 is generated while
 * 'Thing' creation on AWS portal.
 *
 * Must follow the below format and include the PEM header and footer:
        "-----BEGIN CERTIFICATE-----\n"
        ".........base64 data.......\n"
        "-----END CERTIFICATE-----";
 */
static const char aws_root_ca_certificate[] = "";

/* AWS - Device certificate generated while 'Thing' creation on AWS IoT portal.*/
/* Must follow the below format and include the PEM header and footer:
        "-----BEGIN CERTIFICATE-----\n"
        ".........base64 data.......\n"
        "-----END CERTIFICATE-------";
 */
static const char aws_client_cert[] = "";

/* AWS - Device Private key generated while 'Thing' creation on AWS IoT portal.*/
/* Must follow the below format and include the PEM header and footer:
        "-----BEGIN PRIVATE KEY-----\n"
        ".........base64 data.......\n"
        "-----END PRIVATE KEY-----";
 */
static const char aws_client_key[] = "";

#endif /* SOURCE_CREDENTIALS_CONFIG_H_ */
