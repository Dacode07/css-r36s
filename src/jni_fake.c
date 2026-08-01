#include <stdlib.h>
#include <string.h>

#include "jni_fake.h"
#include "util.h"

#define JNI_TABLE_SIZE 256

static void *jni_functions[JNI_TABLE_SIZE];
static void **jni_env_obj = jni_functions;

void *fake_env = &jni_env_obj;

static long jni_unimplemented(void) {
  tracePrintf("JNI: unimplemented function called\n");
  return 0;
}

static const char *GetStringUTFChars_fake(void *env, const char *jstr, unsigned char *is_copy) {
  (void)env;
  if (is_copy)
    *is_copy = 0;
  return jstr;
}

static void ReleaseStringUTFChars_fake(void *env, const char *jstr, const char *utf) {
  (void)env; (void)jstr; (void)utf;
}

static int GetStringUTFLength_fake(void *env, const char *jstr) {
  (void)env;
  return jstr ? (int)strlen(jstr) : 0;
}

void jni_init(void) {
  for (int i = 0; i < JNI_TABLE_SIZE; i++)
    jni_functions[i] = (void *)jni_unimplemented;
  jni_functions[164] = (void *)GetStringUTFLength_fake;
  jni_functions[169] = (void *)GetStringUTFChars_fake;
  jni_functions[170] = (void *)ReleaseStringUTFChars_fake;
}
