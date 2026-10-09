/*
	openbv: FMOD 3.75's fmod_errors.h (FMOD_ErrorString), for the FMOD shim.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#ifndef _FMOD_ERRORS_H
#define _FMOD_ERRORS_H

#include "fmod.h"

static const char *FMOD_ErrorString(int errcode)
{
	switch (errcode)
	{
	case FMOD_ERR_NONE:             return "No errors";
	case FMOD_ERR_BUSY:             return "Cannot call this command after FSOUND_Init.  Call FSOUND_Close first.";
	case FMOD_ERR_UNINITIALIZED:    return "This command failed because FSOUND_Init was not called";
	case FMOD_ERR_PLAY:             return "Playing the sound failed.";
	case FMOD_ERR_INIT:             return "Error initializing output device.";
	case FMOD_ERR_ALLOCATED:        return "The output device is already in use and cannot be reused.";
	case FMOD_ERR_OUTPUT_FORMAT:    return "Soundcard does not support the features needed for this soundsystem (16bit stereo output)";
	case FMOD_ERR_COOPERATIVELEVEL: return "Error setting cooperative level for hardware.";
	case FMOD_ERR_CREATEBUFFER:     return "Error creating hardware sound buffer.";
	case FMOD_ERR_FILE_NOTFOUND:    return "File not found";
	case FMOD_ERR_FILE_FORMAT:      return "Unknown file format";
	case FMOD_ERR_FILE_BAD:         return "Error loading file";
	case FMOD_ERR_MEMORY:           return "Not enough memory or resources";
	case FMOD_ERR_VERSION:          return "The version number of this file format is not supported";
	case FMOD_ERR_INVALID_PARAM:    return "An invalid parameter was passed to this function";
	case FMOD_ERR_NO_EAX:           return "Tried to use an EAX command on a non EAX enabled channel or output.";
	case FMOD_ERR_CHANNEL_ALLOC:    return "Failed to allocate a new channel";
	case FMOD_ERR_RECORD:           return "Recording is not supported on this machine";
	case FMOD_ERR_MEDIAPLAYER:      return "Required Mediaplayer codec is not installed";
	case FMOD_ERR_CDDEVICE:         return "An error occured trying to open the specified CD device";
	default:                        return "Unrecognized error code";
	}
}

#endif
