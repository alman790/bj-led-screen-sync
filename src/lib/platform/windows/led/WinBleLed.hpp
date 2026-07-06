#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lib/bj_core.hpp"

class WinBleLed {
public:
    ~WinBleLed();
    bool connect(const wchar_t* address);
    bool isReady() const;
    void write(bj::RGB color, int maxChannel);

private:
    void close();

    bool ready_ = false;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    struct CharacteristicStorage;
    CharacteristicStorage* characteristic_ = nullptr;
};
