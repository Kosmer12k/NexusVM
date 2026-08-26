/*
 * NexusVM — Module 5: Property Spoofing
 * 
 * Manages system property spoofing for device model customization.
 * Hooks __system_property_get/read to return custom values.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

namespace nexusvm {

class NexusEngine;

class PropertySpoof {
public:
    explicit PropertySpoof(NexusEngine& engine);
    ~PropertySpoof();
    
    int init();
    void shutdown();
    
    /** Set a custom system property */
    int setProperty(const std::string& key, const std::string& value);
    
    /** Get a property value (checks custom first, then real) */
    std::string getProperty(const std::string& key) const;
    
    /** Load properties from a config file */
    int loadFromFile(const std::string& path);
    
    /** Save current properties to a config file */
    int saveToFile(const std::string& path) const;
    
    /** Patch build.prop file on the guest filesystem */
    int patchBuildProp(const std::string& build_prop_path);
    
    /** Load default device profile (e.g., "pixel7pro", "rogphone6") */
    int loadDeviceProfile(const std::string& profile_name);
    
    /** Get all custom properties */
    const std::unordered_map<std::string, std::string>& getProperties() const { return m_properties; }

private:
    NexusEngine& m_engine;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_properties;
    
    /** Setup default properties from engine config */
    void setupDefaults();
};

} // namespace nexusvm
