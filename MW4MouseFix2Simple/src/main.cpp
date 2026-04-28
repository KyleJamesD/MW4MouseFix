// MW4 Mouse Fix (Simple Edition) - DirectInput8 Proxy DLL
// Scales raw mouse X/Y deltas by configurable percentages.
// Keeps the original game's DirectInput code untouched while allowing quick tweaks.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>

// ============================================================
// Config globals (loaded from mousefix.ini)
// ============================================================
static float g_ScaleX = 1.25f;   // 125%
static float g_ScaleY = 1.25f;   // 125%
static bool  g_DebugLog = false;
static FILE* g_LogFile = nullptr;

// Handle to the REAL dinput8.dll
static HMODULE g_RealDInput8 = nullptr;

// Typedef for the real DirectInput8Create function
using DirectInput8Create_t = HRESULT(WINAPI*)(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter
);
static DirectInput8Create_t g_RealDirectInput8Create = nullptr;

// ============================================================
// Logging helper
// ============================================================
static void LogMsg(const char* fmt, ...) {
    if (!g_DebugLog || !g_LogFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_LogFile, fmt, args);
    fflush(g_LogFile);
    va_end(args);
}

// ============================================================
// Load config from mousefix.ini next to the DLL
// ============================================================
static void LoadConfig(HMODULE hModule) {
    char path[MAX_PATH];
    GetModuleFileNameA(hModule, path, MAX_PATH);

    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        strcpy(lastSlash + 1, "mousefix.ini");
    }

    const int scaleXPercent = GetPrivateProfileIntA("MouseFix", "ScalePercentX", 125, path);
    const int scaleYPercent = GetPrivateProfileIntA("MouseFix", "ScalePercentY", 125, path);
    g_ScaleX = (scaleXPercent <= 0 ? 100 : scaleXPercent) / 100.0f;
    g_ScaleY = (scaleYPercent <= 0 ? 100 : scaleYPercent) / 100.0f;
    g_DebugLog = GetPrivateProfileIntA("MouseFix", "DebugLog", 0, path) != 0;

    if (g_DebugLog) {
        char logPath[MAX_PATH];
        GetModuleFileNameA(hModule, logPath, MAX_PATH);
        lastSlash = strrchr(logPath, '\\');
        if (lastSlash) strcpy(lastSlash + 1, "mousefix.log");
        g_LogFile = fopen(logPath, "w");
        if (g_LogFile) {
            LogMsg("MW4 Mouse Fix (Simple) loaded!\n");
            LogMsg("ScaleX: %.2f\n", g_ScaleX);
            LogMsg("ScaleY: %.2f\n", g_ScaleY);
        }
    }
}

// ============================================================
// Load the REAL dinput8.dll from system32
// ============================================================
static bool LoadRealDInput8() {
    if (g_RealDInput8 && g_RealDirectInput8Create) return true;

    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat(sysPath, "\\dinput8.dll");

    g_RealDInput8 = LoadLibraryA(sysPath);
    if (!g_RealDInput8) {
        // Try SysWOW64 for 32-bit shim on 64-bit Windows
        GetWindowsDirectoryA(sysPath, MAX_PATH);
        strcat(sysPath, "\\SysWOW64\\dinput8.dll");
        g_RealDInput8 = LoadLibraryA(sysPath);
    }

    if (!g_RealDInput8) {
        MessageBoxA(nullptr, "MW4 Mouse Fix (Simple): Could not load real dinput8.dll!", "Error", MB_OK);
        return false;
    }

    g_RealDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(
        GetProcAddress(g_RealDInput8, "DirectInput8Create"));
    if (!g_RealDirectInput8Create) {
        MessageBoxA(nullptr, "MW4 Mouse Fix (Simple): Could not find DirectInput8Create!", "Error", MB_OK);
        return false;
    }

    LogMsg("Real dinput8.dll loaded successfully\n");
    return true;
}

// ============================================================
// Utility: scale delta + keep fractional residual for precision
// ============================================================
static void ApplyScale(LONG& value, float scale, float& residual) {
    if (value == 0 && residual == 0.0f) return;

    const float scaled = value * scale + residual;
    const float rounded = (scaled >= 0.0f) ? std::floor(scaled + 0.5f) : std::ceil(scaled - 0.5f);
    residual = scaled - rounded;
    value = static_cast<LONG>(rounded);
}

// ============================================================
// Wrapped IDirectInputDevice8 - scales mouse movements
// ============================================================
class WrappedDevice : public IDirectInputDevice8A {
private:
    IDirectInputDevice8A* m_real;
    bool m_isMouse;
    float m_residualX;
    float m_residualY;

public:
    WrappedDevice(IDirectInputDevice8A* real, bool isMouse)
        : m_real(real), m_isMouse(isMouse), m_residualX(0.0f), m_residualY(0.0f) {
        LogMsg("WrappedDevice created (isMouse=%d)\n", isMouse);
    }

    void ScaleMouseDelta(LONG& dx, LONG& dy) {
        if (!m_isMouse) return;

        LONG originalX = dx;
        LONG originalY = dy;

        ApplyScale(dx, g_ScaleX, m_residualX);
        ApplyScale(dy, g_ScaleY, m_residualY);

        LogMsg("Mouse delta %ld/%ld -> %ld/%ld\n", originalX, originalY, dx, dy);
    }

    // ======== IUnknown ========
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        return m_real->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = m_real->Release();
        if (ref == 0) {
            LogMsg("WrappedDevice released\n");
            delete this;
        }
        return ref;
    }

    // ======== IDirectInputDevice8A ========
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        return m_real->GetCapabilities(caps);
    }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags) override {
        return m_real->EnumObjects(cb, ref, flags);
    }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID rguid, LPDIPROPHEADER pdiph) override {
        return m_real->GetProperty(rguid, pdiph);
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID rguid, LPCDIPROPHEADER pdiph) override {
        return m_real->SetProperty(rguid, pdiph);
    }
    HRESULT STDMETHODCALLTYPE Acquire() override {
        return m_real->Acquire();
    }
    HRESULT STDMETHODCALLTYPE Unacquire() override {
        return m_real->Unacquire();
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData) override {
        HRESULT hr = m_real->GetDeviceState(cbData, lpvData);
        if (SUCCEEDED(hr) && m_isMouse && lpvData && cbData >= sizeof(DIMOUSESTATE)) {
            auto* ms = reinterpret_cast<DIMOUSESTATE*>(lpvData);
            ScaleMouseDelta(ms->lX, ms->lY);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod,
        LPDWORD pdwInOut, DWORD dwFlags) override {
        HRESULT hr = m_real->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);
        if (SUCCEEDED(hr) && m_isMouse && rgdod && pdwInOut) {
            for (DWORD i = 0; i < *pdwInOut; ++i) {
                auto* data = reinterpret_cast<LPDIDEVICEOBJECTDATA>(reinterpret_cast<BYTE*>(rgdod) + i * cbObjectData);
                if (data->dwOfs == DIMOFS_X) {
                    LONG dx = static_cast<LONG>(data->dwData);
                    LONG originalX = dx;
                    ApplyScale(dx, g_ScaleX, m_residualX);
                    data->dwData = static_cast<DWORD>(dx);
                    LogMsg("Buffered mouse X %ld -> %ld\n", originalX, dx);
                }
                else if (data->dwOfs == DIMOFS_Y) {
                    LONG dy = static_cast<LONG>(data->dwData);
                    LONG originalY = dy;
                    ApplyScale(dy, g_ScaleY, m_residualY);
                    data->dwData = static_cast<DWORD>(dy);
                    LogMsg("Buffered mouse Y %ld -> %ld\n", originalY, dy);
                }
            }
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT lpdf) override {
        return m_real->SetDataFormat(lpdf);
    }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE hEvent) override {
        return m_real->SetEventNotification(hEvent);
    }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD dwFlags) override {
        return m_real->SetCooperativeLevel(hwnd, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) override {
        return m_real->GetObjectInfo(pdidoi, dwObj, dwHow);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA pdidi) override {
        return m_real->GetDeviceInfo(pdidi);
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) override {
        return m_real->RunControlPanel(hwndOwner, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) override {
        return m_real->Initialize(hinst, dwVersion, rguid);
    }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter) override {
        return m_real->CreateEffect(rguid, lpeff, ppdeff, punkOuter);
    }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) override {
        return m_real->EnumEffects(lpCallback, pvRef, dwEffType);
    }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA pdei, REFGUID rguid) override {
        return m_real->GetEffectInfo(pdei, rguid);
    }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD pdwOut) override {
        return m_real->GetForceFeedbackState(pdwOut);
    }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD dwFlags) override {
        return m_real->SendForceFeedbackCommand(dwFlags);
    }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) override {
        return m_real->EnumCreatedEffectObjects(lpCallback, pvRef, fl);
    }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc) override {
        return m_real->Escape(pesc);
    }
    HRESULT STDMETHODCALLTYPE Poll() override {
        return m_real->Poll();
    }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) override {
        return m_real->SendDeviceData(cbObjectData, rgdod, pdwInOut, fl);
    }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags) override {
        return m_real->EnumEffectsInFile(lpszFileName, pec, pvRef, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags) override {
        return m_real->WriteEffectToFile(lpszFileName, dwEntries, rgDiFileEft, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) override {
        return m_real->BuildActionMap(lpdiaf, lpszUserName, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) override {
        return m_real->SetActionMap(lpdiaf, lpszUserName, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA lpdiDevImageInfoHeader) override {
        return m_real->GetImageInfo(lpdiDevImageInfoHeader);
    }
};

// ============================================================
// Wrapped IDirectInput8 - intercepts CreateDevice to wrap mouse
// ============================================================
class WrappedDInput8 : public IDirectInput8A {
private:
    IDirectInput8A* m_real;

public:
    explicit WrappedDInput8(IDirectInput8A* real) : m_real(real) {
        LogMsg("WrappedDInput8 created\n");
    }

    // ======== IUnknown ========
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        return m_real->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = m_real->Release();
        if (ref == 0) {
            LogMsg("WrappedDInput8 released\n");
            delete this;
        }
        return ref;
    }

    // ======== IDirectInput8A ========
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8A* lplpDirectInputDevice, LPUNKNOWN pUnkOuter) override {
        HRESULT hr = m_real->CreateDevice(rguid, lplpDirectInputDevice, pUnkOuter);
        if (SUCCEEDED(hr) && lplpDirectInputDevice && *lplpDirectInputDevice) {
            const bool isMouse = (rguid == GUID_SysMouse);
            LogMsg("CreateDevice GUID=%s\n", isMouse ? "GUID_SysMouse" : "Other");
            if (isMouse) {
                *lplpDirectInputDevice = new WrappedDevice(*lplpDirectInputDevice, true);
                LogMsg("Mouse device wrapped\n");
            }
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) override {
        return m_real->EnumDevices(dwDevType, lpCallback, pvRef, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance) override {
        return m_real->GetDeviceStatus(rguidInstance);
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) override {
        return m_real->RunControlPanel(hwndOwner, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion) override {
        return m_real->Initialize(hinst, dwVersion);
    }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID rguidClass, LPCSTR ptszName, LPGUID pguidInstance) override {
        return m_real->FindDevice(rguidClass, ptszName, pguidInstance);
    }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR ptszUserName, LPDIACTIONFORMATA lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags) override {
        return m_real->EnumDevicesBySemantics(ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags);
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSA lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) override {
        return m_real->ConfigureDevices(lpdiCallback, lpdiCDParams, dwFlags, pvRefData);
    }
};

// ============================================================
// DLL Exports
// ============================================================
static HMODULE g_hModule = nullptr;

extern "C" {

HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter
) {
    LogMsg("DirectInput8Create called\n");

    if (!g_RealDirectInput8Create) {
        if (!LoadRealDInput8()) {
            return DIERR_NOTINITIALIZED;
        }
    }

    HRESULT hr = g_RealDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

    if (SUCCEEDED(hr) && ppvOut && *ppvOut && riidltf == IID_IDirectInput8A) {
        *ppvOut = new WrappedDInput8(reinterpret_cast<IDirectInput8A*>(*ppvOut));
        LogMsg("IDirectInput8A wrapped\n");
    }

    return hr;
}

HRESULT WINAPI DllCanUnloadNow(void) {
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!g_RealDInput8) LoadRealDInput8();
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    Fn real = reinterpret_cast<Fn>(GetProcAddress(g_RealDInput8, "DllGetClassObject"));
    if (real) return real(rclsid, riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer(void) { return S_OK; }
HRESULT WINAPI DllUnregisterServer(void) { return S_OK; }

} // extern "C"

// ============================================================
// DLL Entry Point
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        LoadConfig(hModule);
        LogMsg("DllMain: PROCESS_ATTACH\n");
        break;
    case DLL_PROCESS_DETACH:
        LogMsg("DllMain: PROCESS_DETACH\n");
        if (g_LogFile) {
            fclose(g_LogFile);
            g_LogFile = nullptr;
        }
        if (g_RealDInput8) {
            FreeLibrary(g_RealDInput8);
            g_RealDInput8 = nullptr;
        }
        break;
    }
    return TRUE;
}
