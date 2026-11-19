#ifndef KOFVS_DINPUT_MIN_H
#define KOFVS_DINPUT_MIN_H

#include "win.h"

#define VT(obj, idx, type) ((type)((*(void***)(obj))[idx]))

#define DI_VERSION 0x0800u
#define DI8DEVCLASS_GAMECTRL 4u
#define DIEDFL_ATTACHEDONLY 1u
#define DIENUM_CONTINUE 1
#define DISCL_NONEXCLUSIVE 2u
#define DISCL_BACKGROUND 8u
#define DIPH_DEVICE 0u
#define DIPROP_RANGE ((const GUID*)4)
#define DIPROP_DEADZONE ((const GUID*)5)
#define DIPROP_GUIDANDPATH ((const GUID*)12)
#define DIDFT_AXIS 0x00000003u
#define DIDFT_BUTTON 0x0000000Cu
#define DIDFT_POV 0x00000010u
#define DIDFT_ANYINSTANCE 0x00FFFF00u
#define DIDFT_OPTIONAL 0x80000000u
#define DIDOI_ASPECTPOSITION 0x00000100u
#define DIDF_ABSAXIS 1u
#define DIERR_INPUTLOST ((HRESULT)0x8007001Eu)
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000Cu)
#define DIERR_UNPLUGGED ((HRESULT)0x80040209u)

enum {
    DI8_CreateDevice = 3,
    DI8_EnumDevices = 4
};

enum {
    DID_Release = 2,
    DID_GetCapabilities = 3,
    DID_GetProperty = 5,
    DID_SetProperty = 6,
    DID_Acquire = 7,
    DID_Unacquire = 8,
    DID_GetDeviceState = 9,
    DID_GetDeviceData = 10,
    DID_SetDataFormat = 11,
    DID_SetCooperativeLevel = 13,
    DID_Poll = 25
};

typedef struct DIDEVICEINSTANCEW_ {
    DWORD dwSize;
    GUID guidInstance;
    GUID guidProduct;
    DWORD dwDevType;
    wch tszInstanceName[260];
    wch tszProductName[260];
    GUID guidFFDriver;
    u16 wUsagePage;
    u16 wUsage;
} DIDEVICEINSTANCEW_;

typedef struct DIOBJECTDATAFORMAT_ {
    const GUID* pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJECTDATAFORMAT_;

typedef struct DIDATAFORMAT_ {
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    DIOBJECTDATAFORMAT_* rgodf;
} DIDATAFORMAT_;

typedef struct DIPROPHEADER_ {
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER_;

typedef struct DIPROPRANGE_ {
    DIPROPHEADER_ diph;
    LONG lMin;
    LONG lMax;
} DIPROPRANGE_;

typedef struct DIPROPGUIDANDPATH_ {
    DIPROPHEADER_ diph;
    GUID guidClass;
    wch wszPath[260];
} DIPROPGUIDANDPATH_;

typedef struct DIJOYSTATE_ {
    LONG lX, lY, lZ;
    LONG lRx, lRy, lRz;
    LONG rglSlider[2];
    DWORD rgdwPOV[4];
    u8 rgbButtons[32];
} DIJOYSTATE_;

typedef HRESULT (WINAPI *DirectInput8Create_fn)(HMODULE, DWORD, const GUID*, void**, void*);
typedef int (WINAPI *DIEnumCb_fn)(const DIDEVICEINSTANCEW_*, void*);
typedef HRESULT (WINAPI *DI8_EnumDevices_fn)(void*, DWORD, DIEnumCb_fn, void*, DWORD);
typedef HRESULT (WINAPI *DI8_CreateDevice_fn)(void*, const GUID*, void**, void*);
typedef u32 (WINAPI *Release_fn)(void*);
typedef HRESULT (WINAPI *DID_Simple_fn)(void*);
typedef HRESULT (WINAPI *DID_GetDeviceState_fn)(void*, DWORD, void*);
typedef HRESULT (WINAPI *DID_GetDeviceData_fn)(void*, DWORD, void*, DWORD*, DWORD);
typedef HRESULT (WINAPI *DID_SetDataFormat_fn)(void*, const DIDATAFORMAT_*);
typedef HRESULT (WINAPI *DID_SetCooperativeLevel_fn)(void*, HWND, DWORD);
typedef HRESULT (WINAPI *DID_Property_fn)(void*, const GUID*, DIPROPHEADER_*);

extern const GUID IID_IDirectInput8W_;
extern const GUID IID_IDirectInput8A_;
extern const GUID GUID_SysKeyboard_;
extern const DIDATAFORMAT_ g_dfJoystick;

typedef struct XINPUT_STATE_ {
    DWORD dwPacketNumber;
    u16 wButtons;
    u8 bLeftTrigger;
    u8 bRightTrigger;
    i16 sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_STATE_;

typedef struct XINPUT_VIBRATION_ {
    u16 wLeftMotorSpeed;
    u16 wRightMotorSpeed;
} XINPUT_VIBRATION_;

typedef struct XINPUT_CAPABILITIES_EX_ {
    u8 Type;
    u8 SubType;
    u16 Flags;
    u16 wButtons;
    u8 bLeftTrigger;
    u8 bRightTrigger;
    i16 sThumbLX, sThumbLY, sThumbRX, sThumbRY;
    u16 wLeftMotorSpeed;
    u16 wRightMotorSpeed;
    u16 VendorId;
    u16 ProductId;
    u16 ProductVersion;
    u16 unk1;
    DWORD unk2;
} XINPUT_CAPABILITIES_EX_;

#define ERROR_SUCCESS_ 0u
#define ERROR_DEVICE_NOT_CONNECTED_ 1167u

typedef DWORD (WINAPI *XInputGetState_fn)(DWORD, XINPUT_STATE_*);
typedef DWORD (WINAPI *XInputSetState_fn)(DWORD, XINPUT_VIBRATION_*);
typedef DWORD (WINAPI *XInputGetCapabilitiesEx_fn)(DWORD, DWORD, DWORD, XINPUT_CAPABILITIES_EX_*);

#endif
