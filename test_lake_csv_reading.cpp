#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>

// Simple test program to verify CSV reading functionality
// This mimics the CSV reading functions in BasinConfigSection.cpp

struct LakeInfo {
    std::string name;
    double lat;
    double lon;
    double th_volume;
    double area;
    double retention_constant;
};

bool ReadLakesFromCSV(const std::string& filename, std::vector<LakeInfo>& lakes) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Failed to open lakes CSV file: " << filename << std::endl;
        return false;
    }

    std::string line;
    // Skip header line
    if (std::getline(file, line)) {
        // Check if header is valid
        if (line.find("name") == std::string::npos || 
            line.find("lat") == std::string::npos || 
            line.find("lon") == std::string::npos) {
            std::cerr << "Invalid lakes CSV header in file: " << filename << std::endl;
            return false;
        }
    }

    // Read data lines
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        LakeInfo lake;

        // Parse name
        if (!std::getline(ss, token, ',')) continue;
        lake.name = token;

        // Parse lat
        if (!std::getline(ss, token, ',')) continue;
        lake.lat = atof(token.c_str());

        // Parse lon
        if (!std::getline(ss, token, ',')) continue;
        lake.lon = atof(token.c_str());

        // Parse th_volume
        if (!std::getline(ss, token, ',')) continue;
        lake.th_volume = atof(token.c_str());

        // Parse area
        if (!std::getline(ss, token, ',')) continue;
        lake.area = atof(token.c_str());

        // Parse klake (optional)
        if (std::getline(ss, token, ',')) {
            lake.retention_constant = atof(token.c_str());
        } else {
            lake.retention_constant = 24.0; // Default value
        }

        lakes.push_back(lake);
    }

    file.close();
    std::cout << "Successfully loaded " << lakes.size() << " lakes from " << filename << std::endl;
    return true;
}

bool ReadEngineeredDischargeFromCSV(const std::string& filename, std::map<std::string, double>& engineeredDischarge) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Failed to open engineered discharge CSV file: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::vector<std::string> lakeNames;
    
    // Read header line to get lake names
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        
        // Skip timestamp column
        if (!std::getline(ss, token, ',')) return false;
        
        // Read lake names from header
        while (std::getline(ss, token, ',')) {
            lakeNames.push_back(token);
        }
    }

    // Read first data line (we'll use the first timestamp for now)
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;

        // Skip timestamp
        if (!std::getline(ss, token, ',')) return false;

        // Parse discharge values for each lake
        int lakeIndex = 0;
        while (std::getline(ss, token, ',') && lakeIndex < (int)lakeNames.size()) {
            double discharge = atof(token.c_str());
            if (discharge != 0.0 || token == "0" || token == "0.0") {
                engineeredDischarge[lakeNames[lakeIndex]] = discharge;
            } else {
                std::cerr << "Failed to parse discharge value '" << token << "' for lake '" << lakeNames[lakeIndex] << "'" << std::endl;
            }
            lakeIndex++;
        }
    }

    file.close();
    std::cout << "Successfully loaded " << engineeredDischarge.size() << " engineered discharge values from " << filename << std::endl;
    return true;
}

int main() {
    std::cout << "Testing Lake CSV Reading Functionality" << std::endl;
    std::cout << "=====================================" << std::endl;

    // Test lakes CSV reading
    std::vector<LakeInfo> lakes;
    if (ReadLakesFromCSV("lakes.csv", lakes)) {
        std::cout << "\nLakes loaded:" << std::endl;
        for (size_t i = 0; i < lakes.size(); ++i) {
            const LakeInfo& lake = lakes[i];
            std::cout << "  " << lake.name << ": lat=" << lake.lat 
                      << ", lon=" << lake.lon 
                      << ", volume=" << lake.th_volume 
                      << ", area=" << lake.area 
                      << ", klake=" << lake.retention_constant << std::endl;
        }
    }

    // Test engineered discharge CSV reading
    std::map<std::string, double> engineeredDischarge;
    if (ReadEngineeredDischargeFromCSV("engineered_discharge.csv", engineeredDischarge)) {
        std::cout << "\nEngineered discharge values:" << std::endl;
        for (std::map<std::string, double>::const_iterator it = engineeredDischarge.begin(); 
             it != engineeredDischarge.end(); ++it) {
            std::cout << "  " << it->first << ": " << it->second << " m³/s" << std::endl;
        }
    }

    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
} 