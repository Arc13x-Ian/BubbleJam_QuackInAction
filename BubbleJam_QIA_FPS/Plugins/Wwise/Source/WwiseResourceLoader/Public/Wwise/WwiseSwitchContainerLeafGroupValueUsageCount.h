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

#pragma once

#include "Wwise/CookedData/WwiseSwitchContainerLeafCookedData.h"

struct WWISERESOURCELOADER_API FWwiseSwitchContainerLeafGroupValueUsageCount
{
	/**
	 * @brief SwitchContainer Leaf this structure represents.
	*/
	const FWwiseSwitchContainerLeafCookedData& Key;

	/**
	 * @brief Number of GroupValues present in this key that are already loaded.
	*/
	TSet<FWwiseGroupValueCookedData> LoadedGroupValues;

	/**
	 * @brief Resources represented by the Key that were successfully loaded.
	*/
	struct WWISERESOURCELOADER_API FLoadedData
	{
		FLoadedData();
		TArray<const FWwiseSoundBankCookedData*> LoadedSoundBanks;
		TArray<const FWwiseExternalSourceCookedData*> LoadedExternalSources;
		TArray<const FWwiseMediaCookedData*> LoadedMedia;
		int IsProcessing{0};
		bool IsLoaded() const;
	} LoadedData;

	FWwiseSwitchContainerLeafGroupValueUsageCount(const FWwiseSwitchContainerLeafCookedData& InKey);

	bool HaveAllKeys() const;
};
