#include <jni.h>
#include <string>
#include <android/log.h>

// Include the drills_capi header
// You might need to adjust this path based on where you place drills_capi.h
// For simplicity, assume it's accessible or copied into this project's include path.
#include "drills_capi.h"

#define LOG_TAG "DrillsJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global pointer for the Drills engine instance
static DrillsEngine* drillsEngine = nullptr;

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_drillsandroidapp_DrillsEngine_initEngine(JNIEnv* env, jobject /* this */) {
    LOGD("initEngine called");
    if (drillsEngine != nullptr) {
        LOGD("Engine already initialized. Destroying existing one.");
        drills_destroy_engine(drillsEngine);
        drillsEngine = nullptr;
    }

    drillsEngine = drills_create_engine();
    if (drillsEngine == nullptr) {
        LOGE("Failed to create Drills engine.");
        return env->NewStringUTF("Error: Failed to create engine");
    }
    LOGD("Drills engine created successfully.");
    return env->NewStringUTF("Engine initialized");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_drillsandroidapp_DrillsEngine_loadRules(JNIEnv* env, jobject /* this */, jstring drlRules) {
    LOGD("loadRules called");
    if (drillsEngine == nullptr) {
        LOGE("Engine not initialized. Call initEngine first.");
        return env->NewStringUTF("Error: Engine not initialized");
    }

    const char* rulesCStr = env->GetStringUTFChars(drlRules, nullptr);
    if (rulesCStr == nullptr) {
        LOGE("Failed to get RFL rules string.");
        return env->NewStringUTF("Error: Invalid rules string");
    }

    DrillsError* error = nullptr;
    bool success = drills_load_drl_rules(drillsEngine, rulesCStr, &error);
    env->ReleaseStringUTFChars(drlRules, rulesCStr);

    if (!success) {
        std::string errorMessage = "Error loading rules: ";
        if (error != nullptr) {
            errorMessage += drills_get_error_message(error);
            drills_destroy_error(error);
        }
        LOGE("%s", errorMessage.c_str());
        return env->NewStringUTF(errorMessage.c_str());
    }
    LOGD("Rules loaded successfully.");
    return env->NewStringUTF("Rules loaded");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_drillsandroidapp_DrillsEngine_insertFact(JNIEnv* env, jobject /* this */, jstring factJson) {
    LOGD("insertFact called");
    if (drillsEngine == nullptr) {
        LOGE("Engine not initialized. Call initEngine first.");
        return env->NewStringUTF("Error: Engine not initialized");
    }

    const char* factCStr = env->GetStringUTFChars(factJson, nullptr);
    if (factCStr == nullptr) {
        LOGE("Failed to get fact JSON string.");
        return env->NewStringUTF("Error: Invalid fact string");
    }

    DrillsError* error = nullptr;
    bool success = drills_insert_fact_json(drillsEngine, factCStr, &error);
    env->ReleaseStringUTFChars(factJson, factCStr);

    if (!success) {
        std::string errorMessage = "Error inserting fact: ";
        if (error != nullptr) {
            errorMessage += drills_get_error_message(error);
            drills_destroy_error(error);
        }
        LOGE("%s", errorMessage.c_str());
        return env->NewStringUTF(errorMessage.c_str());
    }
    LOGD("Fact inserted successfully.");
    return env->NewStringUTF("Fact inserted");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_drillsandroidapp_DrillsEngine_fireAllRules(JNIEnv* env, jobject /* this */) {
    LOGD("fireAllRules called");
    if (drillsEngine == nullptr) {
        LOGE("Engine not initialized. Call initEngine first.");
        return env->NewStringUTF("Error: Engine not initialized");
    }

    DrillsError* error = nullptr;
    int firedRules = drills_fire_all_rules(drillsEngine, &error);

    if (firedRules < 0) { // An error occurred
        std::string errorMessage = "Error firing rules: ";
        if (error != nullptr) {
            errorMessage += drills_get_error_message(error);
            drills_destroy_error(error);
        }
        LOGE("%s", errorMessage.c_str());
        return env->NewStringUTF(errorMessage.c_str());
    }
    LOGD("Rules fired successfully. Fired %d rules.", firedRules);
    return env->NewStringUTF(std::string("Rules fired: ") + std::to_string(firedRules)).c_str();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_drillsandroidapp_DrillsEngine_destroyEngine(JNIEnv* env, jobject /* this */) {
    LOGD("destroyEngine called");
    if (drillsEngine != nullptr) {
        drills_destroy_engine(drillsEngine);
        drillsEngine = nullptr;
        LOGD("Drills engine destroyed.");
        return env->NewStringUTF("Engine destroyed");
    }
    LOGD("Engine already null or not initialized.");
    return env->NewStringUTF("Engine not active");
}
