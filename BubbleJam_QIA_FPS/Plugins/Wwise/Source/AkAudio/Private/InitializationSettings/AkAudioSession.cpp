/*******************************************************************************
The content of this file includes portions of the proprietary AUDIOKINETIC Wwise
Technology released in source code form as part of the game integration package.
The content of this file may not be used without valid licenses to the
AUDIOKINETIC Wwise Technology.
Note that the use of the game engine is subject to the Unreal(R) Engine End User
License Agreement at https://www.unrealengine.com/en-US/eula/unreal
 
License Usage
 
Licensees holding valid licenses to the AUDIOKINETIC Wwise Technology may use
this file in accordance with the end user license agreement provided with the
software or, alternatively, in accordance with the terms contained
in a written agreement between you and Audiokinetic Inc.
Copyright (c) 2024 Audiokinetic Inc.
*******************************************************************************/

#include "InitializationSettings/AkAudioSession.h"
#include "InitializationSettings/AkInitializationSettings.h"

void FAkAudioSession::FillInitializationStructure(FAkInitializationStructure& InitializationStructure) const
{
#if PLATFORM_IOS
    InitializationStructure.PlatformInitSettings.audioSession.eCategory = (AkAudioSessionCategory)AudioSessionCategory;
    InitializationStructure.PlatformInitSettings.audioSession.eCategoryOptions = (AkAudioSessionCategoryOptions)AudioSessionCategoryOptions;
    InitializationStructure.PlatformInitSettings.audioSession.eMode = (AkAudioSessionMode)AudioSessionMode;

#if WWISE_2024_1_OR_LATER
    InitializationStructure.PlatformInitSettings.audioSession.eRouteSharingPolicy = (AkAudioSessionRouteSharingPolicy)AudioSessionRouteSharingPolicy;
#endif
#endif
}

