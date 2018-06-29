//----------------------------------------------------------------------------
// Copyright Notice:
// Copyright 2017 Distributed Management Task Force, Inc. All rights reserved.
// License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libredfish/blob/master/LICENSE.md
//----------------------------------------------------------------------------
#include <string.h>

#include "redfishPayload.h"
#include "debug.h"

static redfishPayload* getOpResult(redfishPayload* payload, const char* propName, const char* op, const char* value);
static bool            getOpResultAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context);
static redfishPayload* collectionEvalOp(redfishPayload* payload, const char* propName, const char* op, const char* value);
static bool            collectionEvalOpAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context);
static redfishPayload* arrayEvalOp(redfishPayload* payload, const char* propName, const char* op, const char* value);
static bool            arrayEvalOpAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context);
static redfishPayload* createCollection(redfishService* service, size_t count, redfishPayload** payloads);
static json_t*         json_object_get_by_index(json_t* json, size_t index);
static bool            isOdataIdNode(json_t* json, char** uriPtr);

redfishPayload* createRedfishPayload(json_t* value, redfishService* service)
{
    redfishPayload* payload;
    payload = (redfishPayload*)calloc(sizeof(redfishPayload), 1);
    if(payload != NULL)
    {
        payload->json = value;
        payload->service = service;
        payload->contentType = PAYLOAD_CONTENT_JSON;
    }
    return payload;
}

redfishPayload* createRedfishPayloadFromString(const char* value, redfishService* service)
{
    json_t* jValue = json_loads(value, 0, NULL);
    if(jValue == NULL)
    {
        return NULL;
    }
    return createRedfishPayload(jValue, service);
}

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

REDFISH_EXPORT redfishPayload* createRedfishPayloadFromContent(const char* content, size_t contentLength, const char* contentType, redfishService* service)
{
    redfishPayload* ret;
    if(contentType == NULL || strncasecmp(contentType, "application/json", 16) == 0)
    {
        return createRedfishPayloadFromString(content, service);
    }
    //Other payload, treat as binary for now...
    ret = (redfishPayload*)calloc(sizeof(redfishPayload), 1);
    if(ret != NULL)
    {
        if(contentLength == 0)
        {
            ret->content = NULL;
        }
        else
        {
            ret->content = malloc(contentLength);
            memcpy(ret->content, content, contentLength);
        } 
        ret->contentLength = contentLength;
        ret->contentType = PAYLOAD_CONTENT_OTHER;
        ret->contentTypeStr = strdup(contentType);
        ret->service = service;
    }
    return ret;
}

bool isPayloadCollection(redfishPayload* payload)
{
    json_t* members;
    json_t* count;

    if(!payload || !json_is_object(payload->json))
    {
        return false;
    }
    members = json_object_get(payload->json, "Members");
    count = json_object_get(payload->json, "Members@odata.count");
    return ((members != NULL) && (count != NULL));
}

bool isPayloadArray(redfishPayload* payload)
{
    if(!payload || !json_is_array(payload->json))
    {
        return false;
    }
    return true;
}

size_t getPayloadSize(redfishPayload* payload)
{
    char* body;
    size_t len;
    if(payload->contentType != PAYLOAD_CONTENT_JSON)
    {
        return payload->contentLength;
    }
    body = payloadToString(payload, false);
    len = strlen(body)+1;
    free(body);
    return len;
}

char* getPayloadBody(redfishPayload* payload)
{
    if(payload->contentType != PAYLOAD_CONTENT_JSON)
    {
        return payload->content;
    }
    return payloadToString(payload, false);
}

char* getPayloadContentType(redfishPayload* payload)
{
    if(payload->contentType != PAYLOAD_CONTENT_OTHER)
    {
        return payload->contentTypeStr;
    }
    return "application/json";
}

char* getPayloadStringValue(redfishPayload* payload)
{
    json_t* tmp;
    const char* value = json_string_value(payload->json);
    if(value == NULL)
    {
        if(json_object_size(payload->json) == 1)
        {
            tmp = json_object_get_by_index(payload->json, 0);
            if(tmp != NULL)
            {
                value = json_string_value(tmp);
            }
        }
        if(value == NULL)
        {
            return NULL;
        }
    }
#ifdef _MSC_VER
	return _strdup(value);
#else
    return strdup(value);
#endif
}

int getPayloadIntValue(redfishPayload* payload)
{
    return (int)json_integer_value(payload->json);
}

redfishPayload* getPayloadByNodeName(redfishPayload* payload, const char* nodeName)
{
    json_t* value;
    json_t* odataId;
    const char* uri;

    if(!payload || !nodeName)
    {
        return NULL;
    }

    value = json_object_get(payload->json, nodeName);
    if(value == NULL)
    {
        return NULL;
    }
    json_incref(value);
    if(json_object_size(value) == 1)
    {
        odataId = json_object_get(value, "@odata.id");
        if(odataId != NULL)
        {
            json_incref(odataId);
            uri = json_string_value(odataId);
            json_decref(value);
            value = getUriFromService(payload->service, uri);
            json_decref(odataId);
            if(value == NULL)
            {
                return NULL;
            }
        }
    }
    if(json_is_string(value))
    {
        odataId = json_object();
        json_object_set(odataId, nodeName, value);
        json_decref(value);
        value = odataId;
    }
    return createRedfishPayload(value, payload->service);
}

redfishPayload* getPayloadByIndex(redfishPayload* payload, size_t index)
{
    json_t* value = NULL;
    json_t* odataId;
    const char* uri;

    if(!payload)
    {
        return NULL;
    }
    if(isPayloadCollection(payload))
    {
        redfishPayload* members = getPayloadByNodeName(payload, "Members");
        redfishPayload* ret = getPayloadByIndex(members, index);
        cleanupPayload(members);
        return ret;
    }
    if(json_is_array(payload->json))
    {
        value = json_array_get(payload->json, index);
    }
    else if(json_is_object(payload->json))
    {
        value = json_object_get_by_index(payload->json, index);
    }

    if(value == NULL)
    {
        return NULL;
    }

    json_incref(value);
    if(json_object_size(value) == 1)
    {
        odataId = json_object_get(value, "@odata.id");
        if(odataId != NULL)
        {
            uri = json_string_value(odataId);
            json_decref(value);
            value = getUriFromService(payload->service, uri);
            if(value == NULL)
            {
                return NULL;
            }
        }
    }
    return createRedfishPayload(value, payload->service);
}

size_t getValueCountFromPayload(redfishPayload* payload)
{
    if(!payload)
    {
        return 0;
    }
    if(json_is_array(payload->json))
    {
        return json_array_size(payload->json);
    }
    else if(json_is_object(payload->json))
    {
        return json_object_size(payload->json);
    }
    else
    {
        return 1;
    }
}

redfishPayload* getPayloadForPath(redfishPayload* payload, redPathNode* redpath)
{
    redfishPayload* ret = NULL;
    redfishPayload* tmp;

    if(!payload || !redpath)
    {
        return NULL;
    }

    if(redpath->nodeName)
    {
        ret = getPayloadByNodeName(payload, redpath->nodeName);
    }
    else if(redpath->isIndex)
    {
        ret = getPayloadByIndex(payload, redpath->index);
    }
    else if(redpath->op)
    {
        ret = getOpResult(payload, redpath->propName, redpath->op, redpath->value);
    }
    else
    {
        return NULL;
    }

    if(redpath->next == NULL || ret == NULL)
    {
        return ret;
    }
    else
    {
        tmp = getPayloadForPath(ret, redpath->next);
        cleanupPayload(ret);
        return tmp;
    }
}

redfishPayload* getPayloadForPathString(redfishPayload* payload, const char* string)
{
    redPathNode* redpath;
    redfishPayload* ret;

    if(!string)
    {
        return NULL;
    }
    redpath = parseRedPath(string);
    if(redpath == NULL)
    {
        return NULL;
    }
    ret = getPayloadForPath(payload, redpath);
    cleanupRedPath(redpath);
    return ret;
}

size_t getCollectionSize(redfishPayload* payload)
{
    json_t* members;
    json_t* count;

    if(!payload || !json_is_object(payload->json))
    {
        return 0;
    }
    members = json_object_get(payload->json, "Members");
    count = json_object_get(payload->json, "Members@odata.count");
    if(!members || !count)
    {
        return 0;
    }
    return (size_t)json_integer_value(count);
}

redfishPayload* patchPayloadStringProperty(redfishPayload* payload, const char* propertyName, const char* value)
{
    json_t* json;
    json_t* json2;
    char* content;
    char* uri;

    if(!payload || !propertyName || !value)
    {
        return NULL;
    }

    json = json_object_get(payload->json, "@odata.id");
    if(json == NULL)
    {
        return NULL;
    }
    uri = strdup(json_string_value(json));

    json = json_object();
    json2 = json_string(value);
    json_object_set(json, propertyName, json2);
    content = json_dumps(json, 0);
    json_decref(json2);
    json_decref(json);

    json = patchUriFromService(payload->service, uri, content);
    free(uri);
    free(content);
    if(json == NULL)
    {
        return NULL;
    }
    return createRedfishPayload(json, payload->service);
}

redfishPayload* postContentToPayload(redfishPayload* target, const char* data, size_t dataSize, const char* contentType)
{
    json_t* json;
    char* uri;

    if(!target || !data)
    {
        return NULL;
    }
    json = json_object_get(target->json, "@odata.id");
    if(json == NULL)
    {
        json = json_object_get(target->json, "target");
        if(json == NULL)
        {
            return NULL;
        }
    }
    uri = strdup(json_string_value(json));
    json = postUriFromService(target->service, uri, data, dataSize, contentType);
    free(uri);
    if(json == NULL)
    {
        return NULL;
    }
    return createRedfishPayload(json, target->service);
}

redfishPayload* postPayload(redfishPayload* target, redfishPayload* payload)
{
    char* content;
    redfishPayload* ret;

    if(!target || !payload)
    {
        return NULL;
    }

    if(!json_is_object(payload->json))
    {
        return NULL;
    }
    content = payloadToString(payload, false);
    ret = postContentToPayload(target, content, strlen(content), NULL);
    free(content);
    return ret;
}

bool deletePayload(redfishPayload* payload)
{
    json_t* json;
    char* uri;
    bool ret;

    if(!payload)
    {
        return false;
    }

    json = json_object_get(payload->json, "@odata.id");
    if(json == NULL)
    {
        return false;
    }
    uri = strdup(json_string_value(json));

    ret = deleteUriFromService(payload->service, uri);
    free(uri);
    return ret;
}

void cleanupPayload(redfishPayload* payload)
{
    if(!payload)
    {
        return;
    }
    json_decref(payload->json);
    //Don't free payload->service, let the caller handle cleaning up the service
    free(payload);
}

char* payloadToString(redfishPayload* payload, bool prettyPrint)
{
    size_t flags = 0;
	if(!payload)
    {
        return NULL;
    }
    if(prettyPrint)
    {
        flags = JSON_INDENT(2);
    }
    return json_dumps(payload->json, flags);
}

bool getPayloadByNodeNameAsync(redfishPayload* payload, const char* nodeName, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    json_t* value;
    json_t* odataId;
    char* uri;
    bool ret;
    redfishPayload* retPayload;

    if(!payload || !nodeName)
    {
        return false;
    }

    value = json_object_get(payload->json, nodeName);
    if(value == NULL)
    {
        return false;
    }
    if(isOdataIdNode(value, &uri))
    {
        ret = getUriFromServiceAsync(payload->service, uri, options, callback, context);
        free(uri);
        return ret;
    }
    json_incref(value);
    if(json_is_string(value))
    {
        odataId = json_object();
        json_object_set(odataId, nodeName, value);
        json_decref(value);
        value = odataId;
    }
    retPayload = createRedfishPayload(value, payload->service);
    callback(true, 200, retPayload, context);
    return true;
}

bool getPayloadByIndexAsync(redfishPayload* payload, size_t index, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    json_t* value = NULL;
    char* uri;
    bool ret;
    redfishPayload* retPayload;

    if(!payload)
    {
        return false;
    }
    if(isPayloadCollection(payload))
    {
        //Members will always be local, don't worry about async for this...
        redfishPayload* members = getPayloadByNodeName(payload, "Members");
        ret = getPayloadByIndexAsync(members, index, options, callback, context);
        cleanupPayload(members);
        return ret;
    }
    if(json_is_array(payload->json))
    {
        value = json_array_get(payload->json, index);
    }
    else if(json_is_object(payload->json))
    {
        value = json_object_get_by_index(payload->json, index);
    }

    if(value == NULL)
    {
        return false;
    }
    if(isOdataIdNode(value, &uri))
    {
        ret = getUriFromServiceAsync(payload->service, uri, options, callback, context);
        free(uri);
        return ret;
    }
    retPayload = createRedfishPayload(value, payload->service);
    callback(true, 200, retPayload, context);
    return true;
}

typedef struct
{
    redfishAsyncCallback callback;
    void* originalContext;
    redPathNode* redpath;
    redfishAsyncOptions* options;
} redpathAsyncContext;

void gotNextRedPath(bool success, unsigned short httpCode, redfishPayload* payload, void* context)
{
    redpathAsyncContext* myContext = (redpathAsyncContext*)context;
    bool ret;

    if(success == false || httpCode >= 400 || myContext->redpath->next == NULL)
    {
        myContext->callback(success, httpCode, payload, myContext->originalContext);
        cleanupRedPath(myContext->redpath);
        free(context);
        return;
    }
    ret = getPayloadForPathAsync(payload, myContext->redpath->next, myContext->options, myContext->callback, myContext->originalContext);
    cleanupPayload(payload);
    if(ret == false)
    {
        myContext->callback(ret, 0xFFFF, NULL, myContext->originalContext);
        cleanupRedPath(myContext->redpath); 
    }
    else
    {
        //Free just this redpath node...
        myContext->redpath->next = NULL;
        cleanupRedPath(myContext->redpath);
    }
    free(context);
}

bool getPayloadForPathAsync(redfishPayload* payload, redPathNode* redpath, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    redpathAsyncContext* myContext;
    bool ret;

    if(!payload || !redpath)
    {
        return NULL;
    }

    myContext = malloc(sizeof(redpathAsyncContext));
    myContext->callback = callback;
    myContext->originalContext = context;
    myContext->redpath = redpath;
    myContext->options = options;

    if(redpath->nodeName)
    {
        ret = getPayloadByNodeNameAsync(payload, redpath->nodeName, options, gotNextRedPath, myContext);
    }
    else if(redpath->isIndex)
    {
        ret = getPayloadByIndexAsync(payload, redpath->index, options, gotNextRedPath, myContext);
    }
    else if(redpath->op)
    {
        ret = getOpResultAsync(payload, redpath->propName, redpath->op, redpath->value, options, gotNextRedPath, myContext);
    }
    else
    {
        REDFISH_DEBUG_ERR_PRINT("%s: Don't know how to handle redpath with no nodename, no index, and no operation\n", __FUNCTION__);
        ret = false;
    }
    if(ret == false)
    {
        free(myContext);
    }
    return ret; 
}

static redfishPayload* getOpResult(redfishPayload* payload, const char* propName, const char* op, const char* value)
{
    const char* propStr;
    json_t* stringProp;
    bool ret = false;
    redfishPayload* prop;
    long long intVal, intPropVal;

    if(isPayloadCollection(payload))
    {
        return collectionEvalOp(payload, propName, op, value);
    }
    if(isPayloadArray(payload))
    {
        return arrayEvalOp(payload, propName, op, value);
    }

    prop = getPayloadByNodeName(payload, propName);
    if(prop == NULL)
    {
        return NULL;
    }
    stringProp = prop->json;
    switch(json_typeof(prop->json))
    {
        case JSON_OBJECT:
            stringProp = json_object_get(prop->json, propName);
        case JSON_STRING:
            if(strcmp(op, "=") == 0)
            {
                propStr = json_string_value(stringProp);
                if(propStr == NULL)
                {
                    cleanupPayload(prop);
                    return NULL;
                }
                ret = (strcmp(propStr, value) == 0);
            }
            break;
        case JSON_TRUE:
            if(strcmp(op, "=") == 0)
            {
                ret = (strcmp(value, "true") == 0);
            }
            break;
        case JSON_FALSE:
            if(strcmp(op, "=") == 0)
            {
                ret = (strcmp(value, "false") == 0);
            }
            break;
        case JSON_INTEGER:
            intPropVal = json_integer_value(prop->json);
            intVal = strtoll(value, NULL, 0);
            if(strcmp(op, "=") == 0)
            {
                ret = (intPropVal == intVal);
            }
            else if(strcmp(op, "<") == 0)
            {
                ret = (intPropVal < intVal);
            }
            else if(strcmp(op, ">") == 0)
            {
                ret = (intPropVal > intVal);
            }
            else if(strcmp(op, "<=") == 0)
            {
                ret = (intPropVal <= intVal);
            }
            else if(strcmp(op, ">=") == 0)
            {
                ret = (intPropVal >= intVal);
            }
            break;
        default:
            break;
    }
    cleanupPayload(prop);
    if(ret)
    {
        return payload;
    }
    else
    {
        return NULL;
    }
}

typedef struct
{
    redfishAsyncCallback callback;
    void* originalContext;
    redfishAsyncOptions* options;
    redfishPayload* payload;
    char* propName;
    char* op;
    char* value;
    size_t count;
    size_t left;
    size_t validCount;
    redfishPayload** payloads;
} redpathAsyncOpContext;

static void opGotPayloadByNodeNameAsync(bool success, unsigned short httpCode, redfishPayload* payload, void* context)
{
    redpathAsyncOpContext* myContext = (redpathAsyncOpContext*)context;
    json_t* stringProp;
    bool ret = false;
    const char* propStr;
    long long intVal, intPropVal;

    if(success == false || httpCode >= 400 || payload == NULL)
    {
        myContext->callback(success, httpCode, payload, myContext->originalContext);
        free(myContext->op);
        free(myContext->value);
        free(myContext);
        return;
    }
    stringProp = payload->json;
    switch(json_typeof(payload->json))
    {
        case JSON_OBJECT:
            stringProp = json_object_get(payload->json, myContext->propName);
        case JSON_STRING:
            if(strcmp(myContext->op, "=") == 0)
            {
                propStr = json_string_value(stringProp);
                if(propStr == NULL)
                {
                    cleanupPayload(payload);
                    myContext->callback(false, 0xFFFF, NULL, myContext->originalContext);
                    free(myContext->propName);
                    free(myContext->op);
                    free(myContext->value);
                    free(myContext);
                    return;
                }
                ret = (strcmp(propStr, myContext->value) == 0);
            }
            break;
        case JSON_TRUE:
            if(strcmp(myContext->op, "=") == 0)
            {
                ret = (strcmp(myContext->value, "true") == 0);
            }
            break;
        case JSON_FALSE:
            if(strcmp(myContext->op, "=") == 0)
            {
                ret = (strcmp(myContext->value, "false") == 0);
            }
            break;
        case JSON_INTEGER:
            intPropVal = json_integer_value(payload->json);
            intVal = strtoll(myContext->value, NULL, 0);
            if(strcmp(myContext->op, "=") == 0)
            {
                ret = (intPropVal == intVal);
            }
            else if(strcmp(myContext->op, "<") == 0)
            {
                ret = (intPropVal < intVal);
            }
            else if(strcmp(myContext->op, ">") == 0)
            {
                ret = (intPropVal > intVal);
            }
            else if(strcmp(myContext->op, "<=") == 0)
            {
                ret = (intPropVal <= intVal);
            }
            else if(strcmp(myContext->op, ">=") == 0)
            {
                ret = (intPropVal >= intVal);
            }
            break;
        default:
            break;
    }
    cleanupPayload(payload);
    if(ret)
    {
        myContext->callback(ret, 200, myContext->payload, myContext->originalContext);
    }
    else
    {
        myContext->callback(ret, 0xFFFF, NULL, myContext->originalContext);
    }
    free(myContext->propName);
    free(myContext->op);
    free(myContext->value);
    free(myContext);
}

static bool getOpResultAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    bool ret;
    redpathAsyncOpContext* myContext;

    if(isPayloadCollection(payload))
    {
        return collectionEvalOpAsync(payload, propName, op, value, options, callback, context);
    }
    if(isPayloadArray(payload))
    {
        return arrayEvalOpAsync(payload, propName, op, value, options, callback, context);
    }
    myContext = malloc(sizeof(redpathAsyncOpContext));
    if(myContext == NULL)
    {
        return false;
    }
    myContext->callback = callback;
    myContext->originalContext = context;
    myContext->options = options;
    myContext->payload = payload;
    myContext->propName = strdup(propName);
    myContext->op = strdup(op);
    myContext->value = strdup(value);
    ret = getPayloadByNodeNameAsync(payload, propName, options, opGotPayloadByNodeNameAsync, myContext);
    if(ret == false)
    {
        free(myContext);
    }
    return ret;
}

static redfishPayload* collectionEvalOp(redfishPayload* payload, const char* propName, const char* op, const char* value)
{
    redfishPayload* ret;
    redfishPayload* tmp;
    redfishPayload* members;
    redfishPayload** valid;
    size_t validMax;
    size_t validCount = 0;
    size_t i;

    validMax = getCollectionSize(payload);
    if(validMax == 0)
    {
        return NULL;
    }

    valid = (redfishPayload**)calloc(validMax, sizeof(redfishPayload*));
    if(valid == NULL)
    {
        return NULL;
    }
    /*Technically getPayloadByIndex would do this, but this optimizes things*/
    members = getPayloadByNodeName(payload, "Members");
    for(i = 0; i < validMax; i++)
    {
        tmp = getPayloadByIndex(members, i);
        valid[validCount] = getOpResult(tmp, propName, op, value);
        if(valid[validCount] != NULL)
        {
            validCount++;
        }
        else
        {
            cleanupPayload(tmp);
        }
    }
    cleanupPayload(members);
    if(validCount == 0)
    {
        free(valid);
        return NULL;
    }
    if(validCount == 1)
    {
        ret = valid[0];
        free(valid);
        return ret;
    }
    else
    {
        ret = createCollection(payload->service, validCount, valid);
        free(valid);
        return ret;
    }
}

static void opFinishByIndexTransaction(redpathAsyncOpContext* myContext)
{
    redfishPayload* returnValue;

    if(myContext->left != 0)
    {
        return;
    }

    if(myContext->validCount == 0)
    {
        returnValue = NULL;
    }
    else if(myContext->validCount == 1)
    {
        returnValue = myContext->payloads[0];
    }
    else
    {
        returnValue = createCollection(myContext->payloads[0]->service, myContext->validCount, myContext->payloads);
    }

    myContext->callback(true, 200, returnValue, myContext->originalContext);
    free(myContext->propName);
    free(myContext->op);
    free(myContext->value);
    free(myContext->payloads);
    free(myContext);
}

static void opGotResultAsync(bool success, unsigned short httpCode, redfishPayload* payload, void* context)
{
    redpathAsyncOpContext* myContext = (redpathAsyncOpContext*)context;

    if(success == true && httpCode < 300 && payload != NULL)
    {
        myContext->payloads[myContext->validCount++] = payload;
    }

    myContext->left--;
    if(myContext->left == 0)
    {
        opFinishByIndexTransaction(myContext);
    }
}

static void opGotPayloadByIndexAsync(bool success, unsigned short httpCode, redfishPayload* payload, void* context)
{
    redpathAsyncOpContext* myContext = (redpathAsyncOpContext*)context;
    bool ret;

    if(success == true && httpCode < 300 && payload != NULL)
    {
        ret = getOpResultAsync(payload, myContext->propName, myContext->op, myContext->value, myContext->options, opGotResultAsync, myContext);
        if(ret == true)
        {
            return;
        }
    }
    myContext->left--;
    if(myContext->left == 0)
    {
        opFinishByIndexTransaction(myContext);
    }
}

static bool collectionEvalOpAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    size_t max;
    size_t i;
    bool ret;
    bool anyWork = false;
    redpathAsyncOpContext* myContext;
    redfishPayload* members;

    max = getCollectionSize(payload);
    if(max == 0)
    {
        return false;
    }

    myContext = malloc(sizeof(redpathAsyncOpContext));
    if(myContext == NULL)
    {
        return false;
    }
    myContext->callback = callback;
    myContext->originalContext = context;
    myContext->options = options;
    myContext->propName = strdup(propName);
    myContext->op = strdup(op);
    myContext->value = strdup(value);
    myContext->count = max;
    myContext->left = max;
    myContext->validCount = 0;
    myContext->payloads = calloc(sizeof(redfishPayload*), max);
    /*Technically getPayloadByIndex would do this, but this optimizes things*/
    members = getPayloadByNodeName(payload, "Members");
    for(i = 0; i < max; i++)
    {
        ret = getPayloadByIndexAsync(members, i, options, opGotPayloadByIndexAsync, myContext);
        if(ret == false)
        {
            myContext->left--;
        }
        else
        {
            anyWork = true;
        }
    }
    cleanupPayload(members);
    if(anyWork == false)
    {
        free(myContext->propName);
        free(myContext->op);
        free(myContext->value);
        free(myContext->payloads);
        free(myContext);
    }
    return anyWork;
}

static redfishPayload* arrayEvalOp(redfishPayload* payload, const char* propName, const char* op, const char* value)
{
    redfishPayload* ret;
    redfishPayload* tmp;
    redfishPayload** valid;
    size_t validMax;
    size_t validCount = 0;
    size_t i;

    validMax = json_array_size(payload->json);
    if(validMax == 0)
    {
        return NULL;
    }

    valid = (redfishPayload**)calloc(validMax, sizeof(redfishPayload*));
    if(valid == NULL)
    {
        return NULL;
    }
    for(i = 0; i < validMax; i++)
    {
        tmp = getPayloadByIndex(payload, i);
        valid[validCount] = getOpResult(tmp, propName, op, value);
        if(valid[validCount] != NULL)
        {
            validCount++;
        }
        else
        {
            cleanupPayload(tmp);
        }
    }
    if(validCount == 0)
    {
        free(valid);
        return NULL;
    }
    if(validCount == 1)
    {
        ret = valid[0];
        free(valid);
        return ret;
    }
    else
    {
        ret = createCollection(payload->service, validCount, valid);
        free(valid);
        return ret;
    }
}

static bool arrayEvalOpAsync(redfishPayload* payload, const char* propName, const char* op, const char* value, redfishAsyncOptions* options, redfishAsyncCallback callback, void* context)
{
    size_t max;
    size_t i;
    bool ret;
    bool anyWork = false;
    redpathAsyncOpContext* myContext;

    max = json_array_size(payload->json);
    if(max == 0)
    {
        return false;
    }

    myContext = malloc(sizeof(redpathAsyncOpContext));
    if(myContext == NULL)
    {
        return false;
    }
    myContext->callback = callback;
    myContext->originalContext = context;
    myContext->options = options;
    myContext->propName = strdup(propName);
    myContext->op = strdup(op);
    myContext->value = strdup(value);
    myContext->count = max;
    myContext->left = max;
    myContext->validCount = 0;
    myContext->payloads = calloc(sizeof(redfishPayload*), max);
    for(i = 0; i < max; i++)
    {
        ret = getPayloadByIndexAsync(payload, i, options, opGotPayloadByIndexAsync, myContext);
        if(ret == false)
        {
            myContext->left--;
        }
        else
        {
            anyWork = true;
        }
    }
    if(anyWork == false)
    {
        free(myContext->propName);
        free(myContext->op);
        free(myContext->value);
        free(myContext->payloads);
        free(myContext);
    }
    return anyWork;
}

static redfishPayload* createCollection(redfishService* service, size_t count, redfishPayload** payloads)
{
    redfishPayload* ret;
    json_t* collectionJson = json_object();
    json_t* jcount = json_integer((json_int_t)count);
    json_t* members = json_array();
    size_t i;

    if(!collectionJson)
    {
        return NULL;
    }
    if(!members)
    {
        json_decref(collectionJson);
        return NULL;
    }
    json_object_set(collectionJson, "Members@odata.count", jcount);
    json_decref(jcount);
    for(i = 0; i < count; i++)
    {
        json_array_append(members, payloads[i]->json);
        cleanupPayload(payloads[i]);
    }
    json_object_set(collectionJson, "Members", members);
    json_decref(members);

    ret = createRedfishPayload(collectionJson, service);
    return ret;
}

static json_t* json_object_get_by_index(json_t* json, size_t index)
{
    void* iter;
    size_t i;

    iter = json_object_iter(json);
    for(i = 0; i < index; i++)
    {
        iter = json_object_iter_next(json, iter);
        if(iter == NULL) break;
    }
    if(iter == NULL)
    {
        return NULL;
    }
    return json_object_iter_value(iter);
}

static bool isOdataIdNode(json_t* json, char** uriPtr)
{
    json_t* odataId;
    const char* uri;

    //Must be an object with exactly one entry..
    if(json_object_size(json) != 1)
    {
        return false;
    }
    //Must contain an entry called "@odata.id"
    odataId = json_object_get(json, "@odata.id");
    if(odataId == NULL)
    {
        return false;
    }
    uri = json_string_value(odataId);
    if(uri == NULL)
    {
        return false;
    }
    *uriPtr = strdup(uri);
    return true;
}
/* vim: set tabstop=4 shiftwidth=4 expandtab: */