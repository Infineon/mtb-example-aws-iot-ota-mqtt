/********************************************************************************
 * File Name: mqtt_subscription_manager.h
 *
 * Description: The API of a subscription manager for handling
 * subscription callbacks to topic filters in MQTT operations.
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

#ifndef MQTT_SUBSCRIPTION_MANAGER_H_
#define MQTT_SUBSCRIPTION_MANAGER_H_

/* Include MQTT library. */
#include "core_mqtt.h"

/* MQTT include. */
#include "cy_mqtt_api.h"


/* Enumeration type for return status value from Subscription Manager API. */
typedef enum SubscriptionManagerStatus
{
    /**
     * @brief Success return value from Subscription Manager API.
     */
    SUBSCRIPTION_MANAGER_SUCCESS = 1,

    /**
     * @brief Failure return value due to registry being full.
     */
    SUBSCRIPTION_MANAGER_REGISTRY_FULL = 2,

    /**
     * @brief Failure return value due to an already existing record in the
     * registry for a new callback registration's requested topic filter.
     */
    SUBSCRIPTION_MANAGER_RECORD_EXISTS = 3
} SubscriptionManagerStatus_t;


/*******************************************************************************
 * Function Name: (* SubscriptionManagerCallback_t )()
 *******************************************************************************
 * Summary:
 * Callback type to be registered for a topic filter with the subscription manager.
 *
 * For incoming PUBLISH messages received on topics that match the registered topic filter,
 * the callback would be invoked by the subscription manager.
 *
 * Parameters:
 * phandle - The handle associated with the MQTT connection.
 * pPublishInfo - The incoming PUBLISH message information.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
typedef void (* SubscriptionManagerCallback_t )( cy_mqtt_t phandle,
        cy_mqtt_received_msg_info_t *pPublishInfo );


/******************************************************************************
 * Function Prototypes
 *******************************************************************************/
void SubscriptionManager_DispatchHandler( cy_mqtt_t phandle,
        cy_mqtt_received_msg_info_t * pPublishInfo );

SubscriptionManagerStatus_t SubscriptionManager_RegisterCallback( const char * pTopicFilter,
        uint16_t topicFilterLength,
        SubscriptionManagerCallback_t pCallback );

void SubscriptionManager_RemoveCallback( const char * pTopicFilter,
        uint16_t topicFilterLength );


#endif /* ifndef MQTT_SUBSCRIPTION_MANAGER_H_ */
