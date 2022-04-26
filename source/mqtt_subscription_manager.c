/********************************************************************************
 * File Name: mqtt_subscription_manager.c
 *
 * Description: Implementation of the API of a subscription manager for handling
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

/* Standard includes. */
#include <string.h>
#include <assert.h>

/* Include header for the subscription manager. */
#include "mqtt_subscription_manager.h"

/**
 * @brief Represents a registered record of the topic filter and its associated callback
 * in the subscription manager registry.
 */
typedef struct SubscriptionManagerRecord
{
    const char * pTopicFilter;
    uint16_t topicFilterLength;
    SubscriptionManagerCallback_t callback;
} SubscriptionManagerRecord_t;

/**
 * @brief The default value for the maximum size of the callback registry in the
 * subscription manager.
 */
#ifndef MAX_SUBSCRIPTION_CALLBACK_RECORDS
#define MAX_SUBSCRIPTION_CALLBACK_RECORDS    5
#endif

/**
 * @brief The registry to store records of topic filters and their subscription callbacks.
 */
static SubscriptionManagerRecord_t callbackRecordList[ MAX_SUBSCRIPTION_CALLBACK_RECORDS ] = { 0 };


/*******************************************************************************
 * Function Name: SubscriptionManager_DispatchHandler()
 *******************************************************************************
 * Summary:
 * Dispatches the incoming PUBLISH message to the callbacks that have their
 * registered topic filters matching the incoming PUBLISH topic name. The dispatch
 * handler will invoke all these callbacks with matching topic filters.
 *
 * Parameters:
 *  handle:  The handle associated with the MQTT connection.
 *  pPublishInfo: The incoming PUBLISH message information.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void SubscriptionManager_DispatchHandler( cy_mqtt_t handle,
        cy_mqtt_received_msg_info_t * pPublishInfo )
{
    bool matchStatus = false;
    size_t listIndex = 0u;

    assert( pPublishInfo != NULL );
    assert( handle != NULL );

    /* Iterate through record list to find matching topics, and invoke their callbacks. */
    for( listIndex = 0; listIndex < MAX_SUBSCRIPTION_CALLBACK_RECORDS; listIndex++ )
    {
        if( ( callbackRecordList[ listIndex ].pTopicFilter != NULL ) &&
                ( MQTT_MatchTopic( pPublishInfo->topic,
                        pPublishInfo->topic_len,
                        callbackRecordList[ listIndex ].pTopicFilter,
                        callbackRecordList[ listIndex ].topicFilterLength,
                        &matchStatus ) == MQTTSuccess ) &&
                        ( matchStatus == true ) )
        {
            LogInfo( ( "Invoking subscription callback of matching topic filter: "
                    "TopicFilter=%.*s, TopicName=%.*s",
                    callbackRecordList[ listIndex ].topicFilterLength,
                    callbackRecordList[ listIndex ].pTopicFilter,
                    pPublishInfo->topic,
                    pPublishInfo->topic_len ) );

            /* Invoke the callback associated with the record as the topics match. */
            callbackRecordList[ listIndex ].callback( handle, pPublishInfo );
        }
    }
}


/*******************************************************************************
 * Function Name: SubscriptionManager_RegisterCallback()
 *******************************************************************************
 * Summary:
 * Utility to register a callback for a topic filter in the subscription manager.
 *
 * The callback will be invoked when an incoming PUBLISH message is received on
 * a topic that matches the topic filter, @a pTopicFilter. The subscription manager
 * accepts wildcard topic filters.
 *
 * Parameters:
 *  pTopicFilter: The topic filter to register the callback for.
 *  topicFilterLength: The length of the topic filter string.
 *  callback: The callback to be registered for the topic filter.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
SubscriptionManagerStatus_t SubscriptionManager_RegisterCallback( const char * pTopicFilter,
        uint16_t topicFilterLength,
        SubscriptionManagerCallback_t callback )
{
    assert( pTopicFilter != NULL );
    assert( topicFilterLength != 0 );
    assert( callback != NULL );

    SubscriptionManagerStatus_t returnStatus;
    size_t availableIndex = MAX_SUBSCRIPTION_CALLBACK_RECORDS;
    bool recordExists = false;
    size_t index = 0u;

    /* Search for the first available spot in the list to store the record, and also check if
     * a record for the topic filter already exists. */
    while( ( recordExists == false ) && ( index < MAX_SUBSCRIPTION_CALLBACK_RECORDS ) )
    {
        /* Check if the index represents an empty spot in the registry. If we had already
         * found an empty spot in the list, we will not update it. */
        if( ( availableIndex == MAX_SUBSCRIPTION_CALLBACK_RECORDS ) &&
                ( callbackRecordList[ index ].pTopicFilter == NULL ) )
        {
            availableIndex = index;
        }

        /* Check if the current record's topic filter in the registry matches the topic filter
         * we are trying to register. */
        else if( ( callbackRecordList[ index ].topicFilterLength == topicFilterLength ) &&
                ( strncmp( pTopicFilter, callbackRecordList[ index ].pTopicFilter,
                        topicFilterLength ) == 0 ) )
        {
            recordExists = true;
        }

        index++;
    }

    if( recordExists == true )
    {
        /* The record for the topic filter already exists. */
        LogError( ( "Failed to register callback: Record for topic filter "
                "already exists: TopicFilter=%.*s", topicFilterLength, pTopicFilter ) );

        returnStatus = SUBSCRIPTION_MANAGER_RECORD_EXISTS;
    }
    else if( availableIndex == MAX_SUBSCRIPTION_CALLBACK_RECORDS )
    {
        /* The registry is full. */
        LogError( ( "Unable to register callback: Registry list is full: "
                "TopicFilter=%.*s, MaxRegistrySize=%u", topicFilterLength,
                pTopicFilter, MAX_SUBSCRIPTION_CALLBACK_RECORDS ) );

        returnStatus = SUBSCRIPTION_MANAGER_REGISTRY_FULL;
    }
    else
    {
        callbackRecordList[ availableIndex ].pTopicFilter = pTopicFilter;
        callbackRecordList[ availableIndex ].topicFilterLength = topicFilterLength;
        callbackRecordList[ availableIndex ].callback = callback;

        returnStatus = SUBSCRIPTION_MANAGER_SUCCESS;

        LogDebug( ( "Added callback to registry: TopicFilter=%.*s",
                topicFilterLength,
                pTopicFilter ) );
    }

    return returnStatus;
}

/*******************************************************************************
 * Function Name: SubscriptionManager_RemoveCallback()
 *******************************************************************************
 * Summary:
 * Utility to remove the callback registered for a topic filter from the
 * subscription manager.
 *
 * Parameters:
 *  pTopicFilter: The topic filter to remove from the subscription manager.
 *  topicFilterLength: The length of the topic filter string.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void SubscriptionManager_RemoveCallback( const char * pTopicFilter,
        uint16_t topicFilterLength )
{
    assert( pTopicFilter != NULL );
    assert( topicFilterLength != 0 );

    size_t index;
    SubscriptionManagerRecord_t * pRecord = NULL;

    /* Iterate through the records list to find the matching record. */
    for( index = 0; index < MAX_SUBSCRIPTION_CALLBACK_RECORDS; index++ )
    {
        pRecord = &callbackRecordList[ index ];

        /* Only match the non-empty records. */
        if( pRecord->pTopicFilter != NULL )
        {
            if( ( topicFilterLength == pRecord->topicFilterLength ) &&
                    ( strncmp( pTopicFilter, pRecord->pTopicFilter, topicFilterLength ) == 0 ) )
            {
                break;
            }
        }
    }

    /* Delete the record by clearing the found entry in the records list. */
    if( index < MAX_SUBSCRIPTION_CALLBACK_RECORDS )
    {
        pRecord->pTopicFilter = NULL;
        pRecord->topicFilterLength = 0u;
        pRecord->callback = NULL;

        LogDebug( ( "Deleted callback record for topic filter: TopicFilter=%.*s",
                topicFilterLength,
                pTopicFilter ) );
    }
    else
    {
        LogWarn( ( "Attempted to remove callback for un-registered "
                "topic filter: TopicFilter=%.*s", topicFilterLength, pTopicFilter ) );
    }
}

/* [] END OF FILE */
