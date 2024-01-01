/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class beagle_basta_BastaJNIWrapper */

#ifndef _Included_beagle_basta_BastaJNIWrapper
#define _Included_beagle_basta_BastaJNIWrapper
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     beagle_basta_BastaJNIWrapper
 * Method:    allocateCoalescentBuffers
 * Signature: (III)I
 */
JNIEXPORT jint JNICALL Java_beagle_basta_BastaJNIWrapper_allocateCoalescentBuffers
  (JNIEnv *, jobject, jint, jint, jint);

/*
 * Class:     beagle_basta_BastaJNIWrapper
 * Method:    getBastaBuffer
 * Signature: (II[D)I
 */
JNIEXPORT jint JNICALL Java_beagle_basta_BastaJNIWrapper_getBastaBuffer
  (JNIEnv *, jobject, jint, jint, jdoubleArray);

/*
 * Class:     beagle_basta_BastaJNIWrapper
 * Method:    updateBastaPartials
 * Signature: (I[II[IIII)I
 */
JNIEXPORT jint JNICALL Java_beagle_basta_BastaJNIWrapper_updateBastaPartials
  (JNIEnv *, jobject, jint, jintArray, jint, jintArray, jint, jint, jint);

/*
 * Class:     beagle_basta_BastaJNIWrapper
 * Method:    accumulateBastaPartials
 * Signature: (I[II[III[D)I
 */
JNIEXPORT jint JNICALL Java_beagle_basta_BastaJNIWrapper_accumulateBastaPartials
  (JNIEnv *, jobject, jint, jintArray, jint, jintArray, jint, jdoubleArray, jint, jint, jdoubleArray);

#ifdef __cplusplus
}
#endif
#endif
