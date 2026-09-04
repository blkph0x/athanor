/*
 * JNI glue for the Athanor daemon (REQ-4.1). Native mesh is our C tree.
 * Spec: DEC-0015, DEC-0017. No OkHttp, no Play services.
 */
#include <jni.h>
#include "atn_crypto.h"
#include "atn_dmon.h"
#include "atn_platform.h"

static atn_dmon g_dmon;
static int g_dmon_inited;

static void dmon_once(void)
{
    if (!g_dmon_inited) {
        atn_dmon_init(&g_dmon);
        g_dmon_inited = 1;
    }
}

JNIEXPORT jstring JNICALL
Java_com_athanor_daemon_AtnNative_platformId(JNIEnv *env, jclass cls)
{
    (void)cls;
    return (*env)->NewStringUTF(env, atn_platform_id());
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_randomBytes(JNIEnv *env, jclass cls,
                                              jbyteArray out)
{
    jbyte *p;
    jint n;
    int rc;
    (void)cls;
    if (out == NULL) {
        return ATN_ERR_PARAM;
    }
    n = (*env)->GetArrayLength(env, out);
    p = (*env)->GetByteArrayElements(env, out, NULL);
    if (p == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_random_bytes(p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, out, p, 0);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonLoad(JNIEnv *env, jclass cls,
                                           jbyteArray deviceKey, jbyteArray cluster)
{
    jbyte *dk, *ck;
    int rc;
    (void)cls;
    dmon_once();
    if (deviceKey == NULL || cluster == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, deviceKey) != 32 ||
        (*env)->GetArrayLength(env, cluster) != 32) {
        return ATN_ERR_LEN;
    }
    dk = (*env)->GetByteArrayElements(env, deviceKey, NULL);
    ck = (*env)->GetByteArrayElements(env, cluster, NULL);
    if (dk == NULL || ck == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_load(&g_dmon, (const uint8_t *)dk, (const uint8_t *)ck);
    (*env)->ReleaseByteArrayElements(env, deviceKey, dk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, cluster, ck, JNI_ABORT);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonRequire(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    dmon_once();
    return atn_dmon_require(&g_dmon);
}

JNIEXPORT void JNICALL
Java_com_athanor_daemon_AtnNative_dmonFlush(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    dmon_once();
    atn_dmon_flush(&g_dmon);
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonHbInit(JNIEnv *env, jclass cls,
                                             jbyteArray id, jlong epoch,
                                             jbyteArray head)
{
    jbyte *idp, *hp;
    int rc;
    (void)cls;
    dmon_once();
    if (id == NULL || head == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, id) != (jint)ATN_HB_ID_LEN ||
        (*env)->GetArrayLength(env, head) != (jint)ATN_HB_HEAD_LEN) {
        return ATN_ERR_LEN;
    }
    idp = (*env)->GetByteArrayElements(env, id, NULL);
    hp = (*env)->GetByteArrayElements(env, head, NULL);
    if (idp == NULL || hp == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_hb_init(&g_dmon, (const uint8_t *)idp, (uint64_t)epoch,
                          (const uint8_t *)hp);
    (*env)->ReleaseByteArrayElements(env, id, idp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, head, hp, JNI_ABORT);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonHbAddPeer(JNIEnv *env, jclass cls,
                                                jbyteArray id, jbyteArray key)
{
    jbyte *idp, *kp;
    int rc;
    (void)cls;
    dmon_once();
    if (id == NULL || key == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, id) != (jint)ATN_HB_ID_LEN ||
        (*env)->GetArrayLength(env, key) != 32) {
        return ATN_ERR_LEN;
    }
    idp = (*env)->GetByteArrayElements(env, id, NULL);
    kp = (*env)->GetByteArrayElements(env, key, NULL);
    if (idp == NULL || kp == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_hb_add_peer(&g_dmon, (const uint8_t *)idp, (const uint8_t *)kp);
    (*env)->ReleaseByteArrayElements(env, id, idp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, key, kp, JNI_ABORT);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonHbIngest(JNIEnv *env, jclass cls,
                                               jbyteArray msg)
{
    jbyte *p;
    jint n;
    int rc;
    (void)cls;
    dmon_once();
    if (msg == NULL) {
        return ATN_ERR_PARAM;
    }
    n = (*env)->GetArrayLength(env, msg);
    p = (*env)->GetByteArrayElements(env, msg, NULL);
    if (p == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_hb_ingest(&g_dmon, (const uint8_t *)p, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, msg, p, JNI_ABORT);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonHbTick(JNIEnv *env, jclass cls,
                                             jlong bucket)
{
    (void)env;
    (void)cls;
    dmon_once();
    return atn_dmon_hb_tick(&g_dmon, (uint64_t)bucket);
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmonHbState(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    dmon_once();
    return atn_dmon_hb_state(&g_dmon);
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmon2faEnroll(JNIEnv *env, jclass cls,
                                                jbyteArray id, jbyteArray keyOut)
{
    jbyte *idp, *kp;
    int rc;
    (void)cls;
    dmon_once();
    if (id == NULL || keyOut == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, id) != (jint)ATN_2FA_ID_LEN ||
        (*env)->GetArrayLength(env, keyOut) != (jint)ATN_2FA_KEY_LEN) {
        return ATN_ERR_LEN;
    }
    idp = (*env)->GetByteArrayElements(env, id, NULL);
    kp = (*env)->GetByteArrayElements(env, keyOut, NULL);
    if (idp == NULL || kp == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_2fa_enroll(&g_dmon, (const uint8_t *)idp, (uint8_t *)kp);
    (*env)->ReleaseByteArrayElements(env, id, idp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, keyOut, kp, 0);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmon2faChallenge(JNIEnv *env, jclass cls,
                                                   jbyteArray id, jbyteArray chalOut)
{
    jbyte *idp, *cp;
    int rc;
    (void)cls;
    dmon_once();
    if (id == NULL || chalOut == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, id) != (jint)ATN_2FA_ID_LEN ||
        (*env)->GetArrayLength(env, chalOut) != (jint)ATN_2FA_CHAL_LEN) {
        return ATN_ERR_LEN;
    }
    idp = (*env)->GetByteArrayElements(env, id, NULL);
    cp = (*env)->GetByteArrayElements(env, chalOut, NULL);
    if (idp == NULL || cp == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_2fa_challenge(&g_dmon, (const uint8_t *)idp, (uint8_t *)cp);
    (*env)->ReleaseByteArrayElements(env, id, idp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, chalOut, cp, 0);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_athanor_daemon_AtnNative_dmon2faVerify(JNIEnv *env, jclass cls,
                                                jbyteArray id, jbyteArray chal,
                                                jbyteArray resp)
{
    jbyte *idp, *cp, *rp;
    int rc;
    (void)cls;
    dmon_once();
    if (id == NULL || chal == NULL || resp == NULL) {
        return ATN_ERR_PARAM;
    }
    if ((*env)->GetArrayLength(env, id) != (jint)ATN_2FA_ID_LEN ||
        (*env)->GetArrayLength(env, chal) != (jint)ATN_2FA_CHAL_LEN ||
        (*env)->GetArrayLength(env, resp) != (jint)ATN_2FA_RESP_LEN) {
        return ATN_ERR_LEN;
    }
    idp = (*env)->GetByteArrayElements(env, id, NULL);
    cp = (*env)->GetByteArrayElements(env, chal, NULL);
    rp = (*env)->GetByteArrayElements(env, resp, NULL);
    if (idp == NULL || cp == NULL || rp == NULL) {
        return ATN_ERR_PARAM;
    }
    rc = atn_dmon_2fa_verify(&g_dmon, (const uint8_t *)idp, (const uint8_t *)cp,
                             (const uint8_t *)rp);
    (*env)->ReleaseByteArrayElements(env, id, idp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, chal, cp, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, resp, rp, JNI_ABORT);
    return rc;
}
