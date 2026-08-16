#include "StartupManager.h"

#include <Windows.h>

#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kSettingsKey[] = L"Software\\IME-AltSwap";
constexpr wchar_t kRunValueName[] = L"IME-AltSwap";
constexpr wchar_t kInitializedValueName[] = L"StartupPreferenceInitialized";

std::wstring ExecutableCommand() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    path.resize(length);
    return L"\"" + path + L"\" --startup";
}

bool ReadRunValue(std::wstring& value) noexcept {
    DWORD type = 0;
    DWORD byteCount = 0;
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        kRunKey,
        kRunValueName,
        RRF_RT_REG_SZ,
        &type,
        nullptr,
        &byteCount);
    if (status != ERROR_SUCCESS || byteCount < sizeof(wchar_t)) {
        return false;
    }

    value.assign(byteCount / sizeof(wchar_t), L'\0');
    status = RegGetValueW(
        HKEY_CURRENT_USER,
        kRunKey,
        kRunValueName,
        RRF_RT_REG_SZ,
        &type,
        value.data(),
        &byteCount);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return true;
}
}  // namespace

bool StartupManager::IsEnabled() noexcept {
    std::wstring value;
    const std::wstring expected = ExecutableCommand();
    return !expected.empty() && ReadRunValue(value) && _wcsicmp(value.c_str(), expected.c_str()) == 0;
}

bool StartupManager::SetEnabled(const bool enabled) noexcept {
    if (!enabled) {
        const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValueName);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    const std::wstring command = ExecutableCommand();
    if (command.empty()) {
        return false;
    }

    HKEY key = nullptr;
    const LSTATUS opened = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        kRunKey,
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        &key,
        nullptr);
    if (opened != ERROR_SUCCESS) {
        return false;
    }

    const LSTATUS written = RegSetValueExW(
        key,
        kRunValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return written == ERROR_SUCCESS;
}

void StartupManager::EnableOnFirstRun() noexcept {
    DWORD initialized = 0;
    DWORD type = 0;
    DWORD size = sizeof(initialized);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kSettingsKey,
            kInitializedValueName,
            RRF_RT_REG_DWORD,
            &type,
            &initialized,
            &size) == ERROR_SUCCESS) {
        return;
    }

    if (!SetEnabled(true)) {
        return;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return;
    }

    initialized = 1;
    RegSetValueExW(
        key,
        kInitializedValueName,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&initialized),
        sizeof(initialized));
    RegCloseKey(key);
}
