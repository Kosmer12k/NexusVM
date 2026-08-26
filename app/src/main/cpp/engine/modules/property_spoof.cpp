/*
 * NexusVM — Module 5: Property Spoofing Implementation
 */

#include "property_spoof.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <sstream>
#include <algorithm>

#define MOD_TAG "PropertySpoof"

namespace nexusvm {

PropertySpoof::PropertySpoof(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

PropertySpoof::~PropertySpoof() {
    shutdown();
}

int PropertySpoof::init() {
    setupDefaults();
    MLOG_I(MOD_TAG, "Initialized with %zu properties", m_properties.size());
    return 0;
}

void PropertySpoof::shutdown() {
    MLOG_I(MOD_TAG, "Shutdown");
}

void PropertySpoof::setupDefaults() {
    const auto& config = m_engine.getConfig();
    
    // Device identity
    setProperty("ro.product.model", config.device_model.empty() ? "NexusVM Phone" : config.device_model);
    setProperty("ro.product.brand", config.device_brand.empty() ? "NexusVM" : config.device_brand);
    setProperty("ro.product.manufacturer", config.device_manufacturer.empty() ? "NexusVM" : config.device_manufacturer);
    setProperty("ro.product.name", "nexusvm");
    setProperty("ro.product.device", "nexusvm");
    setProperty("ro.product.board", "nexusvm");
    
    if (!config.device_fingerprint.empty()) {
        setProperty("ro.build.fingerprint", config.device_fingerprint);
    } else {
        setProperty("ro.build.fingerprint", 
            config.device_brand + "/" + "nexusvm/nexusvm:" + 
            config.android_version + "/NVM1.0/1:userdebug/test-keys");
    }
    
    // Android version
    setProperty("ro.build.version.release", config.android_version.empty() ? "12" : config.android_version);
    setProperty("ro.build.version.sdk", std::to_string(config.sdk_version));
    setProperty("ro.build.version.security_patch", "2024-01-01");
    setProperty("ro.build.version.incremental", "NVM100");
    setProperty("ro.build.type", "userdebug");
    setProperty("ro.build.display.id", "NexusVM-1.0");
    
    // Hardware
    setProperty("ro.hardware", "nexusvm");
    setProperty("ro.hardware.chipname", "nexusvm");
    setProperty("ro.board.platform", "nexusvm");
    setProperty("ro.arch", "aarch64");
    setProperty("ro.product.cpu.abi", "arm64-v8a");
    setProperty("ro.product.cpu.abilist", "arm64-v8a,armeabi-v7a,armeabi");
    setProperty("ro.product.cpu.abilist64", "arm64-v8a");
    setProperty("ro.product.cpu.abilist32", "armeabi-v7a,armeabi");
    
    // Display
    setProperty("ro.sf.lcd_density", std::to_string(config.display_dpi));
    setProperty("persist.sys.lcd_density", std::to_string(config.display_dpi));
    
    // Network
    setProperty("gsm.version.baseband", "1.0");
    setProperty("gsm.sim.operator.alpha", "NexusVM");
    setProperty("gsm.sim.operator.iso-country", "us");
    setProperty("gsm.sim.operator.numeric", "310260");
    
    // Security / container flags
    setProperty("ro.secure", "0");
    setProperty("ro.debuggable", "1");
    setProperty("ro.adb.secure", "0");
    setProperty("ro.boot.selinux", "permissive");
    setProperty("selinux.reload_policy", "0");
    setProperty("persist.sys.dalvik.vm.lib.2", "libart.so");
    
    // VM flags
    setProperty("dalvik.vm.heapsize", "512m");
    setProperty("dalvik.vm.heapgrowthlimit", "256m");
    setProperty("dalvik.vm.heapstartsize", "8m");
    setProperty("dalvik.vm.heapminfree", "512k");
    setProperty("dalvik.vm.heapmaxfree", "8m");
    setProperty("dalvik.vm.heaptargetutilization", "0.75");
    
    // NexusVM identifier
    setProperty("ro.nexusvm.version", "1.0.0");
    setProperty("ro.nexusvm.container", "1");
}

// ============================================================================
// Property Management
// ============================================================================

int PropertySpoof::setProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_properties[key] = value;
    MLOG_D(MOD_TAG, "Set property: %s = %s", key.c_str(), value.c_str());
    return 0;
}

std::string PropertySpoof::getProperty(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_properties.find(key);
    if (it != m_properties.end()) {
        return it->second;
    }
    return "";
}

// ============================================================================
// File I/O
// ============================================================================

int PropertySpoof::loadFromFile(const std::string& path) {
    std::string content = FileUtils::readFile(path);
    if (content.empty()) return -1;
    
    std::istringstream stream(content);
    std::string line;
    int count = 0;
    
    while (std::getline(stream, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        
        if (!key.empty()) {
            setProperty(key, value);
            count++;
        }
    }
    
    MLOG_I(MOD_TAG, "Loaded %d properties from %s", count, path.c_str());
    return 0;
}

int PropertySpoof::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::ostringstream out;
    out << "# NexusVM Custom Properties\n";
    out << "# Generated automatically\n\n";
    
    for (const auto& [key, value] : m_properties) {
        out << key << "=" << value << "\n";
    }
    
    return FileUtils::writeFile(path, out.str());
}

// ============================================================================
// Build.prop Patching
// ============================================================================

int PropertySpoof::patchBuildProp(const std::string& build_prop_path) {
    MLOG_I(MOD_TAG, "Patching build.prop: %s", build_prop_path.c_str());
    
    std::string content = FileUtils::readFile(build_prop_path);
    
    // If build.prop doesn't exist, create one from our properties
    if (content.empty()) {
        MLOG_I(MOD_TAG, "Creating new build.prop from custom properties");
        return saveToFile(build_prop_path);
    }
    
    // Patch existing build.prop: replace matching keys, append new ones
    std::istringstream stream(content);
    std::string line;
    std::ostringstream output;
    std::unordered_map<std::string, bool> written;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            output << line << "\n";
            continue;
        }
        
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            output << line << "\n";
            continue;
        }
        
        std::string key = line.substr(0, eq_pos);
        
        // Check if we have a custom value for this key
        auto it = m_properties.find(key);
        if (it != m_properties.end()) {
            output << key << "=" << it->second << "\n";
            written[key] = true;
        } else {
            output << line << "\n";
        }
    }
    
    // Append properties that weren't in the original file
    output << "\n# NexusVM custom properties\n";
    for (const auto& [key, value] : m_properties) {
        if (written.find(key) == written.end()) {
            output << key << "=" << value << "\n";
        }
    }
    
    int ret = FileUtils::writeFile(build_prop_path, output.str());
    MLOG_I(MOD_TAG, "build.prop patched (%zu custom properties applied)", m_properties.size());
    return ret;
}

// ============================================================================
// Device Profiles
// ============================================================================

int PropertySpoof::loadDeviceProfile(const std::string& profile_name) {
    MLOG_I(MOD_TAG, "Loading device profile: %s", profile_name.c_str());
    
    if (profile_name == "pixel7pro" || profile_name == "Pixel 7 Pro") {
        setProperty("ro.product.model", "Pixel 7 Pro");
        setProperty("ro.product.brand", "google");
        setProperty("ro.product.manufacturer", "Google");
        setProperty("ro.product.device", "cheetah");
        setProperty("ro.product.name", "cheetah");
        setProperty("ro.build.fingerprint", "google/cheetah/cheetah:13/TQ3A.230805.001/10316531:user/release-keys");
        setProperty("ro.hardware", "tensor");
        
    } else if (profile_name == "s23ultra" || profile_name == "Galaxy S23 Ultra") {
        setProperty("ro.product.model", "SM-S918B");
        setProperty("ro.product.brand", "samsung");
        setProperty("ro.product.manufacturer", "samsung");
        setProperty("ro.product.device", "dm3q");
        setProperty("ro.product.name", "dm3q");
        setProperty("ro.build.fingerprint", "samsung/dm3qxxx/dm3q:13/TP1A.220624.014/S918BXXU2AWH1:user/release-keys");
        setProperty("ro.hardware", "exynos");
        
    } else if (profile_name == "rogphone6" || profile_name == "ROG Phone 6") {
        setProperty("ro.product.model", "ASUS_AI2201");
        setProperty("ro.product.brand", "asus");
        setProperty("ro.product.manufacturer", "asus");
        setProperty("ro.product.device", "AI2201");
        setProperty("ro.product.name", "WW_AI2201");
        setProperty("ro.build.fingerprint", "asus/WW_AI2201/ASUS_AI2201:13/TQ3A.230805.001/33.0804.2060.86:user/release-keys");
        setProperty("ro.hardware", "qcom");
        
    } else if (profile_name == "ipad" || profile_name == "iPad Pro") {
        // Fun easter egg — spoof as iPad (some games check for tablet)
        setProperty("ro.product.model", "iPad Pro");
        setProperty("ro.product.brand", "Apple");
        setProperty("ro.product.manufacturer", "Apple");
        setProperty("ro.sf.lcd_density", "264");
        
    } else {
        MLOG_W(MOD_TAG, "Unknown device profile: %s", profile_name.c_str());
        return -1;
    }
    
    MLOG_I(MOD_TAG, "Device profile '%s' loaded", profile_name.c_str());
    return 0;
}

} // namespace nexusvm
