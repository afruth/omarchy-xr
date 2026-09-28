#include "power.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
int failures=0;
void expect(bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; ++failures; } }
void supply(const std::filesystem::path& root, const std::string& name, const std::string& type, const std::string& key, const std::string& value) {
    std::filesystem::create_directories(root/name);
    std::ofstream(root/name/"type") << type << '\n';
    std::ofstream(root/name/key) << value << '\n';
}
}

int main() {
    expect(power::parseSettings("power-v1 1")==std::optional<bool>(true), "power-v1 1 enables the saver");
    expect(power::parseSettings("power-v1 0")==std::optional<bool>(false), "power-v1 0 disables the saver");
    expect(!power::parseSettings("power-v1 2") && !power::parseSettings("power-v2 1") && !power::parseSettings("power-v1 1 x") && !power::parseSettings(""),
           "Malformed settings are rejected");

    const auto root=std::filesystem::temp_directory_path()/("omxr-power-"+std::to_string(getpid()));
    std::filesystem::remove_all(root);
    expect(!power::onBattery(root/"missing"), "An unreadable sysfs counts as external power");
    supply(root, "BAT0", "Battery", "status", "Discharging");
    supply(root, "AC", "Mains", "online", "0");
    supply(root, "ucsi-source-psy-USBC000:001", "USB", "online", "0");
    expect(power::onBattery(root), "A discharging battery with every supply offline is on battery");
    supply(root, "BAT0", "Battery", "status", "Full");
    supply(root, "hid-mouse-battery", "Battery", "status", "Discharging");
    std::ofstream(root/"hid-mouse-battery"/"scope") << "Device\n";
    expect(!power::onBattery(root), "A discharging mouse battery is not the computer on battery");
    supply(root, "BAT0", "Battery", "status", "Discharging");
    supply(root, "ucsi-source-psy-USBC000:001", "USB", "online", "1");
    expect(!power::onBattery(root), "An online USB-C supply is external power");
    supply(root, "ucsi-source-psy-USBC000:001", "USB", "online", "0");
    supply(root, "BAT0", "Battery", "status", "Full");
    expect(!power::onBattery(root), "A full battery with no supply online is not discharging");
    std::filesystem::remove_all(root);

    power::Saver saver;
    expect(saver.captureHz(60)==60 && saver.budget()==1, "An off saver caps nothing");
    saver.battery=true;
    expect(saver.captureHz(60)==60, "The saver must be enabled to cap");
    saver.enabled=true;
    expect(saver.active() && saver.captureHz(60)==30 && saver.captureHz(24)==24 && saver.budget()==.5f, "An active saver caps at 30 Hz and halves the budget");
    saver.battery=false;
    expect(!saver.active() && saver.captureHz(120)==120, "External power lifts the cap");
    if (failures) return 1;
    std::cout << "Power: settings, battery detection and saver caps passed" << std::endl;
    return 0;
}
