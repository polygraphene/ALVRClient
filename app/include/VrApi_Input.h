/************************************************************************************

Filename    :   VrApi_Input.h
Content     :   Input API
Created     :   Feb 9, 2016
Authors     :   Jonathan E. Wright
Language    :   C99

Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

*************************************************************************************/
#ifndef OVR_VrApi_Input_h
#define OVR_VrApi_Input_h

#include <stddef.h>
#include <stdint.h>
#include "VrApi_Config.h"
#include "VrApi_Types.h"

/// Describes button input types.
/// For the Gear VR Controller and headset, only the following ovrButton types are reported to the application:
///
/// ovrButton_Back, ovrButton_A, ovrButton_Enter
///
/// ovrButton_Home, ovrButton_VolUp, ovrButtonVolDown and ovrButton_Back are system buttons that are never
/// reported to applications.
/// ovrButton_Back button has system-level handling for long presses, but application-level handling for
/// short-presses. Since a short-press is determined by the time interval between down and up events, the
/// ovrButton_Back flag is only set when the back button comes up in less than the short-press time (0.25
/// seconds). The ovrButton_Back flag always signals a short press and will only remain set for a single frame.
typedef enum ovrButton_
{
	ovrButton_A           = 0x00000001,	// Set for trigger pulled on the Gear VR and Go Controllers
	ovrButton_B           = 0x00000002,
	ovrButton_RThumb      = 0x00000004,
	ovrButton_RShoulder   = 0x00000008,

	ovrButton_X           = 0x00000100,
	ovrButton_Y           = 0x00000200,
	ovrButton_LThumb      = 0x00000400,
	ovrButton_LShoulder   = 0x00000800,

	ovrButton_Up          = 0x00010000,
	ovrButton_Down        = 0x00020000,
	ovrButton_Left        = 0x00040000,
	ovrButton_Right       = 0x00080000,
	ovrButton_Enter       = 0x00100000,	//< Set for touchpad click on the Gear VR and Go Controllers, menu button on Left Quest Controller
	ovrButton_Back        = 0x00200000,	//< Back button on the headset or Gear VR Controller (only set when a short press comes up)
	ovrButton_GripTrigger = 0x04000000,	//< grip trigger engaged
	ovrButton_Trigger     = 0x20000000,	//< Index Trigger engaged
	ovrButton_Joystick    = 0x80000000,	//< Click of the Joystick

	ovrButton_EnumSize  = 0x7fffffff
} ovrButton;


/// Describes touch input types.
/// These values map to capacitive touch values and derived pose states
typedef enum ovrTouch_
{
	ovrTouch_A             = 0x00000001,	//< The A button has a finger resting on it.
	ovrTouch_B             = 0x00000002,	//< The B button has a finger resting on it.
	ovrTouch_X             = 0x00000004,	//< The X button has a finger resting on it.
	ovrTouch_Y             = 0x00000008,	//< The Y button has a finger resting on it.
	ovrTouch_TrackPad      = 0x00000010,	//< The TrackPad has a finger resting on it.
	ovrTouch_Joystick      = 0x00000020,	//< The Joystick has a finger resting on it.
	ovrTouch_IndexTrigger  = 0x00000040,	//< The Index Trigger has a finger resting on it.
	ovrTouch_ThumbUp       = 0x00000100,	//< None of A, B, X, Y, or Joystick has a finger/thumb in proximity to it
	ovrTouch_IndexPointing = 0x00000200,	//< The finger is sufficiently far away from the trigger to not be considered in proximity to it.
	ovrTouch_BaseState     = 0x00000300,	//< No buttons touched or in proximity.  finger pointing and thumb up.
	ovrTouch_LThumb		   = 0x00000400,	//< The Left controller Joystick has a finger/thumb resting on it.
	ovrTouch_RThumb        = 0x00000800,	//< The Right controller Joystick has a finger/thumb resting on it.
	ovrTouch_EnumSize
} ovrTouch;

/// Specifies which controller is connected; multiple can be connected at once.
typedef enum ovrControllerType_
{
	ovrControllerType_None			= 0,
	ovrControllerType_Reserved0		= ( 1 << 0 ),	//< LTouch in CAPI
	ovrControllerType_Reserved1		= ( 1 << 1 ),	//< RTouch in CAPI
	ovrControllerType_TrackedRemote	= ( 1 << 2 ),
	ovrControllerType_Headset		= ( 1 << 3 ),
	ovrControllerType_Gamepad		= ( 1 << 4 ),	//< Xbox in CAPI

	ovrControllerType_EnumSize		= 0x7fffffff
} ovrControllerType;

typedef uint32_t ovrDeviceID;

typedef enum ovrDeviceIdType_
{
	ovrDeviceIdType_Invalid	= 0x7fffffff
} ovrDeviceIdType;

/// This header starts all ovrInputCapabilities structures. It should only hold fields
/// that are common to all input controllers.
typedef struct ovrInputCapabilityHeader_
{
	ovrControllerType	Type;

	/// A unique ID for the input device
	ovrDeviceID			DeviceID;
} ovrInputCapabilityHeader;

/// Specifies capabilites of a controller
/// Note that left and right hand are non-exclusive (a two-handed controller could set both)
typedef enum ovrControllerCapabilities_
{
	ovrControllerCaps_HasOrientationTracking 		= 0x00000001,
	ovrControllerCaps_HasPositionTracking 			= 0x00000002,
	ovrControllerCaps_LeftHand						= 0x00000004,	//< Controller is configured for left hand
	ovrControllerCaps_RightHand						= 0x00000008,	//< Controller is configured for right hand

	ovrControllerCaps_ModelOculusGo					= 0x00000010,	//< Controller for Oculus Go devices

	ovrControllerCaps_HasAnalogIndexTrigger			= 0x00000040,	//< Controller has an analog index trigger vs. a binary one
	ovrControllerCaps_HasAnalogGripTrigger			= 0x00000080,	//< Controller has an analog grip trigger vs. a binary one
	ovrControllerCaps_HasSimpleHapticVibration		= 0x00000200,	//< Controller supports simple haptic vibration
	ovrControllerCaps_HasBufferedHapticVibration	= 0x00000400,	//< Controller supports buffered haptic vibration

	ovrControllerCaps_ModelGearVR					= 0x00000800,	//< Controller is the Gear VR Controller

	ovrControllerCaps_HasTrackpad					= 0x00001000,	//< Controller has a trackpad

	ovrControllerCaps_HasJoystick					= 0x00002000,	//< Controller has a joystick.
	ovrControllerCaps_ModelOculusTouch				= 0x00004000,	//< Oculus Touch Controller For Oculus Quest

	ovrControllerCaps_EnumSize 					= 0x7fffffff
} ovrControllerCapabilties;

/// Details about the Oculus Remote input device.
typedef struct ovrInputTrackedRemoteCapabilities_
{
	ovrInputCapabilityHeader	Header;

	/// Mask of controller capabilities described by ovrControllerCapabilities
	uint32_t					ControllerCapabilities;

	/// Mask of button capabilities described by ovrButton
	uint32_t					ButtonCapabilities;

	/// Maximum coordinates of the Trackpad, bottom right exclusive
	/// For a 300x200 Trackpad, return 299x199
	uint16_t					TrackpadMaxX;
	uint16_t					TrackpadMaxY;

	/// Size of the Trackpad in mm (millimeters)
	float						TrackpadSizeX;
	float						TrackpadSizeY;

	/// added in API version 1.1.13.0
	/// Maximum submittable samples for the haptics buffer
	uint32_t HapticSamplesMax;
	/// length in milliseconds of a sample in the haptics buffer.
	uint32_t HapticSampleDurationMS;
	/// added in API version 1.1.15.0
	uint32_t TouchCapabilities;
	uint32_t Reserved4;
	uint32_t Reserved5;
} ovrInputTrackedRemoteCapabilities;

/// Capabilities for the Head Mounted Tracking device (i.e. the headset).
/// Note that the GearVR headset firmware always sends relative coordinates
/// with the initial touch position offset by (1280,720). There is no way
/// to get purely raw coordinates from the headset. In addition, these
/// coordinates get adjusted for acceleration resulting in a slow movement
/// from one edge to the other the having a coordinate range of about 300
/// units, while a fast movement from edge to edge may result in a range
/// close to 900 units.
/// This means the headset touchpad needs to be handled differently than
/// the GearVR Controller touchpad.
typedef struct ovrInputHeadsetCapabilities_
{
	ovrInputCapabilityHeader	Header;

	/// Mask of controller capabilities described by ovrControllerCapabilities
	uint32_t					ControllerCapabilities;

	/// Mask of button capabilities described by ovrButton
	uint32_t					ButtonCapabilities;

	/// Maximum coordinates of the Trackpad, bottom right exclusive
	/// For a 300x200 Trackpad, return 299x199
	uint16_t					TrackpadMaxX;
	uint16_t					TrackpadMaxY;

	/// Size of the Trackpad in mm (millimeters)
	float						TrackpadSizeX;
	float						TrackpadSizeY;
} ovrInputHeadsetCapabilities;

/// Capabilities for an XBox style game pad
typedef struct ovrInputGamepadCapabilities_
{
	ovrInputCapabilityHeader	Header;

	/// Mask of controller capabilities described by ovrControllerCapabilities
	uint32_t					ControllerCapabilities;

	/// Mask of button capabilities described by ovrButton
	uint32_t					ButtonCapabilities;

	// Reserved for future use.
	uint64_t			Reserved[20];
} ovrInputGamepadCapabilities;


/// The buffer data for playing haptics
typedef struct ovrHapticBuffer_
{
	/// Start time of the buffer
	double						BufferTime;

	/// Number of samples in the buffer;
	uint32_t					NumSamples;

	// True if this is the end of the buffers being sent
	bool						Terminated;

	uint8_t *					HapticBuffer;
} ovrHapticBuffer;

/// This header starts all ovrInputState structures. It should only hold fields
/// that are common to all input controllers.
typedef struct ovrInputStateHeader_
{
	/// Type type of controller
	ovrControllerType 	ControllerType;

	/// System time when the controller state was last updated.
	double				TimeInSeconds;
} ovrInputStateHeader;

/// ovrInputStateTrackedRemote describes the complete input state for the
/// orientation-tracked remote. The TrackpadPosition coordinates returned
/// for the GearVR Controller are in raw, absolute units.
typedef struct ovrInputStateTrackedRemote_
{
	ovrInputStateHeader Header;

	/// Values for buttons described by ovrButton.
	uint32_t	        Buttons;

	/// Finger contact status for trackpad
	/// true = finger is on trackpad, false = finger is off trackpad
	uint32_t            TrackpadStatus;

	/// X and Y coordinates of the Trackpad
	ovrVector2f         TrackpadPosition;

	/// The percentage of max battery charge remaining.
	uint8_t				BatteryPercentRemaining;
	/// Increments every time the remote is recentered. If this changes, the application may need
	/// to adjust its arm model accordingly.
	uint8_t				RecenterCount;
	/// Reserved for future use.
	uint16_t			Reserved;

	/// added in API version 1.1.13.0
	// Analog values from 0.0 - 1.0 of the pull of the triggers
	float IndexTrigger;
	float GripTrigger;

	/// added in API version 1.1.15.0
	uint32_t Touches;
	uint32_t Reserved5a;
	// Analog values from -1.0 - 1.0
	// The value is set to 0.0 on Joystick, if the magnitude of the vector is < 0.1f
	ovrVector2f Joystick;
	// JoystickNoDeadZone does change the raw values of the data.
	ovrVector2f JoystickNoDeadZone;

} ovrInputStateTrackedRemote;


/// ovrInputStateHeadset describes the complete input state for the
/// GearVR headset. The TrackpadPosition coordinates return for the
/// headset are relative coordinates, centered at (1280,720). See the
/// comments on ovrInputHeadsetCapabilities for more information.
typedef struct ovrInputStateHeadset_
{
	ovrInputStateHeader	Header;

	/// Values for buttons described by ovrButton.
	uint32_t			Buttons;

	/// finger contact status for trackpad
	/// true = finger is on trackpad, false = finger is off trackpad
	uint32_t			TrackpadStatus;

	/// X and Y coordinates of the Trackpad
	ovrVector2f			TrackpadPosition;
} ovrInputStateHeadset;

/// ovrInputStateGamepad describes the input state gamepad input devices
typedef struct ovrInputStateGamepad_
{
	ovrInputStateHeader	Header;

	/// Values for buttons described by ovrButton.
	uint32_t			Buttons;

	// Analog value from 0.0 - 1.0 of the pull of the Left Trigger
	float				LeftTrigger;
	// Analog value from 0.0 - 1.0 of the pull of the Right Trigger
	float				RightTrigger;

	/// X and Y coordinates of the Left Joystick, -1.0 - 1.0
	ovrVector2f			LeftJoystick;
	/// X and Y coordinates of the Right Joystick, -1.0 - 1.0
	ovrVector2f			RightJoystick;

	// Reserved for future use.
	uint64_t			Reserved[20];
} ovrInputStateGamepad;


#if defined( __cplusplus )
extern "C" {
#endif

/// Enumerates the input devices connected to the system
/// Start with index=0 and counting up. Stop when ovrResult is < 0
///
/// Input: ovrMobile, device index, and a capabilities header
/// The capabilities header does not need to have any fields set before calling.
/// Output: capabilitiesHeader with information for that enumeration index
OVR_VRAPI_EXPORT ovrResult vrapi_EnumerateInputDevices( ovrMobile * ovr, const uint32_t index, ovrInputCapabilityHeader * capsHeader );

/// Returns the capabilities of the input device for the corresponding device ID
///
/// Input: ovr, pointer to a capabilities structure
/// Output: capabilities will be filled with information for the deviceID
/// Example:
///     The Type field of the capabilitiesHeader must be set when calling this function.
///     Normally the capabilitiesHeader is obtained from the vrapi_EnumerateInputDevices API
///     The Type field in the header should match the structure type that is passed.
///
///         ovrInputCapabilityHeader capsHeader;
///         if ( vrapi_EnumerateInputDevices( ovr, deviceIndex, &capsHeader ) >= 0 ) {
///             if ( capsHeader.Type == ovrDeviceType_TrackedRemote ) {
///                 ovrInputTrackedRemoteCapabilities remoteCaps;
///                 remoteCaps.Header = capsHeader;
///                 vrapi_GetInputDeviceCapabilities( ovr, &remoteCaps.Header );
OVR_VRAPI_EXPORT ovrResult vrapi_GetInputDeviceCapabilities( ovrMobile * ovr, ovrInputCapabilityHeader * capsHeader );

/// Sets the vibration level of a haptic device.
/// there should only be one call to vrapi_SetHapticVibrationSimple or vrapi_SetHapticVibrationBuffer per frame
///  additional calls of either will return ovrError_InvalidOperation and have undefined behavior
/// Input: ovr, deviceID, intensity: 0.0 - 1.0
OVR_VRAPI_EXPORT ovrResult vrapi_SetHapticVibrationSimple( ovrMobile * ovr, const ovrDeviceID deviceID, const float intensity );

/// Fills the haptic vibration buffer of a haptic device
/// there should only be one call to vrapi_SetHapticVibrationSimple or vrapi_SetHapticVibrationBuffer per frame
///  additional calls of either will return ovrError_InvalidOperation and have undefined behavior
/// Input: ovr, deviceID, pointer to a hapticBuffer with filled in data.
OVR_VRAPI_EXPORT ovrResult vrapi_SetHapticVibrationBuffer( ovrMobile * ovr, const ovrDeviceID deviceID, const ovrHapticBuffer * hapticBuffer );

/// Returns the current input state for controllers, without positional tracking info.
///
/// Input: ovr, deviceID, pointer to a capabilities structure (with Type field set)
/// Output: Upon return the inputState structure will be set to the device's current input state
/// Example:
///     The Type field of the passed ovrInputStateHeader must be set to the type that
///     corresponds to the type of structure being passed.
///     The pointer to the ovrInputStateHeader should be a pointer to a Header field in
///     structure matching the value of the Type field.
///
///     ovrInputStateTrackedRemote state;
///     state.Header.Type = ovrControllerType_TrackedRemote;
///     if ( vrapi_GetCurrentInputState( ovr, remoteDeviceID, &state.Header ) >= 0 ) {
OVR_VRAPI_EXPORT ovrResult vrapi_GetCurrentInputState( ovrMobile * ovr, const ovrDeviceID deviceID, ovrInputStateHeader * inputState );


/// Returns the predicted input state based on the specified absolute system time
/// in seconds. Pass absTime value of 0.0 to request the most recent sensor reading.
/// Input: ovr, device ID, prediction time
/// Output: ovrTracking structure containing the device's predicted tracking state.
OVR_VRAPI_EXPORT ovrResult vrapi_GetInputTrackingState( ovrMobile * ovr, const ovrDeviceID deviceID,
														const double absTimeInSeconds, ovrTracking * tracking );

/// Can be called from any thread while in VR mode. Recenters the tracked remote to the current yaw of the headset.
/// Input: ovr, device ID
/// Output: None
OVR_VRAPI_DEPRECATED( OVR_VRAPI_EXPORT void vrapi_RecenterInputPose( ovrMobile * ovr, const ovrDeviceID deviceID ) );

/// Enable or disable emulation for the GearVR Controller.
/// Emulation is false by default.
/// If emulationOn == true, then the back button and touch events on the GearVR Controller will be sent through the Android
/// dispatchKeyEvent and dispatchTouchEvent path as if they were from the headset back button and touchpad.
/// Applications that are intentionally enumerating the controller will likely want to turn emulation off in order
/// to differentiate between controller and headset input events.
OVR_VRAPI_EXPORT ovrResult vrapi_SetRemoteEmulation( ovrMobile * ovr, const bool emulationOn );

#if defined( __cplusplus )
}   // extern "C"
#endif

#endif	// OVR_VrApi_Input_h
