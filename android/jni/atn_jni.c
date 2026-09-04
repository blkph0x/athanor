/*
 * JNI glue for the Athanor daemon (REQ-4.1). Native mesh is our C tree.
 * Spec: DEC-0015. No OkHttp, no Play services.
 */
#include <jni.h>
#include "atn_crypto.h"
#include "atn_platform.h"

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
