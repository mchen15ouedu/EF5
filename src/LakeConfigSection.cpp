#include "LakeConfigSection.h"
#include "Messages.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <cctype>
#include <algorithm>

std::map<std::string, LakeConfigSection *> g_lakeConfigs;

LakeConfigSection::LakeConfigSection(const char *nameVal) : 
    name(nameVal), lat(0.0f), lon(0.0f), area(0.0f), maxDepth(0.0f), 
    initialLevel(0.0f), thVolume(0.0f), gridNodeIndex(-1), outflow(0.0f), 
    inflow(0.0f), storage(0.0f), precipitation(0.0f), evaporation(0.0f), 
    retentionConstant(0.0f), obsFlowAccum(0.0f), obsFlowAccumSet(false), outputts(false) {}

LakeConfigSection::~LakeConfigSection() {}

CONFIG_SEC_RET LakeConfigSection::ProcessKeyValue(char *key, char *value) {
    if (strcasecmp(key, "Name") == 0) {
        name = value;
        return VALID_RESULT;
    } else if (strcasecmp(key, "Lat") == 0) {
        lat = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcasecmp(key, "Lon") == 0) {
        lon = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcasecmp(key, "Area") == 0) {
        area = static_cast<float>(atof(value)) * 1e6f; // km² to m²
        return VALID_RESULT;
    } else if (strcasecmp(key, "MaxDepth") == 0) {
        maxDepth = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcasecmp(key, "InitialLevel") == 0) {
        initialLevel = static_cast<float>(atof(value));
        return VALID_RESULT;
    } else if (strcasecmp(key, "OutflowFile") == 0) {
        outflowFile = value;
        return VALID_RESULT;
    } else if (strcasecmp(key, "ThVolume") == 0) {
        thVolume = static_cast<float>(atof(value)) * 1e9f; // km³ to m³
        return VALID_RESULT;
    } else if (strcasecmp(key, "Klake") == 0) {
        retentionConstant = static_cast<float>(atof(value)); // hours
        return VALID_RESULT;
    } else if (strcasecmp(key, "obsFam") == 0) {
        obsFlowAccum = static_cast<float>(atof(value));
        obsFlowAccumSet = true;
        return VALID_RESULT;
    } else if (strcasecmp(key, "outputts") == 0) {
        // Check for Y, y, TRUE, true, 1
        std::string valStr = value;
        std::transform(valStr.begin(), valStr.end(), valStr.begin(), ::tolower);
        outputts = (valStr == "y" || valStr == "true" || valStr == "1");
        return VALID_RESULT;
    }
    return INVALID_RESULT;
}

CONFIG_SEC_RET LakeConfigSection::ValidateSection() {
    // Add validation logic as needed
    return VALID_RESULT;
}

bool LakeConfigSection::IsDuplicate(char *name) {
    std::map<std::string, LakeConfigSection *>::iterator itr =
        g_lakeConfigs.find(std::string(name));
    if (itr == g_lakeConfigs.end()) {
        return false;
    } else {
        return true;
    }
} 