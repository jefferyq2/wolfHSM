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
 * test/wh_test.c
 *
 */

#include <stdint.h>
#include <stdio.h>  /* For printf */
#include <string.h> /* For memset, memcpy */

#ifndef WOLFHSM_NO_CRYPTO

#include "wolfssl/wolfcrypt/settings.h"

#if defined(WH_CONFIG)
#include "wh_config.h"
#endif

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_message.h"
#include "wolfhsm/wh_server.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_transport_mem.h"

#include "wh_test_common.h"

#if defined(WH_CFG_TEST_POSIX)
#include <pthread.h> /* For pthread_create/cancel/join/_t */
#include "port/posix/posix_transport_tcp.h"
#include "port/posix/posix_flash_file.h"
#endif

#if defined(WH_CFG_TEST_POSIX)
#include <unistd.h> /* For sleep */
#include <pthread.h> /* For pthread_create/cancel/join/_t */
#include "port/posix/posix_transport_tcp.h"
#include "port/posix/posix_flash_file.h"
#endif

enum {
        REQ_SIZE = 32,
        RESP_SIZE = 64,
        BUFFER_SIZE = 4096,
    };


#define PLAINTEXT "mytextisbigplain"

int whTest_CryptoClientConfig(whClientConfig* config)
{
    whClientContext client[1] = {0};
    int ret = 0;
    int res = 0;
    /* wolfcrypt */
    WC_RNG rng[1];
    Aes aes[1];
    RsaKey rsa[1];
    ecc_key eccPrivate[1];
    ecc_key eccPublic[1];
    curve25519_key curve25519PrivateKey[1];
    curve25519_key curve25519PublicKey[1];
    uint32_t outLen;
    uint16_t keyId;
    uint8_t key[16];
    uint8_t keyEnd[16];
    uint8_t labelStart[WOLFHSM_NVM_LABEL_LEN];
    uint8_t labelEnd[WOLFHSM_NVM_LABEL_LEN];
    uint8_t iv[AES_IV_SIZE];
    char plainText[16];
    char cipherText[256];
    char finalText[256];
    uint8_t authIn[16];
    uint8_t authTag[16];
    uint8_t sharedOne[CURVE25519_KEYSIZE];
    uint8_t sharedTwo[CURVE25519_KEYSIZE];

    XMEMCPY(plainText, PLAINTEXT, sizeof(plainText));

    if (config == NULL) {
        return WH_ERROR_BADARGS;
    }

    WH_TEST_RETURN_ON_FAIL(wh_Client_Init(client, config));
    memset(labelStart, 0xff, sizeof(labelStart));

    /* test rng */
    if ((ret = wc_InitRng_ex(rng, NULL, WOLFHSM_DEV_ID)) != 0) {
        WH_ERROR_PRINT("Failed to wc_InitRng_ex %d\n", ret);
        goto exit;
    }
    if ((ret = wc_RNG_GenerateBlock(rng, key, sizeof(key))) != 0) {
        WH_ERROR_PRINT("Failed to wc_RNG_GenerateBlock %d\n", ret);
        goto exit;
    }
    if((ret = wc_RNG_GenerateBlock(rng, iv, sizeof(iv))) != 0) {
        printf("Failed to wc_RNG_GenerateBlock %d\n", ret);
        goto exit;
    }
    if((ret = wc_RNG_GenerateBlock(rng, authIn, sizeof(authIn))) != 0) {
        printf("Failed to wc_RNG_GenerateBlock %d\n", ret);
        goto exit;
    }
    printf("RNG SUCCESS\n");
    /* test cache/export */
    keyId = 0;
    if ((ret = wh_Client_KeyCache(client, 0, labelStart, sizeof(labelStart), key, sizeof(key), &keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyCache %d\n", ret);
        goto exit;
    }
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    if (ret == 0 && XMEMCMP(key, keyEnd, outLen) == 0 && XMEMCMP(labelStart, labelEnd, sizeof(labelStart)) == 0)
        printf("KEY CACHE/EXPORT SUCCESS\n");
    else {
        WH_ERROR_PRINT("KEY CACHE/EXPORT FAILED TO MATCH\n");
        goto exit;
    }
#ifndef WH_CFG_TEST_NO_CUSTOM_SERVERS
    /* WH_CFG_TEST_NO_CUSTOM_SERVERS protects the client test code that expects to
     * interop with the custom server (also defined in this file), so that this
     * test can be run against a standard server app
     *
     * TODO: This is a temporary bodge until we properly split tests into single
     * client and multi client */

    /* test cache with duplicate keyId for a different user */
    WH_TEST_RETURN_ON_FAIL(wh_Client_CommClose(client));
    client->comm->client_id = 2;
    XMEMSET(cipherText, 0xff, sizeof(cipherText));
    /* first check that evicting the other clients key fails */
    if ((ret = wh_Client_KeyEvict(client, keyId)) != WH_ERROR_NOTFOUND) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
    if ((ret = wh_Client_KeyCache(client, 0, labelStart, sizeof(labelStart), (uint8_t*)cipherText, sizeof(key), &keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyCache %d\n", ret);
        goto exit;
    }
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    if (ret != 0 || XMEMCMP(cipherText, keyEnd, outLen) != 0 || XMEMCMP(labelStart, labelEnd, sizeof(labelStart)) != 0) {
        WH_ERROR_PRINT("KEY CACHE/EXPORT FAILED TO MATCH\n");
        goto exit;
    }
    /* evict for this client */
    if ((ret = wh_Client_KeyEvict(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
    /* switch back and verify original key */
    WH_TEST_RETURN_ON_FAIL(wh_Client_CommClose(client));
    client->comm->client_id = 1;
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    if (ret == 0 && XMEMCMP(key, keyEnd, outLen) == 0 && XMEMCMP(labelStart, labelEnd, sizeof(labelStart)) == 0)
        printf("KEY USER CACHE MUTUAL EXCLUSION SUCCESS\n");
    else {
        WH_ERROR_PRINT("KEY CACHE/EXPORT FAILED TO MATCH\n");
        goto exit;
    }
#endif /* !WH_CFG_TEST_NO_CUSTOM_SERVERS */
    /* evict for original client */
    if ((ret = wh_Client_KeyEvict(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != WH_ERROR_NOTFOUND) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    /* test commit */
    keyId = 0;
    if ((ret = wh_Client_KeyCache(client, 0, labelStart, sizeof(labelStart), key, sizeof(key), &keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyCache %d\n", ret);
        goto exit;
    }
    if ((ret = wh_Client_KeyCommit(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyCommit %d\n", ret);
        goto exit;
    }
    if ((ret = wh_Client_KeyEvict(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    if (ret == 0 && XMEMCMP(key, keyEnd, outLen) == 0 && XMEMCMP(labelStart, labelEnd, sizeof(labelStart)) == 0)
        printf("KEY COMMIT/EXPORT SUCCESS\n");
    else {
        WH_ERROR_PRINT("KEY COMMIT/EXPORT FAILED TO MATCH\n");
        goto exit;
    }
    /* test erase */
    if ((ret = wh_Client_KeyErase(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyErase %d\n", ret);
        goto exit;
    }
    outLen = sizeof(keyEnd);
    if ((ret = wh_Client_KeyExport(client, keyId, labelEnd, sizeof(labelEnd), keyEnd, &outLen)) != WH_ERROR_NOTFOUND) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyExport %d\n", ret);
        goto exit;
    }
    printf("KEY ERASE SUCCESS\n");
    /* test aes CBC */
    if((ret = wc_AesInit(aes, NULL, WOLFHSM_DEV_ID)) != 0) {
        printf("Failed to wc_AesInit %d\n", ret);
        goto exit;
    }
#ifdef WOLFHSM_SYMMETRIC_INTERNAL
    keyId = 0;
    if ((ret = wh_Client_KeyCache(client, 0, labelStart, sizeof(labelStart), key, sizeof(key), &keyId)) != 0) {
        printf("Failed to wh_Client_KeyCache %d\n", ret);
        goto exit;
    }
    wh_Client_SetKeyAes(aes, keyId);
    if ((ret = wc_AesSetIV(aes, iv)) != 0) {
        printf("Failed to wc_AesSetIV %d\n", ret);
        goto exit;
    }
#else
    if ((ret = wc_AesSetKey(aes, key, AES_BLOCK_SIZE, iv, AES_ENCRYPTION)) != 0) {
        printf("Failed to wc_AesSetKey %d\n", ret);
        goto exit;
    }
#endif
    if ((ret = wc_AesCbcEncrypt(aes, (byte*)cipherText, (byte*)plainText, sizeof(plainText))) != 0) {
        printf("Failed to wc_AesCbcEncrypt %d\n", ret);
        goto exit;
    }
#ifndef WOLFHSM_SYMMETRIC_INTERNAL
    if ((ret = wc_AesSetKey(aes, key, AES_BLOCK_SIZE, iv, AES_DECRYPTION)) != 0) {
        printf("Failed to wc_AesSetKey %d\n", ret);
        goto exit;
    }
#endif
    if ((ret = wc_AesCbcDecrypt(aes, (byte*)finalText, (byte*)cipherText, sizeof(plainText))) != 0) {
        printf("Failed to wc_AesCbcDecrypt %d\n", ret);
        goto exit;
    }
#ifdef WOLFHSM_SYMMETRIC_INTERNAL
    if((ret = wh_Client_KeyEvict(client, keyId)) != 0) {
        printf("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
#endif
    if (memcmp(plainText, finalText, sizeof(plainText)) == 0)
        printf("AES CBC SUCCESS\n");
    else
        printf("AES CBC FAILED TO MATCH\n");
    /* test aes GCM */
    if((ret = wc_AesInit(aes, NULL, WOLFHSM_DEV_ID)) != 0) {
        printf("Failed to wc_AesInit %d\n", ret);
        goto exit;
    }
#ifdef WOLFHSM_SYMMETRIC_INTERNAL
    keyId = 0;
    if ((ret = wh_Client_KeyCache(client, 0, labelStart, sizeof(labelStart), key, sizeof(key), &keyId)) != 0) {
        printf("Failed to wh_Client_KeyCache %d\n", ret);
        goto exit;
    }
    wh_Client_SetKeyAes(aes, keyId);
    if ((ret = wc_AesSetIV(aes, iv)) != 0) {
        printf("Failed to wc_AesSetIV %d\n", ret);
        goto exit;
    }
#else
    if ((ret = wc_AesSetKey(aes, key, AES_BLOCK_SIZE, iv, AES_ENCRYPTION)) != 0) {
        printf("Failed to wc_AesSetKey %d\n", ret);
        goto exit;
    }
#endif
    if ((ret = wc_AesGcmEncrypt(aes, (byte*)cipherText, (byte*)plainText, sizeof(plainText), iv, sizeof(iv), authTag, sizeof(authTag), authIn, sizeof(authIn))) != 0) {
        printf("Failed to wc_AesGcmEncrypt %d\n", ret);
        goto exit;
    }
#ifndef WOLFHSM_SYMMETRIC_INTERNAL
    if ((ret = wc_AesSetKey(aes, key, AES_BLOCK_SIZE, iv, AES_DECRYPTION)) != 0) {
        printf("Failed to wc_AesSetKey %d\n", ret);
        goto exit;
    }
#endif
    if ((ret = wc_AesGcmDecrypt(aes, (byte*)finalText, (byte*)cipherText, sizeof(plainText), iv, sizeof(iv), authTag, sizeof(authTag), authIn, sizeof(authIn))) != 0) {
        printf("Failed to wc_AesGcmDecrypt %d\n", ret);
        goto exit;
    }
#ifdef WOLFHSM_SYMMETRIC_INTERNAL
    if((ret = wh_Client_KeyEvict(client, keyId)) != 0) {
        printf("Failed to wh_Client_KeyEvict %d\n", ret);
        goto exit;
    }
#endif
    if (memcmp(plainText, finalText, sizeof(plainText)) == 0)
        printf("AES GCM SUCCESS\n");
    else
        printf("AES GCM FAILED TO MATCH\n");
    /* test rsa */
    if((ret = wc_InitRsaKey_ex(rsa, NULL, WOLFHSM_DEV_ID)) != 0) {
        printf("Failed to wc_InitRsaKey_ex %d\n", ret);
        goto exit;
    }
    if((ret = wc_MakeRsaKey(rsa, 2048, 65537, rng)) != 0) {
        printf("Failed to wc_MakeRsaKey %d\n", ret);
        goto exit;
    }
    if ((ret = wc_RsaPublicEncrypt((byte*)plainText, sizeof(plainText), (byte*)cipherText,
        sizeof(cipherText), rsa, rng)) < 0) {
        printf("Failed to wc_RsaPublicEncrypt %d\n", ret);
        goto exit;
    }
    if ((ret = wc_RsaPrivateDecrypt((byte*)cipherText, ret, (byte*)finalText,
        sizeof(finalText), rsa)) < 0) {
        printf("Failed to wc_RsaPrivateDecrypt %d\n", ret);
        goto exit;
    }
    XMEMCPY((uint8_t*)&keyId, (uint8_t*)&rsa->devCtx, sizeof(keyId));
    if ((ret = wh_Client_KeyEvictRequest(client, keyId)) != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvictRequest %d\n", ret);
        goto exit;
    }
    do {
        ret = wh_Client_KeyEvictResponse(client);
    } while (ret == WH_ERROR_NOTREADY);
    if (ret != 0) {
        WH_ERROR_PRINT("Failed to wh_Client_KeyEvictResponse %d\n", ret);
        goto exit;
    }
    if((ret = wc_FreeRsaKey(rsa)) != 0) {
        printf("Failed to wc_FreeRsaKey %d\n", ret);
        goto exit;
    }
    printf("RSA KEYGEN SUCCESS\n");
    if (memcmp(plainText, finalText, sizeof(plainText)) == 0)
        printf("RSA SUCCESS\n");
    else
        printf("RSA FAILED TO MATCH\n");
    /* test ecc */
    if((ret = wc_ecc_init_ex(eccPrivate, NULL, WOLFHSM_DEV_ID)) != 0) {
        printf("Failed to wc_ecc_init_ex %d\n", ret);
        goto exit;
    }
    if((ret = wc_ecc_init_ex(eccPublic, NULL, WOLFHSM_DEV_ID)) != 0) {
        printf("Failed to wc_ecc_init_ex %d\n", ret);
        goto exit;
    }
    if((ret = wc_ecc_make_key(rng, 32, eccPrivate)) != 0) {
        printf("Failed to wc_ecc_make_key %d\n", ret);
        goto exit;
    }
    if((ret = wc_ecc_make_key(rng, 32, eccPublic)) != 0) {
        printf("Failed to wc_ecc_make_key %d\n", ret);
        goto exit;
    }
    outLen = 32;
    if((ret = wc_ecc_shared_secret(eccPrivate, eccPublic, (byte*)cipherText, &outLen)) != 0) {
        printf("Failed to wc_ecc_shared_secret %d\n", ret);
        goto exit;
    }
    if((ret = wc_ecc_shared_secret(eccPublic, eccPrivate, (byte*)finalText, &outLen)) != 0) {
        printf("Failed to wc_ecc_shared_secret %d\n", ret);
        goto exit;
    }
    if (memcmp(cipherText, finalText, outLen) == 0)
        printf("ECDH SUCCESS\n");
    else
        printf("ECDH FAILED TO MATCH\n");
    outLen = 32;
    if((ret = wc_ecc_sign_hash((void*)cipherText, sizeof(cipherText), (void*)finalText, &outLen, rng, eccPrivate)) != 0) {
        printf("Failed to wc_ecc_sign_hash %d\n", ret);
        goto exit;
    }
    if((ret = wc_ecc_verify_hash((void*)finalText, outLen, (void*)cipherText, sizeof(cipherText), &res, eccPrivate)) != 0) {
        printf("Failed to wc_ecc_verify_hash %d\n", ret);
        goto exit;
    }
    if (res == 1)
        printf("ECC SIGN/VERIFY SUCCESS\n");
    else
        printf("ECC SIGN/VERIFY FAIL\n");
    /* test curve25519 */
    if ((ret = wc_curve25519_init_ex(curve25519PrivateKey, NULL, WOLFHSM_DEV_ID)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_init_ex %d\n", ret);
        goto exit;
    }
    if ((ret = wc_curve25519_init_ex(curve25519PublicKey, NULL, WOLFHSM_DEV_ID)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_init_ex %d\n", ret);
        goto exit;
    }
    if ((ret = wc_curve25519_make_key(rng, CURVE25519_KEYSIZE, curve25519PrivateKey)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_make_key %d\n", ret);
        goto exit;
    }
    if ((ret = wc_curve25519_make_key(rng, CURVE25519_KEYSIZE, curve25519PublicKey)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_make_key %d\n", ret);
        goto exit;
    }
    outLen = sizeof(sharedOne);
    if ((ret = wc_curve25519_shared_secret(curve25519PrivateKey, curve25519PublicKey, sharedOne, (word32*)&outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_shared_secret %d\n", ret);
        goto exit;
    }
    if ((ret = wc_curve25519_shared_secret(curve25519PublicKey, curve25519PrivateKey, sharedTwo, (word32*)&outLen)) != 0) {
        WH_ERROR_PRINT("Failed to wc_curve25519_shared_secret %d\n", ret);
        goto exit;
    }
    if (XMEMCMP(sharedOne, sharedTwo, outLen) != 0) {
        WH_ERROR_PRINT("CURVE25519 shared secrets don't match\n");
    }


exit:
    wc_curve25519_free(curve25519PrivateKey);
    wc_curve25519_free(curve25519PublicKey);
    wc_FreeRng(rng);

    /* Tell server to close */
    WH_TEST_RETURN_ON_FAIL(wh_Client_CommClose(client));

    if (ret == 0) {
        WH_TEST_RETURN_ON_FAIL(wh_Client_Cleanup(client));
    }
    else {
        wh_Client_Cleanup(client);
    }

    return ret;
}

int whTest_CryptoServerConfig(whServerConfig* config)
{
    whServerContext server[1] = {0};
    whCommConnected am_connected = WH_COMM_CONNECTED;
    int ret = 0;
#ifndef WH_CFG_TEST_NO_CUSTOM_SERVERS
    int userChange = 0;
#endif

    if (config == NULL) {
        return WH_ERROR_BADARGS;
    }

    WH_TEST_RETURN_ON_FAIL(wh_Server_Init(server, config));
    WH_TEST_RETURN_ON_FAIL(wh_Server_SetConnected(server, am_connected));
    server->comm->client_id = 1;

    while(am_connected == WH_COMM_CONNECTED) {
        ret = wh_Server_HandleRequestMessage(server);
        if ((ret != WH_ERROR_NOTREADY) &&
                (ret != WH_ERROR_OK)) {
            WH_ERROR_PRINT("Failed to wh_Server_HandleRequestMessage: %d\n", ret);
            break;
        }
        wh_Server_GetConnected(server, &am_connected);

#ifndef WH_CFG_TEST_NO_CUSTOM_SERVERS
        /* keep alive for 2 user changes */
        if (am_connected != WH_COMM_CONNECTED && userChange < 2) {
            if (userChange == 0)
                server->comm->client_id = 2;
            else if (userChange == 1)
                server->comm->client_id = 1;
            userChange++;
            am_connected = WH_COMM_CONNECTED;
            WH_TEST_RETURN_ON_FAIL(wh_Server_SetConnected(server, am_connected));
        }
#endif /* !WH_CFG_TEST_NO_CUSTOM_SERVERS */
    }

    if ((ret == 0) || (ret == WH_ERROR_NOTREADY)) {
        WH_TEST_RETURN_ON_FAIL(wh_Server_Cleanup(server));
    } else {
        ret = wh_Server_Cleanup(server);
    }

    return ret;
}

#if defined(WH_CFG_TEST_POSIX)
static void* _whClientTask(void *cf)
{
    WH_TEST_ASSERT(0 == whTest_CryptoClientConfig(cf));
    return NULL;
}

static void* _whServerTask(void* cf)
{
    WH_TEST_ASSERT(0 == whTest_CryptoServerConfig(cf));
    return NULL;
}


static void _whClientServerThreadTest(whClientConfig* c_conf,
                                whServerConfig* s_conf)
{
    pthread_t cthread = {0};
    pthread_t sthread = {0};

    void* retval;
    int rc = 0;

    rc = pthread_create(&sthread, NULL, _whServerTask, s_conf);
    if (rc == 0) {
        rc = pthread_create(&cthread, NULL, _whClientTask, c_conf);
        if (rc == 0) {
            /* All good. Block on joining */
            pthread_join(cthread, &retval);
            pthread_join(sthread, &retval);
        } else {
            /* Cancel the server thread */
            pthread_cancel(sthread);
            pthread_join(sthread, &retval);

        }
    }
}

static int wh_ClientServer_MemThreadTest(void)
{
    uint8_t req[BUFFER_SIZE] = {0};
    uint8_t resp[BUFFER_SIZE] = {0};

    whTransportMemConfig tmcf[1] = {{
        .req       = (whTransportMemCsr*)req,
        .req_size  = sizeof(req),
        .resp      = (whTransportMemCsr*)resp,
        .resp_size = sizeof(resp),
    }};
    /* Client configuration/contexts */
    whTransportClientCb         tccb[1]   = {WH_TRANSPORT_MEM_CLIENT_CB};
    whTransportMemClientContext tmcc[1]   = {0};
    whCommClientConfig          cc_conf[1] = {{
                 .transport_cb      = tccb,
                 .transport_context = (void*)tmcc,
                 .transport_config  = (void*)tmcf,
                 .client_id         = 1,
    }};
    whClientConfig c_conf[1] = {{
       .comm = cc_conf,
    }};
    /* Server configuration/contexts */
    whTransportServerCb         tscb[1]   = {WH_TRANSPORT_MEM_SERVER_CB};
    whTransportMemServerContext tmsc[1]   = {0};
    whCommServerConfig          cs_conf[1] = {{
                 .transport_cb      = tscb,
                 .transport_context = (void*)tmsc,
                 .transport_config  = (void*)tmcf,
                 .server_id         = 124,
    }};

    /* RamSim Flash state and configuration */
    whFlashRamsimCtx fc[1] = {0};
    whFlashRamsimCfg fc_conf[1] = {{
        .size       = 1024 * 1024, /* 1MB  Flash */
        .sectorSize = 128 * 1024,  /* 128KB  Sector Size */
        .pageSize   = 8,           /* 8B   Page Size */
        .erasedByte = ~(uint8_t)0,
    }};
    const whFlashCb  fcb[1]          = {WH_FLASH_RAMSIM_CB};

    /* NVM Flash Configuration using RamSim HAL Flash */
    whNvmFlashConfig nf_conf[1] = {{
        .cb      = fcb,
        .context = fc,
        .config  = fc_conf,
    }};
    whNvmFlashContext nfc[1] = {0};
    whNvmCb nfcb[1] = {WH_NVM_FLASH_CB};

    whNvmConfig n_conf[1] = {{
            .cb = nfcb,
            .context = nfc,
            .config = nf_conf,
    }};
    whNvmContext nvm[1] = {{0}};

    /* Crypto context */
    crypto_context crypto[1] = {{
            .devId = INVALID_DEVID,
    }};

    whServerConfig                  s_conf[1] = {{
       .comm_config = cs_conf,
       .nvm = nvm,
       .crypto = crypto,
       .devId = INVALID_DEVID,
    }};

    WH_TEST_RETURN_ON_FAIL(wh_Nvm_Init(nvm, n_conf));

    WH_TEST_RETURN_ON_FAIL(wolfCrypt_Init());
    WH_TEST_RETURN_ON_FAIL(wc_InitRng_ex(crypto->rng, NULL, crypto->devId));

    _whClientServerThreadTest(c_conf, s_conf);

    wh_Nvm_Cleanup(nvm);
    wc_FreeRng(crypto->rng);
    wolfCrypt_Cleanup();

    return WH_ERROR_OK;
}
#endif /* WH_CFG_TEST_POSIX */

int whTest_Crypto(void)
{
#if defined(WH_CFG_TEST_POSIX)
    printf("Testing crypto: (pthread) mem...\n");
    WH_TEST_RETURN_ON_FAIL(wh_ClientServer_MemThreadTest());
#endif
    return 0;
}

#endif  /* WOLFHSM_NO_CRYPTO */
