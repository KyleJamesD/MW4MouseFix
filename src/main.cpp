// MW4 Mouse Fix - DirectInput8 Proxy DLL
// Intercepts mouse input and applies sensitivity/deadzone fixes
// for MechWarrior 4 Vengeance / Black Knight on modern systems

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <math.h>

// ============================================================
// Config globals (loaded from mousefix.ini)
// ============================================================
static float g_SensitivityX = 3.0f;
static float g_SensitivityY = 3.0f;
static float g_DeadzoneCompensation = 1.0f;
static float g_NonLinearBoost = 2.0f;    // NEW: how aggressively to boost small movements
static float g_BoostThreshold = 10.0f;   // NEW: deltas below this get the extra boost
static bool  g_DebugLog = false;
static FILE* g_LogFile = nullptr;

// Handle to the REAL dinput8.dll
static HMODULE g_RealDInput8 = nullptr;

// Typedef for the real DirectInput8Create function
typedef HRESULT(WINAPI* DirectInput8Create_t)(
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

    // Replace DLL filename with ini filename
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        strcpy(lastSlash + 1, "mousefix.ini");
    }

    g_SensitivityX = (float)GetPrivateProfileIntA("MouseFix", "SensitivityX10", 30, path) / 10.0f;
    g_SensitivityY = (float)GetPrivateProfileIntA("MouseFix", "SensitivityY10", 30, path) / 10.0f;
    g_DeadzoneCompensation = (float)GetPrivateProfileIntA("MouseFix", "DeadzoneCompensationX10", 10, path) / 10.0f;
    g_NonLinearBoost = (float)GetPrivateProfileIntA("MouseFix", "NonLinearBoostX10", 20, path) / 10.0f;
    g_BoostThreshold = (float)GetPrivateProfileIntA("MouseFix", "BoostThreshold", 10, path);
    g_DebugLog = GetPrivateProfileIntA("MouseFix", "DebugLog", 0, path) != 0;

    if (g_DebugLog) {
        char logPath[MAX_PATH];
        GetModuleFileNameA(hModule, logPath, MAX_PATH);
        lastSlash = strrchr(logPath, '\\');
        if (lastSlash) strcpy(lastSlash + 1, "mousefix.log");
        g_LogFile = fopen(logPath, "w");
        LogMsg("MW4 Mouse Fix loaded!\n");
        LogMsg("SensitivityX: %.1f\n", g_SensitivityX);
        LogMsg("SensitivityY: %.1f\n", g_SensitivityY);
        LogMsg("DeadzoneCompensation: %.1f\n", g_DeadzoneCompensation);
        LogMsg("NonLinearBoost: %.1f\n", g_NonLinearBoost);
        LogMsg("BoostThreshold: %.0f\n", g_BoostThreshold);
    }
}

// ============================================================
// Load the REAL dinput8.dll from system32
// ============================================================
static bool LoadRealDInput8() {
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat(sysPath, "\\dinput8.dll");

    g_RealDInput8 = LoadLibraryA(sysPath);
    if (!g_RealDInput8) {
        // Try SysWOW64 for 32-bit on 64-bit Windows
        GetWindowsDirectoryA(sysPath, MAX_PATH);
        strcat(sysPath, "\\SysWOW64\\dinput8.dll");
        g_RealDInput8 = LoadLibraryA(sysPath);
    }

    if (!g_RealDInput8) {
        MessageBoxA(NULL, "MW4 Mouse Fix: Could not load real dinput8.dll!", "Error", MB_OK);
        return false;
    }

    g_RealDirectInput8Create = (DirectInput8Create_t)GetProcAddress(g_RealDInput8, "DirectInput8Create");
    if (!g_RealDirectInput8Create) {
        MessageBoxA(NULL, "MW4 Mouse Fix: Could not find DirectInput8Create!", "Error", MB_OK);
        return false;
    }

    LogMsg("Real dinput8.dll loaded from: %s\n", sysPath);
    return true;
}

// ============================================================
// Non-linear boost function
// Small values get amplified a LOT, large values barely affected
// ============================================================
static float ApplyNonLinearBoost(float value, float sensitivity) {
    if (value == 0.0f) return 0.0f;

    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    float absVal = fabsf(value);

    // Step 1: Deadzone compensation - always add a minimum push
    if (g_DeadzoneCompensation > 0.0f) {
        absVal += g_DeadzoneCompensation;
    }

    // Step 2: Non-linear boost for small movements
    // For values below threshold, we apply a power curve that
    // maps small inputs to larger outputs
    if (absVal < g_BoostThreshold && g_NonLinearBoost > 0.0f) {
        // Normalize to 0-1 range within threshold
        float normalized = absVal / g_BoostThreshold;

        // Apply power curve: sqrt-like for boost
        // boost=2.0 means we use pow(x, 0.5) = sqrt, which lifts small values
        // boost=3.0 means pow(x, 0.33), even more aggressive
        float exponent = 1.0f / g_NonLinearBoost;
        float boosted = powf(normalized, exponent);

        // Scale back to threshold range
        absVal = boosted * g_BoostThreshold;
    }

    // Step 3: Apply sensitivity
    return sign * absVal * sensitivity;
}

// ============================================================
// Wrapped IDirectInputDevice8 - This is where the magic happens
// ============================================================
class WrappedDevice : public IDirectInputDevice8A {
private:
    IDirectInputDevice8A* m_real;
    bool m_isMouse;
    LONG m_residualX;  // Sub-pixel accumulator for X
    LONG m_residualY;  // Sub-pixel accumulator for Y

public:
    WrappedDevice(IDirectInputDevice8A* real, bool isMouse)
        : m_real(real), m_isMouse(isMouse), m_residualX(0), m_residualY(0) {
        LogMsg("WrappedDevice created, isMouse=%d\n", isMouse);
    }

    // Apply our mouse fix to delta values
    void FixMouseDelta(LONG& dx, LONG& dy) {
        float fx = (float)dx;
        float fy = (float)dy;

        // Apply non-linear boost + sensitivity with sub-pixel accumulation
        float scaledX = ApplyNonLinearBoost(fx, g_SensitivityX) + (float)m_residualX / 10.0f;
        float scaledY = ApplyNonLinearBoost(fy, g_SensitivityY) + (float)m_residualY / 10.0f;

        // Convert back to integer, accumulate remainder
        dx = (LONG)scaledX;
        dy = (LONG)scaledY;
        m_residualX = (LONG)((scaledX - (float)dx) * 10.0f);
        m_residualY = (LONG)((scaledY - (float)dy) * 10.0f);

        LogMsg("Mouse delta: %ld, %ld\n", dx, dy);
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

    // *** THE KEY METHOD - GetDeviceState ***
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData) override {
        HRESULT hr = m_real->GetDeviceState(cbData, lpvData);

        if (SUCCEEDED(hr) && m_isMouse && lpvData && cbData >= sizeof(DIMOUSESTATE)) {
            DIMOUSESTATE* ms = (DIMOUSESTATE*)lpvData;
            FixMouseDelta(ms->lX, ms->lY);
        }

        return hr;
    }

    // *** ALSO INTERCEPT GetDeviceData (buffered input mode) ***
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod,
        LPDWORD pdwInOut, DWORD dwFlags) override {
        HRESULT hr = m_real->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);

        if (SUCCEEDED(hr) && m_isMouse && rgdod && pdwInOut) {
            for (DWORD i = 0; i < *pdwInOut; i++) {
                LPDIDEVICEOBJECTDATA data = (LPDIDEVICEOBJECTDATA)((BYTE*)rgdod + i * cbObjectData);

                if (data->dwOfs == DIMOFS_X || data->dwOfs == DIMOFS_Y) {
                    LONG val = (LONG)data->dwData;
                    LONG dummy = 0;

                    if (data->dwOfs == DIMOFS_X) {
                        FixMouseDelta(val, dummy);
                    }
                    else {
                        FixMouseDelta(dummy, val);
                    }

                    data->dwData = (DWORD)val;
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
    WrappedDInput8(IDirectInput8A* real) : m_real(real) {
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
            bool isMouse = (rguid == GUID_SysMouse);
            LogMsg("CreateDevice called - GUID: %s\n", isMouse ? "SysMouse" : "Other");

            if (isMouse) {
                LogMsg(">>> Wrapping mouse device! <<<\n");
                *lplpDirectInputDevice = new WrappedDevice(*lplpDirectInputDevice, true);
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
        LogMsg("DirectInput8Create called, version=0x%08X\n", dwVersion);

        if (!g_RealDirectInput8Create) {
            if (!LoadRealDInput8()) {
                return DIERR_NOTINITIALIZED;
            }
        }

        HRESULT hr = g_RealDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

        if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
            if (riidltf == IID_IDirectInput8A) {
                LogMsg("Wrapping IDirectInput8A interface\n");
                *ppvOut = new WrappedDInput8((IDirectInput8A*)*ppvOut);
            }
            else if (riidltf == IID_IDirectInput8W) {
                LogMsg("IDirectInput8W requested - not wrapping (mouse fix only supports A)\n");
            }
        }

        return hr;
    }

    HRESULT WINAPI DllCanUnloadNow(void) {
        return S_FALSE;
    }

    HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
        if (!g_RealDInput8) LoadRealDInput8();
        typedef HRESULT(WINAPI* fn)(REFCLSID, REFIID, LPVOID*);
        fn real = (fn)GetProcAddress(g_RealDInput8, "DllGetClassObject");
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
        LogMsg("DllMain: DLL_PROCESS_ATTACH\n");
        break;
    case DLL_PROCESS_DETACH:
        LogMsg("DllMain: DLL_PROCESS_DETACH\n");
        if (g_LogFile) fclose(g_LogFile);
        if (g_RealDInput8) FreeLibrary(g_RealDInput8);
        break;
    }
    return TRUE;
}