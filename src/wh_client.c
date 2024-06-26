/*
 * Copyright (C) 2024 wolfSSL Inc.
 *
 * This file is part of wolfHSM.
 *
 * wolfHSM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfHSM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfHSM.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * src/wh_client.c
 */

/* System libraries */
#include <stdint.h>
#include <stdlib.h>  /* For NULL */
#include <string.h>  /* For memset, memcpy */

/* Common WolfHSM types and defines shared with the server */
#include "wolfhsm/wh_common.h"
#include "wolfhsm/wh_error.h"

/* Components */
#include "wolfhsm/wh_comm.h"

#ifndef WOLFHSM_NO_CRYPTO
#include "wolfhsm/wh_cryptocb.h"
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/error-crypt.h"
#include "wolfssl/wolfcrypt/wc_port.h"
#include "wolfssl/wolfcrypt/cryptocb.h"
#include "wolfssl/wolfcrypt/curve25519.h"
#include "wolfssl/wolfcrypt/rsa.h"
#include "wolfssl/wolfcrypt/ecc.h"
#endif

/* Message definitions */
#include "wolfhsm/wh_message.h"
#include "wolfhsm/wh_message_comm.h"
#include "wolfhsm/wh_message_customcb.h"

#include "wolfhsm/wh_packet.h"
#include "wolfhsm/wh_client.h"

int wh_Client_Init(whClientContext* c, const whClientConfig* config)
{
    int rc = 0;
    if((c == NULL) || (config == NULL)) {
        return WH_ERROR_BADARGS;
    }

    memset(c, 0, sizeof(*c));

    if (    ((rc = wh_CommClient_Init(c->comm, config->comm)) == 0) &&
#ifndef WOLFHSM_NO_CRYPTO
            ((rc = wolfCrypt_Init()) == 0) &&
            ((rc = wc_CryptoCb_RegisterDevice(WOLFHSM_DEV_ID, wolfHSM_CryptoCb, c)) == 0) &&
#endif  /* WOLFHSM_NO_CRYPTO */
            1) {
        /* All good */
    }
    if (rc != 0) {
        wh_Client_Cleanup(c);
    }
    return rc;
}

int wh_Client_Cleanup(whClientContext* c)
{
    if (c ==NULL) {
        return WH_ERROR_BADARGS;
    }

    (void)wh_CommClient_Cleanup(c->comm);
#ifndef WOLFHSM_NO_CRYPTO
    (void)wolfCrypt_Cleanup();
#endif  /* WOLFHSM_NO_CRYPTO */

    memset(c, 0, sizeof(*c));
    return 0;
}

int wh_Client_SendRequest(whClientContext* c,
        uint16_t group, uint16_t action,
        uint16_t data_size, const void* data)
{
    uint16_t req_id = 0;
    uint16_t kind = WH_MESSAGE_KIND(group, action);
    int rc = 0;

    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }
    rc = wh_CommClient_SendRequest(c->comm, WH_COMM_MAGIC_NATIVE, kind, &req_id,
        data_size, data);
    if (rc == 0) {
        c->last_req_kind = kind;
        c->last_req_id = req_id;
    }
    return rc;
}

int wh_Client_RecvResponse(whClientContext *c,
        uint16_t *out_group, uint16_t *out_action,
        uint16_t *out_size, void* data)
{
    int rc = 0;
    uint16_t resp_magic = 0;
    uint16_t resp_kind = 0;
    uint16_t resp_id = 0;
    uint16_t resp_size = 0;

    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc = wh_CommClient_RecvResponse(c->comm,
                &resp_magic, &resp_kind, &resp_id,
                &resp_size, data);
    if (rc == 0) {
        /* Validate response */
        if (    (resp_magic != WH_COMM_MAGIC_NATIVE) ||
                (resp_kind != c->last_req_kind) ||
                (resp_id != c->last_req_id) ){
            /* Invalid or unexpected message */
            rc = WH_ERROR_ABORTED;
        } else {
            /* Valid and expected message. Set outputs */
            if (out_group != NULL) {
                *out_group = WH_MESSAGE_GROUP(resp_kind);
            }
            if (out_action != NULL) {
                *out_action = WH_MESSAGE_ACTION(resp_kind);
            }
            if (out_size != NULL) {
                *out_size = resp_size;
            }
        }
    }
    return rc;
}

int wh_Client_CommInitRequest(whClientContext* c)
{
    whMessageCommInitRequest msg = {0};

    if (c == NULL) {
       return WH_ERROR_BADARGS;
   }

   /* Populate the message.*/
   msg.client_id = c->comm->client_id;

   return wh_Client_SendRequest(c,
           WH_MESSAGE_GROUP_COMM, WH_MESSAGE_COMM_ACTION_INIT,
           sizeof(msg), &msg);
}

int wh_Client_CommInitResponse(whClientContext* c,
                                uint32_t *out_clientid,
                                uint32_t *out_serverid)
{
    int rc = 0;
    whMessageCommInitResponse msg = {0};
    uint16_t resp_group = 0;
    uint16_t resp_action = 0;
    uint16_t resp_size = 0;

    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc = wh_Client_RecvResponse(c,
            &resp_group, &resp_action,
            &resp_size, &msg);
    if (rc == 0) {
        /* Validate response */
        if (    (resp_group != WH_MESSAGE_GROUP_COMM) ||
                (resp_action != WH_MESSAGE_COMM_ACTION_INIT) ||
                (resp_size != sizeof(msg)) ){
            /* Invalid message */
            rc = WH_ERROR_ABORTED;
        } else {
            /* Valid message */
            if (out_clientid != NULL) {
                *out_clientid = msg.client_id;
            }
            if (out_serverid != NULL) {
                *out_serverid = msg.server_id;
            }
        }
    }
    return rc;
}

int wh_Client_CommInit(whClientContext* c,
                        uint32_t *out_clientid,
                        uint32_t *out_serverid)
{
    int rc = 0;
    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }
    do {
        rc = wh_Client_CommInitRequest(c);
    } while (rc == WH_ERROR_NOTREADY);

    if (rc == 0) {
        do {
            rc = wh_Client_CommInitResponse(c, out_clientid, out_serverid);
        } while (rc == WH_ERROR_NOTREADY);
    }
    return rc;
}

int wh_Client_CommCloseRequest(whClientContext* c)
{
    if (c == NULL) {
       return WH_ERROR_BADARGS;
   }

   return wh_Client_SendRequest(c,
           WH_MESSAGE_GROUP_COMM, WH_MESSAGE_COMM_ACTION_CLOSE,
           0, NULL);
}

int wh_Client_CommCloseResponse(whClientContext* c)
{
    int rc = 0;
    uint16_t resp_group = 0;
    uint16_t resp_action = 0;
    uint16_t resp_size = 0;

    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc = wh_Client_RecvResponse(c,
            &resp_group, &resp_action,
            &resp_size, NULL);
    if (rc == 0) {
        /* Validate response */
        if (    (resp_group != WH_MESSAGE_GROUP_COMM) ||
                (resp_action != WH_MESSAGE_COMM_ACTION_CLOSE) ){
            /* Invalid message */
            rc = WH_ERROR_ABORTED;
        } else {
            /* Valid message. Server is now disconnected */
            /* TODO: Cleanup the client */
        }
    }
    return rc;
}

int wh_Client_CommClose(whClientContext* c)
{
    int rc = 0;
    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }
    do {
        rc = wh_Client_CommCloseRequest(c);
    } while (rc == WH_ERROR_NOTREADY);

    if (rc == 0) {
        do {
            rc = wh_Client_CommCloseResponse(c);
        } while (rc == WH_ERROR_NOTREADY);
    }
    return rc;
}

int wh_Client_EchoRequest(whClientContext* c, uint16_t size, const void* data)
{
    whMessageCommLenData msg = {0};

    if (    (c == NULL) ||
            ((size > 0) && (data == NULL)) ) {
        return WH_ERROR_BADARGS;
    }

    /* Populate the message.  Ok to truncate here */
    if (size > sizeof(msg.data)) {
        size = sizeof(msg.data);
    }
    msg.len = size;
    memcpy(msg.data, data, size);

    return wh_Client_SendRequest(c,
            WH_MESSAGE_GROUP_COMM, WH_MESSAGE_COMM_ACTION_ECHO,
            sizeof(msg), &msg);
}

int wh_Client_EchoResponse(whClientContext* c, uint16_t *out_size, void* data)
{
    int rc = 0;
    whMessageCommLenData msg = {0};
    uint16_t resp_group = 0;
    uint16_t resp_action = 0;
    uint16_t resp_size = 0;

    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc = wh_Client_RecvResponse(c,
            &resp_group, &resp_action,
            &resp_size, &msg);
    if (rc == 0) {
        /* Validate response */
        if (    (resp_group != WH_MESSAGE_GROUP_COMM) ||
                (resp_action != WH_MESSAGE_COMM_ACTION_ECHO) ||
                (resp_size != sizeof(msg)) ){
            /* Invalid message */
            rc = WH_ERROR_ABORTED;
        } else {
            /* Valid message */
            if (msg.len > sizeof(msg.data)) {
                /* Bad incoming msg len.  Truncate */
                msg.len = sizeof(msg.data);
            }

            if (out_size != NULL) {
                *out_size = msg.len;
            }
            if (data != NULL) {
                memcpy(data, msg.data, msg.len);
            }
        }
    }
    return rc;
}

int wh_Client_Echo(whClientContext* c, uint16_t snd_len, const void* snd_data,
        uint16_t *out_rcv_len, void* rcv_data)
{
    int rc = 0;
    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }
    do {
        rc = wh_Client_EchoRequest(c, snd_len, snd_data);
    } while (rc == WH_ERROR_NOTREADY);

    if (rc == 0) {
        do {
            rc = wh_Client_EchoResponse(c, out_rcv_len, rcv_data);
        } while (rc == WH_ERROR_NOTREADY);
    }
    return rc;
}

int wh_Client_CustomCbRequest(whClientContext* c, const whMessageCustomCb_Request* req)
{
    if (NULL == c || req == NULL || req->id >= WH_CUSTOM_CB_NUM_CALLBACKS) {
        return WH_ERROR_BADARGS;
    }

    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_CUSTOM, req->id,
                                 sizeof(*req), req);
}

int wh_Client_CustomCbResponse(whClientContext*          c,
                             whMessageCustomCb_Response* outResp)
{
    whMessageCustomCb_Response resp;
    uint16_t                 resp_group  = 0;
    uint16_t                 resp_action = 0;
    uint16_t                 resp_size   = 0;
    int32_t                  rc          = 0;

    if (NULL == c || outResp == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc =
        wh_Client_RecvResponse(c, &resp_group, &resp_action, &resp_size, &resp);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    if (resp_size != sizeof(resp) || resp_group != WH_MESSAGE_GROUP_CUSTOM ||
        resp_action >= WH_CUSTOM_CB_NUM_CALLBACKS) {
        /* message invalid */
        return WH_ERROR_ABORTED;
    }

    memcpy(outResp, &resp, sizeof(resp));

    return WH_ERROR_OK;
}

int wh_Client_CustomCheckRegisteredRequest(whClientContext* c, uint32_t id)
{
    whMessageCustomCb_Request req = {0};

    if (c == NULL || id >= WH_CUSTOM_CB_NUM_CALLBACKS) {
        return WH_ERROR_BADARGS;
    }

    req.id = id;
    req.type = WH_MESSAGE_CUSTOM_CB_TYPE_QUERY;

    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_CUSTOM, req.id,
                                 sizeof(req), &req);
}


int wh_Client_CustomCbCheckRegisteredResponse(whClientContext* c, uint16_t* outId, int* responseError)
{
    int rc = 0;
    whMessageCustomCb_Response resp = {0};

    if (c == NULL || outId == NULL || responseError == NULL) {
        return WH_ERROR_BADARGS;
    }

    rc = wh_Client_CustomCbResponse(c, &resp);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    if (resp.type != WH_MESSAGE_CUSTOM_CB_TYPE_QUERY) {
        /* message invalid */
        return WH_ERROR_ABORTED;
    }

    if (resp.err != WH_ERROR_OK && resp.err != WH_ERROR_NOHANDLER) {
        /* error codes that aren't related to the query should be fatal */
        return WH_ERROR_ABORTED;
    }

    *outId = resp.id;
    *responseError = resp.err;

    return WH_ERROR_OK;
}


int wh_Client_CustomCbCheckRegistered(whClientContext* c, uint16_t id, int* responseError)
{
    int rc = 0;

    if (NULL == c || NULL == responseError || id >= WH_CUSTOM_CB_NUM_CALLBACKS) {
        return WH_ERROR_BADARGS;
    }

    do {
        rc = wh_Client_CustomCheckRegisteredRequest(c, id);
    } while (rc == WH_ERROR_NOTREADY);

    if (rc == WH_ERROR_OK) {
        do {
            rc = wh_Client_CustomCbCheckRegisteredResponse(c, &id, responseError);
        } while (rc == WH_ERROR_NOTREADY);
    }

    return rc;
}


#ifndef WOLFHSM_NO_CRYPTO

int wh_Client_KeyCacheRequest_ex(whClientContext* c, uint32_t flags,
    uint8_t* label, uint32_t labelSz, uint8_t* in, uint32_t inSz,
    uint16_t keyId)
{
    uint8_t rawPacket[WH_COMM_MTU] = {0};
    whPacket* packet = (whPacket*)rawPacket;
    uint8_t* packIn = (uint8_t*)(&packet->keyCacheReq + 1);
    if (c == NULL || in == NULL || inSz == 0)
        return WH_ERROR_BADARGS;
    packet->keyCacheReq.id = keyId;
    packet->keyCacheReq.flags = flags;
    packet->keyCacheReq.sz = inSz;
    if (label == NULL)
        packet->keyCacheReq.labelSz = 0;
    else {
        packet->keyCacheReq.labelSz = labelSz;
        /* write label */
        if (labelSz > WOLFHSM_NVM_LABEL_LEN)
            XMEMCPY(packet->keyCacheReq.label, label, WOLFHSM_NVM_LABEL_LEN);
        else
            XMEMCPY(packet->keyCacheReq.label, label, labelSz);
    }
    /* write in */
    XMEMCPY(packIn, in, inSz);
    /* write request */
    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_KEY, WH_KEY_CACHE,
            WOLFHSM_PACKET_STUB_SIZE + sizeof(packet->keyCacheReq) + inSz,
            (uint8_t*)packet);
}

int wh_Client_KeyCacheRequest(whClientContext* c, uint32_t flags,
    uint8_t* label, uint32_t labelSz, uint8_t* in, uint32_t inSz)
{
    return wh_Client_KeyCacheRequest_ex(c, flags, label, labelSz, in, inSz,
        WOLFHSM_KEYID_ERASED);
}

int wh_Client_KeyCacheResponse(whClientContext* c, uint16_t* keyId)
{
    uint16_t group;
    uint16_t action;
    uint16_t size;
    int ret;
    whPacket packet[1] = {0};
    if (c == NULL || keyId == NULL)
        return WH_ERROR_BADARGS;
    ret = wh_Client_RecvResponse(c, &group, &action, &size, (uint8_t*)packet);
    if (ret == 0) {
        if (packet->rc != 0)
            ret = packet->rc;
        else
            *keyId = packet->keyCacheRes.id;
    }
    return ret;
}

int wh_Client_KeyCache(whClientContext* c, uint32_t flags,
    uint8_t* label, uint32_t labelSz, uint8_t* in, uint32_t inSz,
    uint16_t* keyId)
{
    int ret;
    ret = wh_Client_KeyCacheRequest_ex(c, flags, label, labelSz, in, inSz,
        *keyId);
    if (ret == 0) {
        do {
            ret = wh_Client_KeyCacheResponse(c, keyId);
        } while (ret == WH_ERROR_NOTREADY);
    }
    return ret;
}

int wh_Client_KeyEvictRequest(whClientContext* c, uint16_t keyId)
{
    whPacket packet[1] = {0};
    if (c == NULL || keyId == WOLFHSM_KEYID_ERASED)
        return WH_ERROR_BADARGS;
    /* set the keyId */
    packet->keyEvictReq.id = keyId;
    /* write request */
    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_KEY, WH_KEY_EVICT,
            WOLFHSM_PACKET_STUB_SIZE + sizeof(packet->keyEvictReq),
            (uint8_t*)packet);
}

int wh_Client_KeyEvictResponse(whClientContext* c)
{
    uint16_t group;
    uint16_t action;
    uint16_t size;
    int ret;
    whPacket packet[1] = {0};
    if (c == NULL)
        return WH_ERROR_BADARGS;
    ret = wh_Client_RecvResponse(c, &group, &action, &size, (uint8_t*)packet);
    if (ret == 0) {
        if (packet->rc != 0)
            ret = packet->rc;
    }
    return ret;
}

int wh_Client_KeyEvict(whClientContext* c, uint16_t keyId)
{
    int ret;
    ret = wh_Client_KeyEvictRequest(c, keyId);
    if (ret == 0) {
        do {
            ret = wh_Client_KeyEvictResponse(c);
        } while (ret == WH_ERROR_NOTREADY);
    }
    return ret;
}

int wh_Client_KeyExportRequest(whClientContext* c, uint16_t keyId)
{
    whPacket packet[1] = {0};
    if (c == NULL || keyId == WOLFHSM_KEYID_ERASED)
        return WH_ERROR_BADARGS;
    /* set keyId */
    packet->keyExportReq.id = keyId;
    /* write request */
    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_KEY, WH_KEY_EXPORT,
            WOLFHSM_PACKET_STUB_SIZE + sizeof(packet->keyExportReq),
            (uint8_t*)packet);
}

int wh_Client_KeyExportResponse(whClientContext* c, uint8_t* label,
    uint32_t labelSz, uint8_t* out, uint32_t* outSz)
{
    uint16_t group;
    uint16_t action;
    uint16_t size;
    int ret;
    uint8_t rawPacket[WH_COMM_MTU] = {0};
    whPacket* packet = (whPacket*)rawPacket;
    uint8_t* packOut = (uint8_t*)(&packet->keyExportRes + 1);
    if (c == NULL || outSz == NULL)
        return WH_ERROR_BADARGS;
    ret = wh_Client_RecvResponse(c, &group, &action, &size, rawPacket);
    if (ret == 0) {
        if (packet->rc != 0)
            ret = packet->rc;
        else  {
            if (out == NULL) {
                *outSz = packet->keyExportRes.len;
            }
            else if (*outSz < packet->keyExportRes.len) {
                ret = WH_ERROR_ABORTED;
            }
            else {
                XMEMCPY(out, packOut, packet->keyExportRes.len);
                *outSz = packet->keyExportRes.len;
            }
            if (label != NULL) {
                if (labelSz > sizeof(packet->keyExportRes.label)) {
                    XMEMCPY(label, packet->keyExportRes.label,
                        WOLFHSM_NVM_LABEL_LEN);
                }
                else
                    XMEMCPY(label, packet->keyExportRes.label, labelSz);
            }
        }
    }
    return ret;
}

int wh_Client_KeyExport(whClientContext* c, uint16_t keyId,
    uint8_t* label, uint32_t labelSz, uint8_t* out, uint32_t* outSz)
{
    int ret;
    ret = wh_Client_KeyExportRequest(c, keyId);
    if (ret == 0) {
        do {
            ret = wh_Client_KeyExportResponse(c, label, labelSz, out, outSz);
        } while (ret == WH_ERROR_NOTREADY);
    }
    return ret;
}

int wh_Client_KeyCommitRequest(whClientContext* c, whNvmId keyId)
{
    whPacket packet[1] = {0};
    if (c == NULL || keyId == WOLFHSM_KEYID_ERASED)
        return WH_ERROR_BADARGS;
    /* set keyId */
    packet->keyCommitReq.id = keyId;
    /* write request */
    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_KEY, WH_KEY_COMMIT,
            WOLFHSM_PACKET_STUB_SIZE + sizeof(packet->keyCommitReq),
            (uint8_t*)packet);
}

int wh_Client_KeyCommitResponse(whClientContext* c)
{
    uint16_t group;
    uint16_t action;
    uint16_t size;
    int ret;
    whPacket packet[1] = {0};
    if (c == NULL)
        return WH_ERROR_BADARGS;
    ret = wh_Client_RecvResponse(c, &group, &action, &size, (uint8_t*)packet);
    if (ret == 0) {
        if (packet->rc != 0)
            ret = packet->rc;
    }
    return ret;
}

int wh_Client_KeyCommit(whClientContext* c, whNvmId keyId)
{
    int ret;
    ret = wh_Client_KeyCommitRequest(c, keyId);
    if (ret == 0) {
        do {
            ret = wh_Client_KeyCommitResponse(c);
        } while (ret == WH_ERROR_NOTREADY);
    }
    return ret;
}

int wh_Client_KeyEraseRequest(whClientContext* c, whNvmId keyId)
{
    whPacket packet[1] = {0};
    if (c == NULL || keyId == WOLFHSM_KEYID_ERASED)
        return WH_ERROR_BADARGS;
    /* set keyId */
    packet->keyEraseReq.id = keyId;
    /* write request */
    return wh_Client_SendRequest(c, WH_MESSAGE_GROUP_KEY, WH_KEY_ERASE,
            WOLFHSM_PACKET_STUB_SIZE + sizeof(packet->keyEraseReq),
            (uint8_t*)packet);
}

int wh_Client_KeyEraseResponse(whClientContext* c)
{
    uint16_t group;
    uint16_t action;
    uint16_t size;
    int ret;
    whPacket packet[1] = {0};
    if (c == NULL)
        return WH_ERROR_BADARGS;
    ret = wh_Client_RecvResponse(c, &group, &action, &size, (uint8_t*)packet);
    if (ret == 0) {
        if (packet->rc != 0)
            ret = packet->rc;
    }
    return ret;
}

int wh_Client_KeyErase(whClientContext* c, whNvmId keyId)
{
    int ret;
    ret = wh_Client_KeyEraseRequest(c, keyId);
    if (ret == 0) {
        do {
            ret = wh_Client_KeyEraseResponse(c);
        } while (ret == WH_ERROR_NOTREADY);
    }
    return ret;
}

#ifdef HAVE_CURVE25519
void wh_Client_SetKeyCurve25519(curve25519_key* key, whNvmId keyId)
{
    key->devCtx = (void*)((intptr_t)keyId);
}
#endif

#ifndef NO_RSA
void wh_Client_SetKeyRsa(RsaKey* key, whNvmId keyId)
{
    key->devCtx = (void*)((intptr_t)keyId);
}
#endif

#ifndef NO_AES
void wh_Client_SetKeyAes(Aes* key, whNvmId keyId)
{
    key->devCtx = (void*)((intptr_t)keyId);
}
#endif
#endif  /* !WOLFHSM_NO_CRYPTO */
