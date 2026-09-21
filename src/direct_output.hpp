#pragma once
#include <memory>
#include <functional>
#include <string>
#include <vector>
class DirectOutput {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    DirectOutput(const std::string& connector, bool stereo);
    ~DirectOutput();
    int width() const;
    int height() const;
    bool pump();
    void swap(const std::function<void()>& service = {});
    static std::vector<std::string> connectors();
};
