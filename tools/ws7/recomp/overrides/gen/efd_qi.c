// Adapters of override module `efd_qi` (generated, checked in).
// IBase::QueryInterface (vtable slots 4 and 5) of 296 efd classes: F_<addr> -> rsg_efd_query_interface.
#include "rc_ov.h"
#include "rs_efd.h"

static inline void qi(rc_ctx *c, uint64_t desc, int64_t adj) {
  OV_ARGS(a, c);
  uint64_t self = OV_U64(a);
  uint32_t id = OV_U32(a);
  void **out = OV_PTR(a, void *);
  bool r = rsg_efd_query_interface((const void *)(uintptr_t)RC_A(desc), (void *)(uintptr_t)(self + adj), id, out);
  OV_RET_I32(c, r);
}

/* GRCamera::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCamera_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCamera_class_info), 0); }
/* GRCamera::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCamera_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCamera_class_info), 0); }
/* GRCameraBackground::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBackground_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBackground_class_info), 0); }
/* GRCameraBackground::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBackground_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBackground_class_info), 0); }
/* GRCameraBGBehavior_Animated::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Animated_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Animated_class_info), 0); }
/* GRCameraBGBehavior_Animated::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Animated_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Animated_class_info), 0); }
/* GRCameraBGBehavior_BuildUp::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_BuildUp_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_BuildUp_class_info), 0); }
/* GRCameraBGBehavior_BuildUp::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_BuildUp_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_BuildUp_class_info), 0); }
/* GRCameraBGBehavior_Customize::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Customize_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Customize_class_info), 0); }
/* GRCameraBGBehavior_Customize::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Customize_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Customize_class_info), 0); }
/* GRCameraBGBehavior_Debug::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Debug_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Debug_class_info), 0); }
/* GRCameraBGBehavior_Debug::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Debug_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Debug_class_info), 0); }
/* GRCameraBGBehavior_Panning::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Panning_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Panning_class_info), 0); }
/* GRCameraBGBehavior_Panning::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraBGBehavior_Panning_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraBGBehavior_Panning_class_info), 0); }
/* CameraSong::QueryInterface: class-info table, this -> object +0 */
void RC_FN(CameraSong_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(CameraSong_class_info), 0); }
/* CameraSong::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(CameraSong_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(CameraSong_class_info), 0); }
/* GRCameraDebug::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraDebug_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraDebug_class_info), 0); }
/* GRCameraDebug::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraDebug_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraDebug_class_info), 0); }
/* GRCameraScripted::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraScripted_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraScripted_class_info), 0); }
/* GRCameraScripted::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraScripted_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraScripted_class_info), 0); }
/* GRCameraService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraService_class_info), 0); }
/* GRCameraService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraService_class_info), 0); }
/* IGRCameraImpl::QueryInterface: class-info table, this -> object +0 */
void RC_FN(IGRCameraImpl_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(IGRCameraImpl_class_info), 0); }
/* IGRCameraImpl::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(IGRCameraImpl_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(IGRCameraImpl_class_info), 0); }
/* DatabaseCommandQueue::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DatabaseCommandQueue_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseCommandQueue_class_info), 0); }
/* DatabaseCommandQueue::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DatabaseCommandQueue_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseCommandQueue_class_info), 0); }
/* DatabaseMessageSystem::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DatabaseMessageSystem_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseMessageSystem_class_info), 0); }
/* DatabaseMessageSystem::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DatabaseMessageSystem_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseMessageSystem_class_info), 0); }
/* DatabaseService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DatabaseService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseService_class_info), 0); }
/* DatabaseService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DatabaseService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DatabaseService_class_info), 0); }
/* GRCrowdBox::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCrowdBox_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdBox_class_info), 0); }
/* GRCrowdBox::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCrowdBox_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdBox_class_info), 0); }
/* GRCrowdSystem::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCrowdSystem_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdSystem_class_info), 0); }
/* GRCrowdSystem::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCrowdSystem_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdSystem_class_info), 0); }
/* GRLightService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLightService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLightService_class_info), 0); }
/* GRLightService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLightService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLightService_class_info), 0); }
/* GRMusicService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRMusicService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRMusicService_class_info), 0); }
/* GRMusicService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRMusicService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRMusicService_class_info), 0); }
/* JSONFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(JSONFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory_class_info), 0); }
/* JSONFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(JSONFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory_class_info), 0); }
/* JSONFactory::Response::QueryInterface: class-info table, this -> object +0 */
void RC_FN(JSONFactory__Response_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory__Response_class_info), 0); }
/* JSONFactory::Response::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(JSONFactory__Response_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory__Response_class_info), 0); }
/* PostEffectColorCorrect::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PostEffectColorCorrect_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PostEffectColorCorrect_class_info), 0); }
/* PostEffectColorCorrect::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PostEffectColorCorrect_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PostEffectColorCorrect_class_info), 0); }
/* PostEffectsService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PostEffectsService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PostEffectsService_class_info), 0); }
/* PostEffectsService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PostEffectsService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PostEffectsService_class_info), 0); }
/* GRClipFadeEffectComponent::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRClipFadeEffectComponent_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRClipFadeEffectComponent_class_info), 0); }
/* GRClipFadeEffectComponent::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRClipFadeEffectComponent_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRClipFadeEffectComponent_class_info), 0); }
/* GRDisplayRatioAdjuster::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRDisplayRatioAdjuster_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRDisplayRatioAdjuster_class_info), 0); }
/* GRDisplayRatioAdjuster::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRDisplayRatioAdjuster_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRDisplayRatioAdjuster_class_info), 0); }
/* GRShowLightsSystem::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRShowLightsSystem_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRShowLightsSystem_class_info), 0); }
/* GRShowLightsSystem::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRShowLightsSystem_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRShowLightsSystem_class_info), 0); }
/* GRVenueMenuPresentation_Flow::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRVenueMenuPresentation_Flow_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRVenueMenuPresentation_Flow_class_info), 0); }
/* GRVenueMenuPresentation_Flow::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRVenueMenuPresentation_Flow_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRVenueMenuPresentation_Flow_class_info), 0); }
/* MainService_MacOS::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MainService_MacOS_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MainService_MacOS_class_info), 0); }
/* MainService_MacOS::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MainService_MacOS_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MainService_MacOS_class_info), 0); }
/* NSP_PlayListService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NSP_PlayListService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NSP_PlayListService_class_info), 0); }
/* NSP_PlayListService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NSP_PlayListService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NSP_PlayListService_class_info), 0); }
/* RSLogger::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSLogger_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSLogger_class_info), 0); }
/* RSLogger::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSLogger_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSLogger_class_info), 0); }
/* RSDejaOutDestination::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSDejaOutDestination_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSDejaOutDestination_class_info), 0); }
/* RSDejaOutDestination::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSDejaOutDestination_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSDejaOutDestination_class_info), 0); }
/* GuitarService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GuitarService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GuitarService_class_info), 0); }
/* GuitarService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GuitarService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GuitarService_class_info), 0); }
/* GRLocalization::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLocalization_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLocalization_class_info), 0); }
/* GRLocalization::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLocalization_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLocalization_class_info), 0); }
/* GRSongDirectory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRSongDirectory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRSongDirectory_class_info), 0); }
/* GRSongDirectory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRSongDirectory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRSongDirectory_class_info), 0); }
/* DetectionService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DetectionService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DetectionService_class_info), 0); }
/* DetectionService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DetectionService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DetectionService_class_info), 0); }
/* VenueService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(VenueService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(VenueService_class_info), 0); }
/* VenueService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(VenueService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(VenueService_class_info), 0); }
/* GameService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GameService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GameService_class_info), 0); }
/* GameService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GameService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GameService_class_info), 0); }
/* GRAlphaSortingComponent::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRAlphaSortingComponent_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRAlphaSortingComponent_class_info), 0); }
/* GRAlphaSortingComponent::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRAlphaSortingComponent_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRAlphaSortingComponent_class_info), 0); }
/* GRBackground::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRBackground_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRBackground_class_info), 0); }
/* GRBackground::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRBackground_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRBackground_class_info), 0); }
/* GRBackgroundService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRBackgroundService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRBackgroundService_class_info), 0); }
/* GRBackgroundService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRBackgroundService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRBackgroundService_class_info), 0); }
/* GRObjectEffectComponent::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRObjectEffectComponent_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRObjectEffectComponent_class_info), 0); }
/* GRObjectEffectComponent::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRObjectEffectComponent_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRObjectEffectComponent_class_info), 0); }
/* GRWorldTextManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRWorldTextManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRWorldTextManager_class_info), 0); }
/* GRWorldTextManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRWorldTextManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRWorldTextManager_class_info), 0); }
/* GraphAssetFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GraphAssetFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GraphAssetFactory_class_info), 0); }
/* GraphAssetFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GraphAssetFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GraphAssetFactory_class_info), 0); }
/* GraphAssetFactory::Response::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GraphAssetFactory__Response_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GraphAssetFactory__Response_class_info), 0); }
/* GraphAssetFactory::Response::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GraphAssetFactory__Response_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GraphAssetFactory__Response_class_info), 0); }
/* GRComponent::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRComponent_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRComponent_class_info), 0); }
/* GRComponent::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRComponent_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRComponent_class_info), 0); }
/* MainService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MainService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MainService_class_info), 0); }
/* MainService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MainService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MainService_class_info), 0); }
/* PreloadHelper::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PreloadHelper_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelper_class_info), 0); }
/* PreloadHelper::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PreloadHelper_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelper_class_info), 0); }
/* PreloadHelperResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PreloadHelperResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelperResponse_class_info), 0); }
/* PreloadHelperResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PreloadHelperResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelperResponse_class_info), 0); }
/* PreloadItem::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PreloadItem_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PreloadItem_class_info), 0); }
/* PreloadItem::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PreloadItem_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PreloadItem_class_info), 0); }
/* RSBootService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSBootService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSBootService_class_info), 0); }
/* RSBootService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSBootService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSBootService_class_info), 0); }
/* RSRenderService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSRenderService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSRenderService_class_info), 0); }
/* RSRenderService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSRenderService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSRenderService_class_info), 0); }
/* GCTickable::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCTickable_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCTickable_class_info), 0); }
/* GCTickable::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCTickable_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCTickable_class_info), 0); }
/* RSAudioService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSAudioService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSAudioService_class_info), 0); }
/* RSAudioService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSAudioService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSAudioService_class_info), 0); }
/* UIManagerI::QueryInterface: class-info table, this -> object +0 */
void RC_FN(UIManagerI_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIManagerI_class_info), 0); }
/* UIManagerI::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(UIManagerI_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIManagerI_class_info), 0); }
/* UIMenu_LAS_SongsList::QueryInterface: class-info table, this -> object -1008 */
void RC_FN(UIMenu_LAS_SongsList_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_LAS_SongsList_class_info), RC_OFF(UIMenu_LAS_SongsList_qi_this_adj)); }
/* UIMenu_LAS_SongsList::QueryInterface const: class-info table, this -> object -1008 */
void RC_FN(UIMenu_LAS_SongsList_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_LAS_SongsList_class_info), RC_OFF(UIMenu_LAS_SongsList_qi_this_adj)); }
/* UIService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(UIService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIService_class_info), 0); }
/* UIService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(UIService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIService_class_info), 0); }
/* ResourceService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ResourceService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ResourceService_class_info), 0); }
/* ResourceService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ResourceService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ResourceService_class_info), 0); }
/* GRMinigameCamera::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRMinigameCamera_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRMinigameCamera_class_info), 0); }
/* GRMinigameCamera::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRMinigameCamera_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRMinigameCamera_class_info), 0); }
/* OscilleratorScopeModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(OscilleratorScopeModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(OscilleratorScopeModel_class_info), 0); }
/* OscilleratorScopeModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(OscilleratorScopeModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(OscilleratorScopeModel_class_info), 0); }
/* ReactiveEffectsService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ReactiveEffectsService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ReactiveEffectsService_class_info), 0); }
/* ReactiveEffectsService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ReactiveEffectsService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ReactiveEffectsService_class_info), 0); }
/* GCColorShift::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCColorShift_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCColorShift_class_info), 0); }
/* GCColorShift::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCColorShift_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCColorShift_class_info), 0); }
/* GC2DAssetHelper::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GC2DAssetHelper_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GC2DAssetHelper_class_info), 0); }
/* GC2DAssetHelper::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GC2DAssetHelper_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GC2DAssetHelper_class_info), 0); }
/* GCAccumulatedActor::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCAccumulatedActor_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCAccumulatedActor_class_info), 0); }
/* GCAccumulatedActor::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCAccumulatedActor_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCAccumulatedActor_class_info), 0); }
/* GCPauseManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCPauseManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCPauseManager_class_info), 0); }
/* GCPauseManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCPauseManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCPauseManager_class_info), 0); }
/* GRCameraNarniaVenue::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraNarniaVenue_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraNarniaVenue_class_info), 0); }
/* GRCameraNarniaVenue::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraNarniaVenue_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraNarniaVenue_class_info), 0); }
/* FormLoader::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FormLoader_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FormLoader_class_info), 0); }
/* FormLoader::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FormLoader_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FormLoader_class_info), 0); }
/* ClipFadeManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ClipFadeManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ClipFadeManager_class_info), 0); }
/* ClipFadeManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ClipFadeManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ClipFadeManager_class_info), 0); }
/* GEService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GEService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GEService_class_info), 0); }
/* GEService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GEService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GEService_class_info), 0); }
/* NarniaPortalModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NarniaPortalModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NarniaPortalModel_class_info), 0); }
/* NarniaPortalModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NarniaPortalModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NarniaPortalModel_class_info), 0); }
/* SongListService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SongListService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SongListService_class_info), 0); }
/* SongListService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SongListService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SongListService_class_info), 0); }
/* NarniaService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NarniaService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NarniaService_class_info), 0); }
/* NarniaService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NarniaService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NarniaService_class_info), 0); }
/* GRSL2Spawner::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRSL2Spawner_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRSL2Spawner_class_info), 0); }
/* GRSL2Spawner::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRSL2Spawner_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRSL2Spawner_class_info), 0); }
/* NarniaFogModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NarniaFogModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NarniaFogModel_class_info), 0); }
/* NarniaFogModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NarniaFogModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NarniaFogModel_class_info), 0); }
/* NarniaFogParticle::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NarniaFogParticle_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NarniaFogParticle_class_info), 0); }
/* NarniaFogParticle::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NarniaFogParticle_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NarniaFogParticle_class_info), 0); }
/* MissionService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MissionService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MissionService_class_info), 0); }
/* MissionService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MissionService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MissionService_class_info), 0); }
/* ToneService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ToneService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ToneService_class_info), 0); }
/* ToneService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ToneService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ToneService_class_info), 0); }
/* StringTuner::QueryInterface: class-info table, this -> object +0 */
void RC_FN(StringTuner_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(StringTuner_class_info), 0); }
/* StringTuner::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(StringTuner_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(StringTuner_class_info), 0); }
/* TunerService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(TunerService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(TunerService_class_info), 0); }
/* TunerService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(TunerService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(TunerService_class_info), 0); }
/* InstrumentLoader::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InstrumentLoader_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InstrumentLoader_class_info), 0); }
/* InstrumentLoader::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InstrumentLoader_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InstrumentLoader_class_info), 0); }
/* GRCrowdPoint::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCrowdPoint_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdPoint_class_info), 0); }
/* GRCrowdPoint::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCrowdPoint_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCrowdPoint_class_info), 0); }
/* GearManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearManager_class_info), 0); }
/* GearManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearManager_class_info), 0); }
/* GearService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearService_class_info), 0); }
/* GearService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearService_class_info), 0); }
/* FogService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FogService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FogService_class_info), 0); }
/* FogService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FogService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FogService_class_info), 0); }
/* GlobalFogSettings::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GlobalFogSettings_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GlobalFogSettings_class_info), 0); }
/* GlobalFogSettings::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GlobalFogSettings_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GlobalFogSettings_class_info), 0); }
/* ManifestManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ManifestManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ManifestManager_class_info), 0); }
/* ManifestManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ManifestManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ManifestManager_class_info), 0); }
/* StatsService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(StatsService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(StatsService_class_info), 0); }
/* StatsService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(StatsService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(StatsService_class_info), 0); }
/* GRCameraLoft::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraLoft_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraLoft_class_info), 0); }
/* GRCameraLoft::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraLoft_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraLoft_class_info), 0); }
/* GEVideoScreenModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GEVideoScreenModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GEVideoScreenModel_class_info), 0); }
/* GEVideoScreenModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GEVideoScreenModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GEVideoScreenModel_class_info), 0); }
/* TunerProgressBuiltin::QueryInterface: class-info table, this -> object +0 */
void RC_FN(TunerProgressBuiltin_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(TunerProgressBuiltin_class_info), 0); }
/* TunerProgressBuiltin::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(TunerProgressBuiltin_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(TunerProgressBuiltin_class_info), 0); }
/* GameColorManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GameColorManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GameColorManager_class_info), 0); }
/* GameColorManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GameColorManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GameColorManager_class_info), 0); }
/* GRCameraSessionMode::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRCameraSessionMode_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraSessionMode_class_info), 0); }
/* GRCameraSessionMode::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRCameraSessionMode_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRCameraSessionMode_class_info), 0); }
/* GCAttachmentManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCAttachmentManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCAttachmentManager_class_info), 0); }
/* GCAttachmentManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCAttachmentManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCAttachmentManager_class_info), 0); }
/* UIMenu_ShopSongs::QueryInterface: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopSongs_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopSongs_class_info), RC_OFF(UIMenu_ShopSongs_qi_this_adj)); }
/* UIMenu_ShopSongs::QueryInterface const: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopSongs_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopSongs_class_info), RC_OFF(UIMenu_ShopSongs_qi_this_adj)); }
/* GRLoftAmbientLight::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLoftAmbientLight_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftAmbientLight_class_info), 0); }
/* GRLoftAmbientLight::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLoftAmbientLight_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftAmbientLight_class_info), 0); }
/* GRLoftSpotLight::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLoftSpotLight_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftSpotLight_class_info), 0); }
/* GRLoftSpotLight::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLoftSpotLight_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftSpotLight_class_info), 0); }
/* GRLoftState::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLoftState_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftState_class_info), 0); }
/* GRLoftState::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLoftState_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftState_class_info), 0); }
/* LoftPostEffectsManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(LoftPostEffectsManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(LoftPostEffectsManager_class_info), 0); }
/* LoftPostEffectsManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(LoftPostEffectsManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(LoftPostEffectsManager_class_info), 0); }
/* RSVenueForkEmitterGear::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSVenueForkEmitterGear_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSVenueForkEmitterGear_class_info), 0); }
/* RSVenueForkEmitterGear::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSVenueForkEmitterGear_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSVenueForkEmitterGear_class_info), 0); }
/* UIMenu_ShopGear::QueryInterface: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopGear_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopGear_class_info), RC_OFF(UIMenu_ShopGear_qi_this_adj)); }
/* UIMenu_ShopGear::QueryInterface const: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopGear_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopGear_class_info), RC_OFF(UIMenu_ShopGear_qi_this_adj)); }
/* UIMenu_ShopPacks::QueryInterface: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopPacks_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopPacks_class_info), RC_OFF(UIMenu_ShopPacks_qi_this_adj)); }
/* UIMenu_ShopPacks::QueryInterface const: class-info table, this -> object -1008 */
void RC_FN(UIMenu_ShopPacks_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(UIMenu_ShopPacks_class_info), RC_OFF(UIMenu_ShopPacks_qi_this_adj)); }
/* GearPlacementNode::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearPlacementNode_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearPlacementNode_class_info), 0); }
/* GearPlacementNode::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearPlacementNode_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearPlacementNode_class_info), 0); }
/* GearLava::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearLava_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearLava_class_info), 0); }
/* GearLava::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearLava_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearLava_class_info), 0); }
/* GearNifCache::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearNifCache_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearNifCache_class_info), 0); }
/* GearNifCache::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearNifCache_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearNifCache_class_info), 0); }
/* GRVenueAmbientAnimations::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRVenueAmbientAnimations_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRVenueAmbientAnimations_class_info), 0); }
/* GRVenueAmbientAnimations::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRVenueAmbientAnimations_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRVenueAmbientAnimations_class_info), 0); }
/* GRSoundIntensity::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRSoundIntensity_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRSoundIntensity_class_info), 0); }
/* GRSoundIntensity::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRSoundIntensity_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRSoundIntensity_class_info), 0); }
/* GRVenuePointLight::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRVenuePointLight_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRVenuePointLight_class_info), 0); }
/* GRVenuePointLight::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRVenuePointLight_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRVenuePointLight_class_info), 0); }
/* GRLoftCabinetScreen::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GRLoftCabinetScreen_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftCabinetScreen_class_info), 0); }
/* GRLoftCabinetScreen::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GRLoftCabinetScreen_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GRLoftCabinetScreen_class_info), 0); }
/* GCLdbChlgService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCLdbChlgService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCLdbChlgService_class_info), 0); }
/* GCLdbChlgService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCLdbChlgService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCLdbChlgService_class_info), 0); }
/* GearChangeController::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GearChangeController_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GearChangeController_class_info), 0); }
/* GearChangeController::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GearChangeController_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GearChangeController_class_info), 0); }
/* GCGFxMouseEventHandler::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCGFxMouseEventHandler_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCGFxMouseEventHandler_class_info), 0); }
/* GCGFxMouseEventHandler::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCGFxMouseEventHandler_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCGFxMouseEventHandler_class_info), 0); }
/* AchievementService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AchievementService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AchievementService_class_info), 0); }
/* AchievementService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AchievementService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AchievementService_class_info), 0); }
/* PrizeService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PrizeService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PrizeService_class_info), 0); }
/* PrizeService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PrizeService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PrizeService_class_info), 0); }
/* GCKinectEventHandler::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCKinectEventHandler_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCKinectEventHandler_class_info), 0); }
/* GCKinectEventHandler::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCKinectEventHandler_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCKinectEventHandler_class_info), 0); }
/* FavoritesService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FavoritesService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FavoritesService_class_info), 0); }
/* FavoritesService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FavoritesService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FavoritesService_class_info), 0); }
/* RSConnectExtension_RDV::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSConnectExtension_RDV_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectExtension_RDV_class_info), 0); }
/* RSConnectExtension_RDV::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSConnectExtension_RDV_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectExtension_RDV_class_info), 0); }
/* RSConnectService_Steam::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSConnectService_Steam_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectService_Steam_class_info), 0); }
/* RSConnectService_Steam::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSConnectService_Steam_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectService_Steam_class_info), 0); }
/* RSSteamService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamService_class_info), 0); }
/* RSSteamService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamService_class_info), 0); }
/* RSDLCService_Steam::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSDLCService_Steam_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSDLCService_Steam_class_info), 0); }
/* RSDLCService_Steam::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSDLCService_Steam_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSDLCService_Steam_class_info), 0); }
/* RSProfileService_MacOS::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSProfileService_MacOS_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSProfileService_MacOS_class_info), 0); }
/* RSProfileService_MacOS::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSProfileService_MacOS_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSProfileService_MacOS_class_info), 0); }
/* RSSteamAchievement::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamAchievement_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamAchievement_class_info), 0); }
/* RSSteamAchievement::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamAchievement_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamAchievement_class_info), 0); }
/* RSSteamActivityFeed::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamActivityFeed_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamActivityFeed_class_info), 0); }
/* RSSteamActivityFeed::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamActivityFeed_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamActivityFeed_class_info), 0); }
/* RSSteamCloudFile::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamCloudFile_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamCloudFile_class_info), 0); }
/* RSSteamCloudFile::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamCloudFile_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamCloudFile_class_info), 0); }
/* RSSteamDownloadableContent::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamDownloadableContent_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamDownloadableContent_class_info), 0); }
/* RSSteamDownloadableContent::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamDownloadableContent_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamDownloadableContent_class_info), 0); }
/* RSSteamLeaderboards::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboards_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboards_class_info), 0); }
/* RSSteamLeaderboards::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboards_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboards_class_info), 0); }
/* RSSteamLeaderboardRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequest_class_info), 0); }
/* RSSteamLeaderboardRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequest_class_info), 0); }
/* RSSteamLeaderboardRequestUpload::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequestUpload_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequestUpload_class_info), 0); }
/* RSSteamLeaderboardRequestUpload::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequestUpload_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequestUpload_class_info), 0); }
/* RSSteamLeaderboardRequestDownload::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequestDownload_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequestDownload_class_info), 0); }
/* RSSteamLeaderboardRequestDownload::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamLeaderboardRequestDownload_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamLeaderboardRequestDownload_class_info), 0); }
/* RSSteamWebApi::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSSteamWebApi_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamWebApi_class_info), 0); }
/* RSSteamWebApi::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSSteamWebApi_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSSteamWebApi_class_info), 0); }
/* FrkService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FrkService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FrkService_class_info), 0); }
/* FrkService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FrkService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FrkService_class_info), 0); }
/* GFxAssetTaskAssetFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFxAssetTaskAssetFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFxAssetTaskAssetFactory_class_info), 0); }
/* GFxAssetTaskAssetFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFxAssetTaskAssetFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFxAssetTaskAssetFactory_class_info), 0); }
/* GFxInputRouterModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFxInputRouterModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFxInputRouterModel_class_info), 0); }
/* GFxInputRouterModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFxInputRouterModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFxInputRouterModel_class_info), 0); }
/* GFxMovieModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFxMovieModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFxMovieModel_class_info), 0); }
/* GFxMovieModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFxMovieModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFxMovieModel_class_info), 0); }
/* GFxService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFxService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFxService_class_info), 0); }
/* GFxService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFxService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFxService_class_info), 0); }
/* sub:LuaResumeCoroutineTask::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_LuaResumeCoroutineTask_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_LuaResumeCoroutineTask_class_info), 0); }
/* sub:LuaResumeCoroutineTask::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_LuaResumeCoroutineTask_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_LuaResumeCoroutineTask_class_info), 0); }
/* LuaScriptFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(LuaScriptFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(LuaScriptFactory_class_info), 0); }
/* LuaScriptFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(LuaScriptFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(LuaScriptFactory_class_info), 0); }
/* SchedulerLua::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SchedulerLua_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SchedulerLua_class_info), 0); }
/* SchedulerLua::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SchedulerLua_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SchedulerLua_class_info), 0); }
/* AttachNifData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AttachNifData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AttachNifData_class_info), 0); }
/* AttachNifData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AttachNifData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AttachNifData_class_info), 0); }
/* CameraService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(CameraService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(CameraService_class_info), 0); }
/* CameraService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(CameraService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(CameraService_class_info), 0); }
/* LightService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(LightService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(LightService_class_info), 0); }
/* LightService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(LightService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(LightService_class_info), 0); }
/* NIFFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NIFFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NIFFactory_class_info), 0); }
/* NIFFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NIFFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NIFFactory_class_info), 0); }
/* RenderService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RenderService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RenderService_class_info), 0); }
/* RenderService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RenderService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RenderService_class_info), 0); }
/* SceneGraphCache::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphCache_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphCache_class_info), 0); }
/* SceneGraphCache::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphCache_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphCache_class_info), 0); }
/* SceneGraphService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphService_class_info), 0); }
/* SceneGraphService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphService_class_info), 0); }
/* ShaderService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ShaderService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ShaderService_class_info), 0); }
/* ShaderService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ShaderService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ShaderService_class_info), 0); }
/* InputActionMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InputActionMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InputActionMessage_class_info), 0); }
/* InputActionMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InputActionMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InputActionMessage_class_info), 0); }
/* InputHandler::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InputHandler_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InputHandler_class_info), 0); }
/* InputHandler::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InputHandler_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InputHandler_class_info), 0); }
/* InputService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InputService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InputService_class_info), 0); }
/* InputService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InputService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InputService_class_info), 0); }
/* InputStickMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InputStickMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InputStickMessage_class_info), 0); }
/* InputStickMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InputStickMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InputStickMessage_class_info), 0); }
/* KeyDownMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KeyDownMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KeyDownMessage_class_info), 0); }
/* KeyDownMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KeyDownMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KeyDownMessage_class_info), 0); }
/* KeyUpMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KeyUpMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KeyUpMessage_class_info), 0); }
/* KeyUpMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KeyUpMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KeyUpMessage_class_info), 0); }
/* MouseMoveMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MouseMoveMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MouseMoveMessage_class_info), 0); }
/* MouseMoveMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MouseMoveMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MouseMoveMessage_class_info), 0); }
/* MouseDownMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MouseDownMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MouseDownMessage_class_info), 0); }
/* MouseDownMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MouseDownMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MouseDownMessage_class_info), 0); }
/* MouseUpMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MouseUpMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MouseUpMessage_class_info), 0); }
/* MouseUpMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MouseUpMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MouseUpMessage_class_info), 0); }
/* Entity::QueryInterface: class-info table, this -> object +0 */
void RC_FN(Entity_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(Entity_class_info), 0); }
/* Entity::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(Entity_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(Entity_class_info), 0); }
/* sub:EntityChangeMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_EntityChangeMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_EntityChangeMessage_class_info), 0); }
/* sub:EntityChangeMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_EntityChangeMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_EntityChangeMessage_class_info), 0); }
/* EntityFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactory_class_info), 0); }
/* EntityFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactory_class_info), 0); }
/* EntityLoadRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityLoadRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoadRequest_class_info), 0); }
/* EntityLoadRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityLoadRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoadRequest_class_info), 0); }
/* EntityLoadResult::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityLoadResult_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoadResult_class_info), 0); }
/* EntityLoadResult::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityLoadResult_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoadResult_class_info), 0); }
/* EntityPreloadRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityPreloadRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityPreloadRequest_class_info), 0); }
/* EntityPreloadRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityPreloadRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityPreloadRequest_class_info), 0); }
/* EntityPreloadResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityPreloadResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityPreloadResponse_class_info), 0); }
/* EntityPreloadResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityPreloadResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityPreloadResponse_class_info), 0); }
/* EntityLoaderService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityLoaderService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoaderService_class_info), 0); }
/* EntityLoaderService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityLoaderService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityLoaderService_class_info), 0); }
/* EntityManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityManager_class_info), 0); }
/* EntityManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityManager_class_info), 0); }
/* EntitySetLoadState::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntitySetLoadState_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntitySetLoadState_class_info), 0); }
/* EntitySetLoadState::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntitySetLoadState_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntitySetLoadState_class_info), 0); }
/* sub:EventMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_EventMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_EventMessage_class_info), 0); }
/* sub:EventMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_EventMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_EventMessage_class_info), 0); }
/* FlatModelFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FlatModelFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactory_class_info), 0); }
/* FlatModelFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FlatModelFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactory_class_info), 0); }
/* FlatModelManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FlatModelManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelManager_class_info), 0); }
/* FlatModelManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FlatModelManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelManager_class_info), 0); }
/* PendingBehavior::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PendingBehavior_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PendingBehavior_class_info), 0); }
/* PendingBehavior::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PendingBehavior_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PendingBehavior_class_info), 0); }
/* Scheduler::QueryInterface: class-info table, this -> object +0 */
void RC_FN(Scheduler_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(Scheduler_class_info), 0); }
/* Scheduler::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(Scheduler_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(Scheduler_class_info), 0); }
/* ScriptFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ScriptFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ScriptFactoryResponse_class_info), 0); }
/* ScriptFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ScriptFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ScriptFactoryResponse_class_info), 0); }
/* WorldFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(WorldFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactory_class_info), 0); }
/* WorldFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(WorldFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactory_class_info), 0); }
/* AnimationService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AnimationService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AnimationService_class_info), 0); }
/* AnimationService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AnimationService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AnimationService_class_info), 0); }
/* KFMFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KFMFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactory_class_info), 0); }
/* KFMFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KFMFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactory_class_info), 0); }
/* KFMFactory::ResponseData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KFMFactory__ResponseData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactory__ResponseData_class_info), 0); }
/* KFMFactory::ResponseData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KFMFactory__ResponseData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactory__ResponseData_class_info), 0); }
/* AssetCacheResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetCacheResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetCacheResponse_class_info), 0); }
/* AssetCacheResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetCacheResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetCacheResponse_class_info), 0); }
/* AssetConfigService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetConfigService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetConfigService_class_info), 0); }
/* AssetConfigService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetConfigService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetConfigService_class_info), 0); }
/* AssetFactoryManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetFactoryManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetFactoryManager_class_info), 0); }
/* AssetFactoryManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetFactoryManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetFactoryManager_class_info), 0); }
/* AssetLoadRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetLoadRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetLoadRequest_class_info), 0); }
/* AssetLoadRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetLoadRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetLoadRequest_class_info), 0); }
/* AssetLoadResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetLoadResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetLoadResponse_class_info), 0); }
/* AssetLoadResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetLoadResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetLoadResponse_class_info), 0); }
/* sub:AssetLocatorRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_AssetLocatorRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetLocatorRequest_class_info), 0); }
/* sub:AssetLocatorRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_AssetLocatorRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetLocatorRequest_class_info), 0); }
/* sub:AssetConfigurationMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_AssetConfigurationMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetConfigurationMessage_class_info), 0); }
/* sub:AssetConfigurationMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_AssetConfigurationMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetConfigurationMessage_class_info), 0); }
/* sub:AssetLocatorResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_AssetLocatorResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetLocatorResponse_class_info), 0); }
/* sub:AssetLocatorResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_AssetLocatorResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_AssetLocatorResponse_class_info), 0); }
/* AssetTagsInfoResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetTagsInfoResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetTagsInfoResponse_class_info), 0); }
/* AssetTagsInfoResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetTagsInfoResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetTagsInfoResponse_class_info), 0); }
/* AssetLocatorService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetLocatorService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetLocatorService_class_info), 0); }
/* AssetLocatorService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetLocatorService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetLocatorService_class_info), 0); }
/* ConfigManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ConfigManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ConfigManager_class_info), 0); }
/* ConfigManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ConfigManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ConfigManager_class_info), 0); }
/* SectionEntry::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SectionEntry_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SectionEntry_class_info), 0); }
/* SectionEntry::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SectionEntry_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SectionEntry_class_info), 0); }
/* ConfigSource::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ConfigSource_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ConfigSource_class_info), 0); }
/* ConfigSource::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ConfigSource_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ConfigSource_class_info), 0); }
/* CommandLineSource::QueryInterface: class-info table, this -> object +0 */
void RC_FN(CommandLineSource_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(CommandLineSource_class_info), 0); }
/* CommandLineSource::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(CommandLineSource_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(CommandLineSource_class_info), 0); }
/* IniSource::QueryInterface: class-info table, this -> object +0 */
void RC_FN(IniSource_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(IniSource_class_info), 0); }
/* IniSource::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(IniSource_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(IniSource_class_info), 0); }
/* EnvVarSource::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EnvVarSource_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EnvVarSource_class_info), 0); }
/* EnvVarSource::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EnvVarSource_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EnvVarSource_class_info), 0); }
/* DebugOutDestination::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DebugOutDestination_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DebugOutDestination_class_info), 0); }
/* DebugOutDestination::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DebugOutDestination_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DebugOutDestination_class_info), 0); }
/* FileDestination::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FileDestination_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FileDestination_class_info), 0); }
/* FileDestination::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FileDestination_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FileDestination_class_info), 0); }
/* GenericAssetFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GenericAssetFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GenericAssetFactory_class_info), 0); }
/* GenericAssetFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GenericAssetFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GenericAssetFactory_class_info), 0); }
/* GenericAssetLoadResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GenericAssetLoadResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GenericAssetLoadResponse_class_info), 0); }
/* GenericAssetLoadResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GenericAssetLoadResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GenericAssetLoadResponse_class_info), 0); }
/* sub:IMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_IMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_IMessage_class_info), 0); }
/* sub:IMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_IMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_IMessage_class_info), 0); }
/* Logger::QueryInterface: class-info table, this -> object +0 */
void RC_FN(Logger_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(Logger_class_info), 0); }
/* Logger::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(Logger_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(Logger_class_info), 0); }
/* MessageService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MessageService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MessageService_class_info), 0); }
/* MessageService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MessageService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MessageService_class_info), 0); }
/* NetMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NetMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NetMessage_class_info), 0); }
/* NetMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NetMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NetMessage_class_info), 0); }
/* AssignNetIDMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssignNetIDMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssignNetIDMessage_class_info), 0); }
/* AssignNetIDMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssignNetIDMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssignNetIDMessage_class_info), 0); }
/* PrintDestination::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PrintDestination_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PrintDestination_class_info), 0); }
/* PrintDestination::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PrintDestination_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PrintDestination_class_info), 0); }
/* ServiceManager::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ServiceManager_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ServiceManager_class_info), 0); }
/* ServiceManager::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ServiceManager_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ServiceManager_class_info), 0); }
/* sub:StreamMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(sub_StreamMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(sub_StreamMessage_class_info), 0); }
/* sub:StreamMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(sub_StreamMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(sub_StreamMessage_class_info), 0); }
/* MacOSPlatformService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(MacOSPlatformService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(MacOSPlatformService_class_info), 0); }
/* MacOSPlatformService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(MacOSPlatformService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(MacOSPlatformService_class_info), 0); }
/* AudioBankAssetFactory::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetFactory_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetFactory_class_info), 0); }
/* AudioBankAssetFactory::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetFactory_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetFactory_class_info), 0); }
/* AudioBankAssetLoadResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetLoadResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetLoadResponse_class_info), 0); }
/* AudioBankAssetLoadResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetLoadResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetLoadResponse_class_info), 0); }
/* AudioMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AudioMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AudioMessage_class_info), 0); }
/* AudioMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AudioMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AudioMessage_class_info), 0); }
/* AudioService::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AudioService_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AudioService_class_info), 0); }
/* AudioService::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AudioService_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AudioService_class_info), 0); }
/* SceneGraphRemovedMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphRemovedMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphRemovedMessage_class_info), 0); }
/* SceneGraphRemovedMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphRemovedMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphRemovedMessage_class_info), 0); }
/* SceneGraphAddedMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphAddedMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphAddedMessage_class_info), 0); }
/* SceneGraphAddedMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphAddedMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphAddedMessage_class_info), 0); }
/* SceneGraphsUpdatedMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphsUpdatedMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphsUpdatedMessage_class_info), 0); }
/* SceneGraphsUpdatedMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphsUpdatedMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphsUpdatedMessage_class_info), 0); }
/* SceneGraphMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphMessage_class_info), 0); }
/* SceneGraphMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphMessage_class_info), 0); }
/* JSONFactory::JSONFactoryData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(JSONFactory__JSONFactoryData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory__JSONFactoryData_class_info), 0); }
/* JSONFactory::JSONFactoryData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(JSONFactory__JSONFactoryData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(JSONFactory__JSONFactoryData_class_info), 0); }
/* ReturnToTitleScreen::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ReturnToTitleScreen_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ReturnToTitleScreen_class_info), 0); }
/* ReturnToTitleScreen::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ReturnToTitleScreen_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ReturnToTitleScreen_class_info), 0); }
/* PreloadHelperComplete::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PreloadHelperComplete_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelperComplete_class_info), 0); }
/* PreloadHelperComplete::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PreloadHelperComplete_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PreloadHelperComplete_class_info), 0); }
/* SceneGraphCacheResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SceneGraphCacheResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphCacheResponse_class_info), 0); }
/* SceneGraphCacheResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SceneGraphCacheResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SceneGraphCacheResponse_class_info), 0); }
/* PreloadGroup::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PreloadGroup_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PreloadGroup_class_info), 0); }
/* PreloadGroup::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PreloadGroup_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PreloadGroup_class_info), 0); }
/* GCPauseMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCPauseMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCPauseMessage_class_info), 0); }
/* GCPauseMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCPauseMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCPauseMessage_class_info), 0); }
/* RSGraphAssetFactoryLoadResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSGraphAssetFactoryLoadResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSGraphAssetFactoryLoadResponse_class_info), 0); }
/* RSGraphAssetFactoryLoadResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSGraphAssetFactoryLoadResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSGraphAssetFactoryLoadResponse_class_info), 0); }
/* RSConnectGameInterface_Impl::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RSConnectGameInterface_Impl_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectGameInterface_Impl_class_info), 0); }
/* RSConnectGameInterface_Impl::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RSConnectGameInterface_Impl_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RSConnectGameInterface_Impl_class_info), 0); }
/* ReactiveRingModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ReactiveRingModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ReactiveRingModel_class_info), 0); }
/* ReactiveRingModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ReactiveRingModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ReactiveRingModel_class_info), 0); }
/* RingParticleEmitter::QueryInterface: class-info table, this -> object +0 */
void RC_FN(RingParticleEmitter_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(RingParticleEmitter_class_info), 0); }
/* RingParticleEmitter::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(RingParticleEmitter_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(RingParticleEmitter_class_info), 0); }
/* GCGFxMouseEventMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCGFxMouseEventMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCGFxMouseEventMessage_class_info), 0); }
/* GCGFxMouseEventMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCGFxMouseEventMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCGFxMouseEventMessage_class_info), 0); }
/* GCKinectEventMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GCKinectEventMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GCKinectEventMessage_class_info), 0); }
/* GCKinectEventMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GCKinectEventMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GCKinectEventMessage_class_info), 0); }
/* NiFrkParticleEffectModel::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NiFrkParticleEffectModel_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NiFrkParticleEffectModel_class_info), 0); }
/* NiFrkParticleEffectModel::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NiFrkParticleEffectModel_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NiFrkParticleEffectModel_class_info), 0); }
/* GFxAssetTaskRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFxAssetTaskRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFxAssetTaskRequest_class_info), 0); }
/* GFxAssetTaskRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFxAssetTaskRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFxAssetTaskRequest_class_info), 0); }
/* GFXModelResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(GFXModelResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(GFXModelResponse_class_info), 0); }
/* GFXModelResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(GFXModelResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(GFXModelResponse_class_info), 0); }
/* LuaScriptFactoryResponseData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(LuaScriptFactoryResponseData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(LuaScriptFactoryResponseData_class_info), 0); }
/* LuaScriptFactoryResponseData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(LuaScriptFactoryResponseData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(LuaScriptFactoryResponseData_class_info), 0); }
/* NIFFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(NIFFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(NIFFactoryResponse_class_info), 0); }
/* NIFFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(NIFFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(NIFFactoryResponse_class_info), 0); }
/* AttachmentMadeMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AttachmentMadeMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentMadeMessage_class_info), 0); }
/* AttachmentMadeMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AttachmentMadeMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentMadeMessage_class_info), 0); }
/* AttachmentMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AttachmentMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentMessage_class_info), 0); }
/* AttachmentMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AttachmentMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentMessage_class_info), 0); }
/* AttachmentBrokenMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AttachmentBrokenMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentBrokenMessage_class_info), 0); }
/* AttachmentBrokenMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AttachmentBrokenMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AttachmentBrokenMessage_class_info), 0); }
/* InputActionMapsLoaded::QueryInterface: class-info table, this -> object +0 */
void RC_FN(InputActionMapsLoaded_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(InputActionMapsLoaded_class_info), 0); }
/* InputActionMapsLoaded::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(InputActionMapsLoaded_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(InputActionMapsLoaded_class_info), 0); }
/* ScheduledTask::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ScheduledTask_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ScheduledTask_class_info), 0); }
/* ScheduledTask::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ScheduledTask_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ScheduledTask_class_info), 0); }
/* EntityFactory::EntityFactoryData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityFactory__EntityFactoryData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactory__EntityFactoryData_class_info), 0); }
/* EntityFactory::EntityFactoryData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityFactory__EntityFactoryData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactory__EntityFactoryData_class_info), 0); }
/* EntityFactoryRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityFactoryRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactoryRequest_class_info), 0); }
/* EntityFactoryRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityFactoryRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactoryRequest_class_info), 0); }
/* EntityFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(EntityFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactoryResponse_class_info), 0); }
/* EntityFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(EntityFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(EntityFactoryResponse_class_info), 0); }
/* ResponseData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ResponseData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ResponseData_class_info), 0); }
/* ResponseData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ResponseData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ResponseData_class_info), 0); }
/* FlatModelFactoryRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FlatModelFactoryRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactoryRequest_class_info), 0); }
/* FlatModelFactoryRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FlatModelFactoryRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactoryRequest_class_info), 0); }
/* FlatModelFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(FlatModelFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactoryResponse_class_info), 0); }
/* FlatModelFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(FlatModelFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(FlatModelFactoryResponse_class_info), 0); }
/* ScriptFactoryRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ScriptFactoryRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ScriptFactoryRequest_class_info), 0); }
/* ScriptFactoryRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ScriptFactoryRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ScriptFactoryRequest_class_info), 0); }
/* WorldFactoryData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(WorldFactoryData_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryData_class_info), 0); }
/* WorldFactoryData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(WorldFactoryData_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryData_class_info), 0); }
/* WorldFactoryRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(WorldFactoryRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryRequest_class_info), 0); }
/* WorldFactoryRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(WorldFactoryRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryRequest_class_info), 0); }
/* WorldFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(WorldFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryResponse_class_info), 0); }
/* WorldFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(WorldFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(WorldFactoryResponse_class_info), 0); }
/* ActorAddedMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ActorAddedMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ActorAddedMessage_class_info), 0); }
/* ActorAddedMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ActorAddedMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ActorAddedMessage_class_info), 0); }
/* ActorRemovedMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ActorRemovedMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ActorRemovedMessage_class_info), 0); }
/* ActorRemovedMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ActorRemovedMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ActorRemovedMessage_class_info), 0); }
/* KFMFactoryRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KFMFactoryRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactoryRequest_class_info), 0); }
/* KFMFactoryRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KFMFactoryRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactoryRequest_class_info), 0); }
/* KFMFactoryResponse::QueryInterface: class-info table, this -> object +0 */
void RC_FN(KFMFactoryResponse_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactoryResponse_class_info), 0); }
/* KFMFactoryResponse::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(KFMFactoryResponse_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(KFMFactoryResponse_class_info), 0); }
/* AssetLocationMap::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AssetLocationMap_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AssetLocationMap_class_info), 0); }
/* AssetLocationMap::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AssetLocationMap_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AssetLocationMap_class_info), 0); }
/* ResponseData::QueryInterface: class-info table, this -> object +0 */
void RC_FN(ResponseData2_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(ResponseData2_class_info), 0); }
/* ResponseData::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(ResponseData2_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(ResponseData2_class_info), 0); }
/* AudioBankAssetLoadRequest::QueryInterface: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetLoadRequest_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetLoadRequest_class_info), 0); }
/* AudioBankAssetLoadRequest::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(AudioBankAssetLoadRequest_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(AudioBankAssetLoadRequest_class_info), 0); }
/* PostEventMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PostEventMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PostEventMessage_class_info), 0); }
/* PostEventMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PostEventMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PostEventMessage_class_info), 0); }
/* PostTriggerMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(PostTriggerMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(PostTriggerMessage_class_info), 0); }
/* PostTriggerMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(PostTriggerMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(PostTriggerMessage_class_info), 0); }
/* SetParamMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SetParamMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SetParamMessage_class_info), 0); }
/* SetParamMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SetParamMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SetParamMessage_class_info), 0); }
/* SetStateMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SetStateMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SetStateMessage_class_info), 0); }
/* SetStateMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SetStateMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SetStateMessage_class_info), 0); }
/* SetSwitchMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(SetSwitchMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(SetSwitchMessage_class_info), 0); }
/* SetSwitchMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(SetSwitchMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(SetSwitchMessage_class_info), 0); }
/* DoPrepareEventCallbackBehaviorMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DoPrepareEventCallbackBehaviorMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DoPrepareEventCallbackBehaviorMessage_class_info), 0); }
/* DoPrepareEventCallbackBehaviorMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DoPrepareEventCallbackBehaviorMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DoPrepareEventCallbackBehaviorMessage_class_info), 0); }
/* DoUnprepareEventCallbackBehaviorMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DoUnprepareEventCallbackBehaviorMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DoUnprepareEventCallbackBehaviorMessage_class_info), 0); }
/* DoUnprepareEventCallbackBehaviorMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DoUnprepareEventCallbackBehaviorMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DoUnprepareEventCallbackBehaviorMessage_class_info), 0); }
/* DoLoadBankCallbackBehaviorMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DoLoadBankCallbackBehaviorMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DoLoadBankCallbackBehaviorMessage_class_info), 0); }
/* DoLoadBankCallbackBehaviorMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DoLoadBankCallbackBehaviorMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DoLoadBankCallbackBehaviorMessage_class_info), 0); }
/* DoUnloadBankCallbackBehaviorMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DoUnloadBankCallbackBehaviorMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DoUnloadBankCallbackBehaviorMessage_class_info), 0); }
/* DoUnloadBankCallbackBehaviorMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DoUnloadBankCallbackBehaviorMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DoUnloadBankCallbackBehaviorMessage_class_info), 0); }
/* DoPostEventExNotifyCallbackBehaviorMessage::QueryInterface: class-info table, this -> object +0 */
void RC_FN(DoPostEventExNotifyCallbackBehaviorMessage_QueryInterface)(rc_ctx *c) { qi(c, RC_ADDR(DoPostEventExNotifyCallbackBehaviorMessage_class_info), 0); }
/* DoPostEventExNotifyCallbackBehaviorMessage::QueryInterface const: class-info table, this -> object +0 */
void RC_FN(DoPostEventExNotifyCallbackBehaviorMessage_QueryInterface_const)(rc_ctx *c) { qi(c, RC_ADDR(DoPostEventExNotifyCallbackBehaviorMessage_class_info), 0); }
