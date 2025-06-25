#include "LakeConfigSection.h"
#include <cstring>
#include <cstdlib>

LakeConfigSection::LakeConfigSection(const char *nameVal) : name(nameVal) {}
LakeConfigSection::~LakeConfigSection() {}

CONFIG_SEC_RET LakeConfigSection::ProcessKeyValue(char *key, char *value) {
    if (strcmp(key, "Name") == 0) {
        name = value;
        return VALID_RESULT;
    } else if (strcmp(key, "Lat") == 0) {
        lat = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcmp(key, "Lon") == 0) {
        lon = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcmp(key, "Area") == 0) {
        area = static_cast<float>(atof(value)) * 1e6f; // km² to m²
        return VALID_RESULT;
    } else if (strcmp(key, "MaxDepth") == 0) {
        maxDepth = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcmp(key, "InitialLevel") == 0) {
        initialLevel = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcmp(key, "OutflowFile") == 0) {
        outflowFile = value;
        return VALID_RESULT;
    } else if (strcmp(key, "ThVolume") == 0) {
        thVolume = static_cast<float>(atof(value)) * 1e9f; // km³ to m³
        return VALID_RESULT;
    } else if (strcmp(key, "Klake") == 0) {
        retentionConstant = static_cast<float>(atof(value)); // hours
        return VALID_RESULT;
    }
    return INVALID_RESULT;
}

CONFIG_SEC_RET LakeConfigSection::ValidateSection() {
    // Add validation logic as needed
    return VALID_RESULT;
} 