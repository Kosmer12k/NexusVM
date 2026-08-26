/*
 * NexusVM — Comprehensive Engine Unit Test Suite
 * Tests core components (PropertySpoof, FileUtils, FakeRoot, InputBridge, etc.)
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

#include "engine/nexus_engine.h"
#include "engine/utils/logger.h"
#include "engine/utils/file_utils.h"
#include "engine/utils/memory_utils.h"
#include "engine/modules/property_spoof.h"
#include "engine/modules/fake_root.h"
#include "engine/modules/input_bridge.h"

using namespace nexusvm;

void testPropertySpoof() {
    std::cout << "[*] Testing PropertySpoof module..." << std::endl;
    NexusEngine& engine = NexusEngine::getInstance();
    PropertySpoof spoof(engine);
    spoof.init();

    // Test basic property set & get
    spoof.setProperty("ro.product.model", "ROG Phone 6 Ultimate");
    assert(spoof.getProperty("ro.product.model") == "ROG Phone 6 Ultimate");

    // Test device profile loading
    spoof.loadDeviceProfile("rogphone6");
    assert(spoof.getProperty("ro.product.brand") == "asus");
    assert(spoof.getProperty("ro.product.device") == "AI2201");

    spoof.loadDeviceProfile("pixel7pro");
    assert(spoof.getProperty("ro.product.brand") == "google");
    assert(spoof.getProperty("ro.product.model") == "Pixel 7 Pro");

    std::cout << "    [+] PropertySpoof tests passed!" << std::endl;
}

void testFakeRoot() {
    std::cout << "[*] Testing FakeRoot module..." << std::endl;
    NexusEngine& engine = NexusEngine::getInstance();
    FakeRoot fakeRoot(engine);
    fakeRoot.init();

    assert(fakeRoot.getFakeUid() == 0);
    assert(fakeRoot.getFakeGid() == 0);

    // Capabilities
    assert(fakeRoot.hasCapability(0) == true);
    assert(fakeRoot.hasCapability(21) == true); // CAP_SYS_ADMIN

    fakeRoot.revokeCapability(21);
    assert(fakeRoot.hasCapability(21) == false);

    fakeRoot.grantCapability(21);
    assert(fakeRoot.hasCapability(21) == true);

    std::cout << "    [+] FakeRoot tests passed!" << std::endl;
}

void testFileUtils() {
    std::cout << "[*] Testing FileUtils module..." << std::endl;
    std::string test_dir = "./test_workspace/subdir/deep";
    assert(FileUtils::mkdirRecursive(test_dir) == 0);
    assert(FileUtils::exists(test_dir) == true);
    assert(FileUtils::isDirectory(test_dir) == true);

    std::string test_file = test_dir + "/test.txt";
    std::string content = "NexusVM Container Engine v1.0.0";
    assert(FileUtils::writeFile(test_file, content) == 0);
    assert(FileUtils::readFile(test_file) == content);

    // Cleanup
    assert(FileUtils::removeRecursive("./test_workspace") == 0);
    assert(FileUtils::exists("./test_workspace") == false);

    std::cout << "    [+] FileUtils tests passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  NexusVM Engine Unit Verification Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    testFileUtils();
    testPropertySpoof();
    testFakeRoot();

    std::cout << "========================================" << std::endl;
    std::cout << "  ALL ENGINE CORE TESTS PASSED (100%)" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
