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

#include "Wwise/SimpleExtSrc/WwiseSimpleExtSrcManager.h"
#include "Wwise/SimpleExtSrc/WwiseExternalSourceCookieDefaultMedia.h"
#include "Wwise/SimpleExtSrc/WwiseExternalSourceSettings.h"
#include "Wwise/SimpleExtSrc/WwiseExternalSourceMediaInfo.h"

#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/Stats/SimpleExtSrc.h"
#include "Wwise/WwiseExternalSourceFileState.h"
#include "Wwise/WwiseResourceLoader.h"

#include "Platforms/AkPlatformInfo.h"
#include "UObject/UObjectIterator.h"

#include "AkAudioEvent.h"

#include <inttypes.h>

#if WITH_EDITORONLY_DATA
#include "Wwise/WwiseResourceCooker.h"
#include "Wwise/WwiseProjectDatabase.h"
#endif


FWwiseSimpleExtSrcManager::FWwiseSimpleExtSrcManager()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT(TEXT("FWwiseSimpleExtSrcManager::FWwiseSimpleExtSrcManager"));
#if WITH_EDITOR
	auto* ExtSettings = GetMutableDefault<UWwiseExternalSourceSettings>();
	// When these settings change we will want to reset the External source manager and reload all external sources
	ExtSettingsTableChangedDelegate = ExtSettings->OnTablesChanged.AddRaw(this, &FWwiseSimpleExtSrcManager::OnTablesChanged);
#endif
	LoadMediaTables();
}

FWwiseSimpleExtSrcManager::~FWwiseSimpleExtSrcManager()
{
}

void FWwiseSimpleExtSrcManager::LoadMediaTables()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_3(TEXT("FWwiseSimpleExtSrcManager::LoadMediaTables"));
	const auto ExtSettings = GetDefault<UWwiseExternalSourceSettings>();

	//This implementation of the WwiseExternalSourceManager uses data tables to keep track of all of the external source media in the project.
	//This table could be automatically generated by external scripts.
	//It would also be perfectly valid to use a different type of data structure or a handling class to retrieve external source media information.
	if (ExtSettings->MediaInfoTable.IsValid())
	{
		MediaInfoTable = TStrongObjectPtr<UDataTable>(Cast<UDataTable>(StreamableManager.LoadSynchronous(ExtSettings->MediaInfoTable)));
	}
	else
	{
		MediaInfoTable.Reset();
	}

	//The same goes for this (optional) table that sets the default media to be associated with each external source cookie
	if (ExtSettings->ExternalSourceDefaultMedia.IsValid())
	{
		ExternalSourceDefaultMedia = TStrongObjectPtr<UDataTable>(Cast<UDataTable>(StreamableManager.LoadSynchronous(ExtSettings->ExternalSourceDefaultMedia)));
	}
	else
	{
		ExternalSourceDefaultMedia.Reset();
	}

	if (MediaInfoTable.IsValid())
	{
#if WITH_EDITOR
		MediaInfoTableChangedDelegate = MediaInfoTable->OnDataTableChanged().AddRaw(this, &FWwiseSimpleExtSrcManager::OnMediaInfoTableChanged);
#endif
		FillMediaNameToIdMap(*MediaInfoTable.Get());
	}
	else
	{
		UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("Wwise Simple External Source: Media Info Table is not set. Please set table in the Project Settings."));
	}

	if (ExternalSourceDefaultMedia.IsValid())
	{
#if WITH_EDITOR
		ExternalSourceDefaultMediaTableChangedDelegate = ExternalSourceDefaultMedia->OnDataTableChanged().AddRaw(this, &FWwiseSimpleExtSrcManager::OnDefaultExternalSourceTableChanged);
#endif
		FillExternalSourceToMediaMap(*ExternalSourceDefaultMedia.Get());
	}
	else
	{
		UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("Wwise Simple External Source: External Source Default Media is not set. Please set table in the Project Settings."));
	}
}

void FWwiseSimpleExtSrcManager::ReloadExternalSources()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_3(TEXT("FWwiseSimpleExtSrcManager::ReloadExternalSources"));
	UE_LOG(LogWwiseSimpleExtSrc, Log, TEXT("Wwise Simple External Source: Reloading events with external sources"));

	//Unload all events containing external source data
	TArray<UAkAudioEvent*> EventsToReload;
	for (TObjectIterator<UAkAudioEvent> EventAssetIt; EventAssetIt; ++EventAssetIt)
	{
		if (EventAssetIt->GetAllExternalSources().Num()>0)
		{
			EventAssetIt->UnloadData();
			EventsToReload.Add(*EventAssetIt);
		}
	}

	//Then reload them
	for (UAkAudioEvent* Event : EventsToReload)
	{
		Event->LoadData();
	}

	UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("Wwise Simple External Source: %d events reloaded"), EventsToReload.Num());
}

FString FWwiseSimpleExtSrcManager::GetStagingDirectory() const
{
	return UWwiseExternalSourceSettings::GetExternalSourceStagingDirectory();
}

void FWwiseSimpleExtSrcManager::SetExternalSourceMediaById(const FName& ExternalSourceName, const int32 MediaId)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT(TEXT("FWwiseSimpleExtSrcManager::SetExternalSourceMediaById"));
	AkUInt32 Cookie = FAkAudioDevice::GetShortIDFromString(ExternalSourceName.ToString());
	SetExternalSourceMedia(Cookie, MediaId, ExternalSourceName);
}

void FWwiseSimpleExtSrcManager::SetExternalSourceMediaByName(const FName& ExternalSourceName, const FName& MediaName)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT(TEXT("FWwiseSimpleExtSrcManager::SetExternalSourceMediaByName"));
	AkUInt32 Cookie = FAkAudioDevice::GetShortIDFromString(ExternalSourceName.ToString());

	if (const uint32* MediaId = MediaNameToId.Find(MediaName))
	{
		SetExternalSourceMedia(Cookie, *MediaId, ExternalSourceName);
		return;
	}

	UE_LOG(LogWwiseSimpleExtSrc, Error, TEXT("Did not find media with name %s in MediaNameToId map."), *MediaName.ToString());
}

void FWwiseSimpleExtSrcManager::SetExternalSourceMediaWithIds(const int32 ExternalSourceId, const int32 MediaId)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT(TEXT("FWwiseSimpleExtSrcManager::SetExternalSourceMediaWithIds"));
	SetExternalSourceMedia(ExternalSourceId, MediaId);
}

#if WITH_EDITORONLY_DATA
//This is called once per external source 
void FWwiseSimpleExtSrcManager::Cook(IWwiseResourceCooker& InResourceCooker, const FWwiseExternalSourceCookedData& InCookedData,
                                     const TCHAR* PackageFilename,
                                     const TFunctionRef<void(const TCHAR* Filename, void* Data, int64 Size)>& WriteAdditionalFile, const FWwiseSharedPlatformId& InPlatform, const FWwiseSharedLanguageId& InLanguage)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::Cook"));
	if (LIKELY(bCooked))
	{
		UE_LOG(LogWwiseSimpleExtSrc, VeryVerbose, TEXT("FillExternalSourceToMediaMap: Media already packaged, Skipping media cook."))
			return;
	}
	bCooked = true;

	const FString Context = TEXT("Iterating over default media");
	MediaInfoTable->ForeachRow<FWwiseExternalSourceMediaInfo>(Context,
		[this, &InResourceCooker, PackageFilename, &WriteAdditionalFile](const FName& Key, const FWwiseExternalSourceMediaInfo& MediaInfo) {
		FWwisePackagedFile PackagedFile;
		const auto MediaName = MediaInfo.MediaName;
		if (UNLIKELY(MediaName.IsNone()))
		{
			return;
		}
		PackagedFile.PackagingStrategy = EWwisePackagingStrategy::AdditionalFile;
		PackagedFile.PathName = FName(GetStagingDirectory() / MediaName.ToString());
		PackagedFile.SourcePathName = GetExternalSourcePathFor(MediaName);
			
		InResourceCooker.CookFileToSandbox(PackagedFile, PackageFilename, WriteAdditionalFile);
	});
}
#endif


void FWwiseSimpleExtSrcManager::LoadExternalSourceMedia(const uint32 InExternalSourceCookie,
	const FName& InExternalSourceName,
	FLoadExternalSourceCallback&& InCallback)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::LoadExternalSourceMedia"));
	uint32 MediaId;
	{
		FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_ReadOnly);
		const uint32* MediaIdPtr = CookieToMediaId.Find(InExternalSourceCookie);
		if (UNLIKELY(!MediaIdPtr))
		{
			UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("LoadExternalSourceMedia: No media has been associated with External Source %" PRIu32 " (%s). No media will be loaded until the media is set."),
				InExternalSourceCookie, *InExternalSourceName.ToString());
			InCallback(true);
			return;
		}
		MediaId = *MediaIdPtr;
	}

	int Count;
	{
		FRWScopeLock StateLock(FileStatesByIdLock, FRWScopeLockType::SLT_Write);
		if (auto* LoadCountPtr = CookieLoadCount.Find(InExternalSourceCookie))
		{
			Count = ++*LoadCountPtr;
		}
		else
		{
			CookieLoadCount.Add(InExternalSourceCookie, 1);
			Count = 1;
		}
	}

	UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("Loading External Source %" PRIu32 " (%s) Media %" PRIu32 " : ++%d Cookie LoadCount"),
		InExternalSourceCookie, *InExternalSourceName.ToString(), MediaId, Count);

	IncrementFileStateUse(MediaId, EWwiseFileStateOperationOrigin::Loading,
		[this, MediaId]() mutable -> FWwiseFileStateSharedPtr
	{
		const FName RowName = FName(FString::FromInt(MediaId));
		const FString Context = TEXT("Find media info");
		if (UNLIKELY(!MediaInfoTable.IsValid()))
		{
			UE_LOG(LogWwiseSimpleExtSrc, Error, TEXT("Cannot read External Source Media information because datatable asset has not been loaded."));
			return {};
		}
		else if (const FWwiseExternalSourceMediaInfo* ExternalSourceMediaInfoEntry = MediaInfoTable->FindRow<FWwiseExternalSourceMediaInfo>(RowName, Context))
		{
			return CreateOp(*ExternalSourceMediaInfoEntry);
		}
		else
		{
			UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("LoadExternalSourceMedia: Could not find media info table entry for media id %" PRIu32), MediaId);
			return {};
		}
	}, [this, InExternalSourceCookie, MediaId, InCallback = MoveTemp(InCallback)](const FWwiseFileStateSharedPtr, bool bInResult) mutable
	{
		if (UNLIKELY(!bInResult))
		{
			InCallback(false);
			return;
		}
		FWwiseFileStateSharedPtr State;
		{
			FRWScopeLock StateLock(FileStatesByIdLock, FRWScopeLockType::SLT_ReadOnly);
			const auto* StatePtr = FileStatesById.Find(MediaId);
			if (UNLIKELY(!StatePtr || !StatePtr->IsValid()))
			{
				UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("LoadExternalSourceMedia: Getting external source media state %" PRIu32 " failed after successful IncrementFileStateUse."), MediaId);
				InCallback(false);
				return;
			}
			State = *StatePtr;
		}
		auto* ExternalSourceFileState = State->GetStateAs<FWwiseExternalSourceFileState>();
		if (UNLIKELY(!ExternalSourceFileState))
		{
			UE_LOG(LogWwiseSimpleExtSrc, Error, TEXT("LoadExternalSourceMedia: Getting external source media %" PRIu32 ": Could not cast to ExternalSourceState"), MediaId);
			InCallback(false);
			return;
		}

		{
			FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_Write);
			UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("Binding Cookie %" PRIu32 " to media %" PRIu32 "."), InExternalSourceCookie, MediaId);
			CookieToMedia.Add(InExternalSourceCookie, ExternalSourceFileState);
		}
		InCallback(true);
	});
}

void FWwiseSimpleExtSrcManager::UnloadExternalSourceMedia(const uint32 InExternalSourceCookie,
	const FName& InExternalSourceName,
	FUnloadExternalSourceCallback&& InCallback)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::UnloadExternalSourceMedia"));
	uint32 MediaId;
	{
		FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_ReadOnly);
		const uint32* MediaIdPtr = CookieToMediaId.Find(InExternalSourceCookie);
		if (UNLIKELY(!MediaIdPtr))
		{
			UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("UnloadExternalSourceMedia: No media has been associated with External Source %" PRIu32 " (%s). No media will be unloaded."),
				InExternalSourceCookie, *InExternalSourceName.ToString());
			InCallback();
			return;
		}
		MediaId = *MediaIdPtr;
	}

	int Count;
	{
		FRWScopeLock StateLock(FileStatesByIdLock, FRWScopeLockType::SLT_Write);
		if (auto* LoadCountPtr = CookieLoadCount.Find(InExternalSourceCookie))
		{
			if (LIKELY(*LoadCountPtr > 0))
			{
				Count = --*LoadCountPtr;
			}
			else
			{
				UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("UnloadExternalSourceMedia: Unloading unloaded External Source %" PRIu32 " (%s)."),
					InExternalSourceCookie, *InExternalSourceName.ToString());
				InCallback();
				return;
			}
		}
		else
		{
			UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("UnloadExternalSourceMedia: Unloading unknown External Source %" PRIu32 " (%s)."),
				InExternalSourceCookie, *InExternalSourceName.ToString());
			InCallback();
			return;
		}
	}

	UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("Unloading External Source %" PRIu32 " (%s) Media %" PRIu32 ": --%d Cookie LoadCount"),
		InExternalSourceCookie, *InExternalSourceName.ToString(), MediaId, Count);

	DecrementFileStateUse(MediaId, nullptr, EWwiseFileStateOperationOrigin::Loading, MoveTemp(InCallback));
}

void FWwiseSimpleExtSrcManager::OnTablesChanged()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::OnTablesChanged"));
	if (MediaInfoTable.IsValid())
	{
		if (MediaInfoTable->OnDataTableChanged().IsBoundToObject(this))
		{
			MediaInfoTable->OnDataTableChanged().RemoveAll(this);
		}
	}

	if (ExternalSourceDefaultMedia.IsValid())
	{
		if (ExternalSourceDefaultMedia->OnDataTableChanged().IsBoundToObject(this))
		{
			ExternalSourceDefaultMedia->OnDataTableChanged().RemoveAll(this);
		}
	}

	UE_LOG(LogWwiseSimpleExtSrc, Log, TEXT("Wwise Simple External Source: Change in external source tables settings detected. Reloading external source tables and events."));
	LoadMediaTables();
	ReloadExternalSources();
}

void FWwiseSimpleExtSrcManager::OnMediaInfoTableChanged()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::OnMediaInfoTableChanged"));
	if (!MediaInfoTable.IsValid())
	{
		return;
	}

	UE_LOG(LogWwiseSimpleExtSrc, Log, TEXT("Wwise Simple External Source: Change in MediaInfoTable detected. Media name map will be refreshed and events with external sources will be reloaded."));
	FillMediaNameToIdMap(*MediaInfoTable.Get());
	ReloadExternalSources();
}

void FWwiseSimpleExtSrcManager::OnDefaultExternalSourceTableChanged()
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::OnDefaultExternalSourceTableChanged"));
	if (!ExternalSourceDefaultMedia.IsValid())
	{
		return;
	}

	UE_LOG(LogWwiseSimpleExtSrc, Log, TEXT("Wwise Simple External Source: Change in ExternalSourceDefaultMedia detected. External source cookie to media Id map will be refreshed and events with external sources will be reloaded."));
	FillExternalSourceToMediaMap(*ExternalSourceDefaultMedia.Get());
	ReloadExternalSources();
}

//It is possible for this to start empty, and for all media mappings to be set in blueprints
void FWwiseSimpleExtSrcManager::FillExternalSourceToMediaMap(const UDataTable& InMappingTable)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_3(TEXT("FWwiseSimpleExtSrcManager::FillExternalSourceToMediaMap"));
	FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_Write);
	if (CookieToMediaId.Num() > 0)
	{
		UE_LOG(LogWwiseSimpleExtSrc, VeryVerbose, TEXT("FillExternalSourceToMediaMap: Emptying external source to media map"));
		CookieToMediaId.Empty();
	}

	FString Context = TEXT("Iterating over default media");
	UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("FillExternalSourceToMediaMap: Filling external source to media map"));

	InMappingTable.ForeachRow<FWwiseExternalSourceCookieDefaultMedia>(Context,
		[this](const FName& Key, const FWwiseExternalSourceCookieDefaultMedia& Value)
		{
			UE_LOG(LogWwiseSimpleExtSrc, VeryVerbose, TEXT("FillExternalSourceToMediaMap : External source %" PRIu32 " (%s) mapped to media %" PRIu32 " (%s)"),
				Value.ExternalSourceCookie, *Value.ExternalSourceName, Value.MediaInfoId, *Value.MediaName);
			CookieToMediaId.Add((uint32)Value.ExternalSourceCookie, Value.MediaInfoId);
		}
	);
}

// This is one way to make setting external source media by name work.
// An alternative approach would be to use a different structure than FWwiseExternalSourceMediaInfo where the media name is the "name" field in the data table (which is used for lookup),
// The media ID could then be generated dynamically using a hashing function such as FAkAudioDevice::GetShortIdFromString.
// In this case, we keep things simple by explicitly stating both the ID and media name in our Media Info Table.
void FWwiseSimpleExtSrcManager::FillMediaNameToIdMap(const UDataTable& InMediaTable)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_3(TEXT("FWwiseSimpleExtSrcManager::FillMediaNameToIdMap"));
	if (MediaNameToId.Num() > 0)
	{
		UE_LOG(LogWwiseSimpleExtSrc, VeryVerbose, TEXT("FillMediaNameToIdMap: Emptying Media Name To Id map"));
		MediaNameToId.Empty();
	}

	FString Context = TEXT("Iterating over default media");
	UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("FillMediaNameToIdMap: Filling Media Name To Id map"));

	InMediaTable.ForeachRow<FWwiseExternalSourceMediaInfo>(Context,
		[this](const FName& Key, const FWwiseExternalSourceMediaInfo& Value)
		{
			UE_LOG(LogWwiseSimpleExtSrc, VeryVerbose, TEXT("FillMediaNameToIdMap: Adding media entry %" PRIu32 ": %s"),
				Value.ExternalSourceMediaInfoId, *Value.MediaName.ToString());

			if (UNLIKELY(MediaNameToId.Contains(Value.MediaName)))
			{
				UE_LOG(LogWwiseSimpleExtSrc, Warning, TEXT("FillMediaNameToIdMap: MediaNameToId already contains entry for %s mapped to ID %" PRIu32 ". It will not be updated."),
					*Value.MediaName.ToString(), Value.ExternalSourceMediaInfoId);
				return;
			}

			MediaNameToId.Add(Value.MediaName, Value.ExternalSourceMediaInfoId);
		}
	);
}

FWwiseFileStateSharedPtr FWwiseSimpleExtSrcManager::CreateOp(const FWwiseExternalSourceMediaInfo& ExternalSourceMediaInfo)
{
	if (ExternalSourceMediaInfo.bIsStreamed)
	{
		return FWwiseFileStateSharedPtr(new FWwiseStreamedExternalSourceFileState(
			ExternalSourceMediaInfo.MemoryAlignment,
			ExternalSourceMediaInfo.bUseDeviceMemory,
			ExternalSourceMediaInfo.PrefetchSize,
			StreamingGranularity,
			ExternalSourceMediaInfo.ExternalSourceMediaInfoId,
			ExternalSourceMediaInfo.MediaName,
			ExternalSourceMediaInfo.CodecID));
	}
	else
	{
		return FWwiseFileStateSharedPtr(new FWwiseInMemoryExternalSourceFileState(
			ExternalSourceMediaInfo.MemoryAlignment,
			ExternalSourceMediaInfo.bUseDeviceMemory,
			ExternalSourceMediaInfo.ExternalSourceMediaInfoId,
			ExternalSourceMediaInfo.MediaName,
			ExternalSourceMediaInfo.CodecID));
	}
}

void FWwiseSimpleExtSrcManager::SetExternalSourceMedia(const uint32 ExternalSourceCookie, const uint32 MediaInfoId, const FName& ExternalSourceName)
{
	SCOPED_WWISESIMPLEEXTERNALSOURCE_EVENT_2(TEXT("FWwiseSimpleExtSrcManager::SetExternalSourceMedia"));
	FEventRef Completed;
	FileHandlerExecutionQueue.Async(WWISESIMPLEEXTERNALSOURCE_ASYNC_NAME("FWwiseSimpleExtSrcManager::SetExternalSourceMedia Async"), [this, ExternalSourceCookie, MediaInfoId, ExternalSourceName, &Completed]() mutable
	{
		// Special case for ID 0: We assume we want to reset to no media ID. So it's merely removed.
		const bool bResetCookie = (MediaInfoId == 0);
 
		if (!MediaInfoTable.IsValid())
		{
			UE_LOG(LogWwiseSimpleExtSrc, Error, TEXT("Cannot read External Source Media information because datatable asset has not yet been loaded."));
			Completed->Trigger();
			return;
		}

		FString LogExternalSourceName = ExternalSourceName.ToString();
		
		const FWwiseExternalSourceMediaInfo* ExternalSourceMediaInfo;
		if (bResetCookie)
		{
			ExternalSourceMediaInfo = nullptr;
		}
		else
		{
			const FName RowName = FName(FString::FromInt(MediaInfoId));
			const FString Context = TEXT("Find external source media");
			ExternalSourceMediaInfo = MediaInfoTable->FindRow<FWwiseExternalSourceMediaInfo>(RowName, Context);			

			if (!ExternalSourceMediaInfo)
			{
				UE_LOG(LogWwiseSimpleExtSrc, Error, TEXT("Could not find media entry with id %" PRIu32 " in ExternalSourceMedia datatable."), MediaInfoId);
				Completed->Trigger();
				return;
			}
		}
		
		int ExternalSourceLoadCount = 0;
		TWwiseFuture<void> UnloadFuture;
		if (ExternalSourceStatesById.Contains(ExternalSourceCookie))
		{
			ExternalSourceLoadCount = 1;		// Temporary, potential value
			const auto ExternalSourceCookedData = ExternalSourceStatesById.FindRef(ExternalSourceCookie);
			LogExternalSourceName = ExternalSourceCookedData->DebugName.ToString();
		}

		if (ExternalSourceLoadCount)
		{
			bool bPreviousMediaExists = false;
			uint32 FoundMedia{ 0 };
			{
				FRWScopeLock StateLock(FileStatesByIdLock, FRWScopeLockType::SLT_ReadOnly);
				if (const auto* MediaIdPtr = CookieToMediaId.Find(ExternalSourceCookie))
				{
					bPreviousMediaExists = true;
					FoundMedia = *MediaIdPtr;
				}
				if (const auto* LoadCountPtr = CookieLoadCount.Find(ExternalSourceCookie))
				{
					ExternalSourceLoadCount = *LoadCountPtr; 
				}
				else
				{
					ExternalSourceLoadCount = 0;
				}
			}

			if (bPreviousMediaExists && FoundMedia == MediaInfoId)
			{
				UE_CLOG(!bResetCookie, LogWwiseSimpleExtSrc, VeryVerbose, TEXT("SetExternalSourceMedia: MediaInfoId for %" PRIu32 " (%s) was already set to %" PRIu32 " (%s). Nothing to do."),
					ExternalSourceCookie, *ExternalSourceName.ToString(), MediaInfoId, *ExternalSourceMediaInfo->MediaName.ToString());
				Completed->Trigger();
				return;
			}

			if (bPreviousMediaExists)
			{
				UE_CLOG(ExternalSourceLoadCount > 0, LogWwiseSimpleExtSrc, Verbose, TEXT("SetExternalSourceMedia: MediaInfoId for %" PRIu32 " (%s) is used %" PRIu32 " times. Reloading all instances."),
					ExternalSourceCookie, *ExternalSourceName.ToString(), ExternalSourceLoadCount);
				UE_CLOG(ExternalSourceLoadCount == 0, LogWwiseSimpleExtSrc, Verbose, TEXT("SetExternalSourceMedia: MediaInfoId for %" PRIu32 " (%s) was not used yet."),
					ExternalSourceCookie, *ExternalSourceName.ToString());
				for (int i = ExternalSourceLoadCount-1; i >= 0; --i)
				{
					if (i == 0)
					{
						TWwisePromise<void> UnloadPromise;
						if (FPlatformProcess::SupportsMultithreading())
						{
							UnloadFuture = UnloadPromise.GetFuture();
						}
						UnloadExternalSourceMedia(ExternalSourceCookie, ExternalSourceName,
							[UnloadPromise = MoveTemp(UnloadPromise)]() mutable
							{
								UnloadPromise.EmplaceValue();
							});
					}
					else
					{
						UnloadExternalSourceMedia(ExternalSourceCookie, ExternalSourceName, []{});
					}
				}
			}
		}

		if (bResetCookie)
		{
			{
				FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_Write);
				CookieToMediaId.Remove(ExternalSourceCookie);
			}
			Completed->Trigger();
			return;
		}

		{
			FRWScopeLock Lock(CookieToMediaLock, FRWScopeLockType::SLT_Write);
			CookieToMediaId.Add(ExternalSourceCookie, MediaInfoId);
		}

		//We don't want to load the media if the external source with this cookie is not yet loaded
		if (!ExternalSourceLoadCount)
		{
			UE_LOG(LogWwiseSimpleExtSrc, Verbose, TEXT("SetExternalSourceMedia: Media %" PRIu32 " (%s) will be loaded when the external source %" PRIu32 " (%s) is loaded."),
				MediaInfoId, *ExternalSourceMediaInfo->MediaName.ToString(), ExternalSourceCookie, *ExternalSourceName.ToString())
			Completed->Trigger();
			return;
		}

		for (int i = ExternalSourceLoadCount-1; i >= 0; --i)
		{
			if (i == 0)
			{
				LoadExternalSourceMedia(ExternalSourceCookie, ExternalSourceName, [&Completed, UnloadFuture = MoveTemp(UnloadFuture)](bool)
				{
					UnloadFuture.Wait();
					Completed->Trigger();
				});
			}
			else
			{
				LoadExternalSourceMedia(ExternalSourceCookie, ExternalSourceName, [&Completed](bool) {});
			}
		}
	});

	Completed->Wait();
}
