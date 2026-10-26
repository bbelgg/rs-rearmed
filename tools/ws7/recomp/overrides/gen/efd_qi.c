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

/* 0x10001cbd0 GRCamera::QueryInterface: class-info table 0x1018ddbb0, this -> object +0 */
void F_10001cbd0(rc_ctx *c) { qi(c, 0x1018ddbb0, 0); }
/* 0x10001cc30 GRCamera::QueryInterface const: class-info table 0x1018ddbb0, this -> object +0 */
void F_10001cc30(rc_ctx *c) { qi(c, 0x1018ddbb0, 0); }
/* 0x100024580 GRCameraBackground::QueryInterface: class-info table 0x1018de018, this -> object +0 */
void F_100024580(rc_ctx *c) { qi(c, 0x1018de018, 0); }
/* 0x1000245e0 GRCameraBackground::QueryInterface const: class-info table 0x1018de018, this -> object +0 */
void F_1000245e0(rc_ctx *c) { qi(c, 0x1018de018, 0); }
/* 0x10002b5a0 GRCameraBGBehavior_Animated::QueryInterface: class-info table 0x1018de480, this -> object +0 */
void F_10002b5a0(rc_ctx *c) { qi(c, 0x1018de480, 0); }
/* 0x10002b600 GRCameraBGBehavior_Animated::QueryInterface const: class-info table 0x1018de480, this -> object +0 */
void F_10002b600(rc_ctx *c) { qi(c, 0x1018de480, 0); }
/* 0x100046e90 GRCameraBGBehavior_BuildUp::QueryInterface: class-info table 0x1018de8e8, this -> object +0 */
void F_100046e90(rc_ctx *c) { qi(c, 0x1018de8e8, 0); }
/* 0x100046ef0 GRCameraBGBehavior_BuildUp::QueryInterface const: class-info table 0x1018de8e8, this -> object +0 */
void F_100046ef0(rc_ctx *c) { qi(c, 0x1018de8e8, 0); }
/* 0x100052c70 GRCameraBGBehavior_Customize::QueryInterface: class-info table 0x1018ded50, this -> object +0 */
void F_100052c70(rc_ctx *c) { qi(c, 0x1018ded50, 0); }
/* 0x100052cd0 GRCameraBGBehavior_Customize::QueryInterface const: class-info table 0x1018ded50, this -> object +0 */
void F_100052cd0(rc_ctx *c) { qi(c, 0x1018ded50, 0); }
/* 0x1000593b0 GRCameraBGBehavior_Debug::QueryInterface: class-info table 0x1018df1b8, this -> object +0 */
void F_1000593b0(rc_ctx *c) { qi(c, 0x1018df1b8, 0); }
/* 0x100059410 GRCameraBGBehavior_Debug::QueryInterface const: class-info table 0x1018df1b8, this -> object +0 */
void F_100059410(rc_ctx *c) { qi(c, 0x1018df1b8, 0); }
/* 0x100060530 GRCameraBGBehavior_Panning::QueryInterface: class-info table 0x1018df620, this -> object +0 */
void F_100060530(rc_ctx *c) { qi(c, 0x1018df620, 0); }
/* 0x100060590 GRCameraBGBehavior_Panning::QueryInterface const: class-info table 0x1018df620, this -> object +0 */
void F_100060590(rc_ctx *c) { qi(c, 0x1018df620, 0); }
/* 0x1000706f0 CameraSong::QueryInterface: class-info table 0x1018dfa98, this -> object +0 */
void F_1000706f0(rc_ctx *c) { qi(c, 0x1018dfa98, 0); }
/* 0x100070750 CameraSong::QueryInterface const: class-info table 0x1018dfa98, this -> object +0 */
void F_100070750(rc_ctx *c) { qi(c, 0x1018dfa98, 0); }
/* 0x10007a980 GRCameraDebug::QueryInterface: class-info table 0x1018dff00, this -> object +0 */
void F_10007a980(rc_ctx *c) { qi(c, 0x1018dff00, 0); }
/* 0x10007a9e0 GRCameraDebug::QueryInterface const: class-info table 0x1018dff00, this -> object +0 */
void F_10007a9e0(rc_ctx *c) { qi(c, 0x1018dff00, 0); }
/* 0x10007f960 GRCameraScripted::QueryInterface: class-info table 0x1018e0368, this -> object +0 */
void F_10007f960(rc_ctx *c) { qi(c, 0x1018e0368, 0); }
/* 0x10007f9c0 GRCameraScripted::QueryInterface const: class-info table 0x1018e0368, this -> object +0 */
void F_10007f9c0(rc_ctx *c) { qi(c, 0x1018e0368, 0); }
/* 0x100085830 GRCameraService::QueryInterface: class-info table 0x1018e07a0, this -> object +0 */
void F_100085830(rc_ctx *c) { qi(c, 0x1018e07a0, 0); }
/* 0x100085890 GRCameraService::QueryInterface const: class-info table 0x1018e07a0, this -> object +0 */
void F_100085890(rc_ctx *c) { qi(c, 0x1018e07a0, 0); }
/* 0x1000876b0 IGRCameraImpl::QueryInterface: class-info table 0x1018e0ba8, this -> object +0 */
void F_1000876b0(rc_ctx *c) { qi(c, 0x1018e0ba8, 0); }
/* 0x100087710 IGRCameraImpl::QueryInterface const: class-info table 0x1018e0ba8, this -> object +0 */
void F_100087710(rc_ctx *c) { qi(c, 0x1018e0ba8, 0); }
/* 0x1000957a0 DatabaseCommandQueue::QueryInterface: class-info table 0x1018e0fe8, this -> object +0 */
void F_1000957a0(rc_ctx *c) { qi(c, 0x1018e0fe8, 0); }
/* 0x100095800 DatabaseCommandQueue::QueryInterface const: class-info table 0x1018e0fe8, this -> object +0 */
void F_100095800(rc_ctx *c) { qi(c, 0x1018e0fe8, 0); }
/* 0x100098c40 DatabaseMessageSystem::QueryInterface: class-info table 0x1018e1c00, this -> object +0 */
void F_100098c40(rc_ctx *c) { qi(c, 0x1018e1c00, 0); }
/* 0x100098ca0 DatabaseMessageSystem::QueryInterface const: class-info table 0x1018e1c00, this -> object +0 */
void F_100098ca0(rc_ctx *c) { qi(c, 0x1018e1c00, 0); }
/* 0x1000a4ff0 DatabaseService::QueryInterface: class-info table 0x1018e2008, this -> object +0 */
void F_1000a4ff0(rc_ctx *c) { qi(c, 0x1018e2008, 0); }
/* 0x1000a5050 DatabaseService::QueryInterface const: class-info table 0x1018e2008, this -> object +0 */
void F_1000a5050(rc_ctx *c) { qi(c, 0x1018e2008, 0); }
/* 0x1000b38f0 GRCrowdBox::QueryInterface: class-info table 0x1018e2418, this -> object +0 */
void F_1000b38f0(rc_ctx *c) { qi(c, 0x1018e2418, 0); }
/* 0x1000b3950 GRCrowdBox::QueryInterface const: class-info table 0x1018e2418, this -> object +0 */
void F_1000b3950(rc_ctx *c) { qi(c, 0x1018e2418, 0); }
/* 0x1000c3110 GRCrowdSystem::QueryInterface: class-info table 0x1018e28a0, this -> object +0 */
void F_1000c3110(rc_ctx *c) { qi(c, 0x1018e28a0, 0); }
/* 0x1000c3170 GRCrowdSystem::QueryInterface const: class-info table 0x1018e28a0, this -> object +0 */
void F_1000c3170(rc_ctx *c) { qi(c, 0x1018e28a0, 0); }
/* 0x1000cfbc0 GRLightService::QueryInterface: class-info table 0x1018e2cb8, this -> object +0 */
void F_1000cfbc0(rc_ctx *c) { qi(c, 0x1018e2cb8, 0); }
/* 0x1000cfc20 GRLightService::QueryInterface const: class-info table 0x1018e2cb8, this -> object +0 */
void F_1000cfc20(rc_ctx *c) { qi(c, 0x1018e2cb8, 0); }
/* 0x1000e5290 GRMusicService::QueryInterface: class-info table 0x1018e3130, this -> object +0 */
void F_1000e5290(rc_ctx *c) { qi(c, 0x1018e3130, 0); }
/* 0x1000e52f0 GRMusicService::QueryInterface const: class-info table 0x1018e3130, this -> object +0 */
void F_1000e52f0(rc_ctx *c) { qi(c, 0x1018e3130, 0); }
/* 0x1000f4590 JSONFactory::QueryInterface: class-info table 0x1018e3620, this -> object +0 */
void F_1000f4590(rc_ctx *c) { qi(c, 0x1018e3620, 0); }
/* 0x1000f45f0 JSONFactory::QueryInterface const: class-info table 0x1018e3620, this -> object +0 */
void F_1000f45f0(rc_ctx *c) { qi(c, 0x1018e3620, 0); }
/* 0x1000f46e0 JSONFactory::Response::QueryInterface: class-info table 0x1018e3a28, this -> object +0 */
void F_1000f46e0(rc_ctx *c) { qi(c, 0x1018e3a28, 0); }
/* 0x1000f4740 JSONFactory::Response::QueryInterface const: class-info table 0x1018e3a28, this -> object +0 */
void F_1000f4740(rc_ctx *c) { qi(c, 0x1018e3a28, 0); }
/* 0x100116be0 PostEffectColorCorrect::QueryInterface: class-info table 0x1018e4e78, this -> object +0 */
void F_100116be0(rc_ctx *c) { qi(c, 0x1018e4e78, 0); }
/* 0x100116c40 PostEffectColorCorrect::QueryInterface const: class-info table 0x1018e4e78, this -> object +0 */
void F_100116c40(rc_ctx *c) { qi(c, 0x1018e4e78, 0); }
/* 0x100124160 PostEffectsService::QueryInterface: class-info table 0x1018e52b8, this -> object +0 */
void F_100124160(rc_ctx *c) { qi(c, 0x1018e52b8, 0); }
/* 0x1001241c0 PostEffectsService::QueryInterface const: class-info table 0x1018e52b8, this -> object +0 */
void F_1001241c0(rc_ctx *c) { qi(c, 0x1018e52b8, 0); }
/* 0x100132340 GRClipFadeEffectComponent::QueryInterface: class-info table 0x1018e56e8, this -> object +0 */
void F_100132340(rc_ctx *c) { qi(c, 0x1018e56e8, 0); }
/* 0x1001323a0 GRClipFadeEffectComponent::QueryInterface const: class-info table 0x1018e56e8, this -> object +0 */
void F_1001323a0(rc_ctx *c) { qi(c, 0x1018e56e8, 0); }
/* 0x100138e80 GRDisplayRatioAdjuster::QueryInterface: class-info table 0x1018e5b20, this -> object +0 */
void F_100138e80(rc_ctx *c) { qi(c, 0x1018e5b20, 0); }
/* 0x100138ee0 GRDisplayRatioAdjuster::QueryInterface const: class-info table 0x1018e5b20, this -> object +0 */
void F_100138ee0(rc_ctx *c) { qi(c, 0x1018e5b20, 0); }
/* 0x10014b5f0 GRShowLightsSystem::QueryInterface: class-info table 0x1018e6370, this -> object +0 */
void F_10014b5f0(rc_ctx *c) { qi(c, 0x1018e6370, 0); }
/* 0x10014b650 GRShowLightsSystem::QueryInterface const: class-info table 0x1018e6370, this -> object +0 */
void F_10014b650(rc_ctx *c) { qi(c, 0x1018e6370, 0); }
/* 0x100154a50 GRVenueMenuPresentation_Flow::QueryInterface: class-info table 0x1018e67c8, this -> object +0 */
void F_100154a50(rc_ctx *c) { qi(c, 0x1018e67c8, 0); }
/* 0x100154ab0 GRVenueMenuPresentation_Flow::QueryInterface const: class-info table 0x1018e67c8, this -> object +0 */
void F_100154ab0(rc_ctx *c) { qi(c, 0x1018e67c8, 0); }
/* 0x100168600 MainService_MacOS::QueryInterface: class-info table 0x1018e6c58, this -> object +0 */
void F_100168600(rc_ctx *c) { qi(c, 0x1018e6c58, 0); }
/* 0x100168660 MainService_MacOS::QueryInterface const: class-info table 0x1018e6c58, this -> object +0 */
void F_100168660(rc_ctx *c) { qi(c, 0x1018e6c58, 0); }
/* 0x100178660 NSP_PlayListService::QueryInterface: class-info table 0x1018e7060, this -> object +0 */
void F_100178660(rc_ctx *c) { qi(c, 0x1018e7060, 0); }
/* 0x1001786c0 NSP_PlayListService::QueryInterface const: class-info table 0x1018e7060, this -> object +0 */
void F_1001786c0(rc_ctx *c) { qi(c, 0x1018e7060, 0); }
/* 0x100187dc0 RSLogger::QueryInterface: class-info table 0x1018e7498, this -> object +0 */
void F_100187dc0(rc_ctx *c) { qi(c, 0x1018e7498, 0); }
/* 0x100187e20 RSLogger::QueryInterface const: class-info table 0x1018e7498, this -> object +0 */
void F_100187e20(rc_ctx *c) { qi(c, 0x1018e7498, 0); }
/* 0x100188940 RSDejaOutDestination::QueryInterface: class-info table 0x1018e78a0, this -> object +0 */
void F_100188940(rc_ctx *c) { qi(c, 0x1018e78a0, 0); }
/* 0x1001889a0 RSDejaOutDestination::QueryInterface const: class-info table 0x1018e78a0, this -> object +0 */
void F_1001889a0(rc_ctx *c) { qi(c, 0x1018e78a0, 0); }
/* 0x1001aca40 GuitarService::QueryInterface: class-info table 0x1018e7cc8, this -> object +0 */
void F_1001aca40(rc_ctx *c) { qi(c, 0x1018e7cc8, 0); }
/* 0x1001acaa0 GuitarService::QueryInterface const: class-info table 0x1018e7cc8, this -> object +0 */
void F_1001acaa0(rc_ctx *c) { qi(c, 0x1018e7cc8, 0); }
/* 0x1001bd880 GRLocalization::QueryInterface: class-info table 0x1018e80e8, this -> object +0 */
void F_1001bd880(rc_ctx *c) { qi(c, 0x1018e80e8, 0); }
/* 0x1001bd8e0 GRLocalization::QueryInterface const: class-info table 0x1018e80e8, this -> object +0 */
void F_1001bd8e0(rc_ctx *c) { qi(c, 0x1018e80e8, 0); }
/* 0x100209530 GRSongDirectory::QueryInterface: class-info table 0x1018e8588, this -> object +0 */
void F_100209530(rc_ctx *c) { qi(c, 0x1018e8588, 0); }
/* 0x100209590 GRSongDirectory::QueryInterface const: class-info table 0x1018e8588, this -> object +0 */
void F_100209590(rc_ctx *c) { qi(c, 0x1018e8588, 0); }
/* 0x10021af30 DetectionService::QueryInterface: class-info table 0x1018e8998, this -> object +0 */
void F_10021af30(rc_ctx *c) { qi(c, 0x1018e8998, 0); }
/* 0x10021af90 DetectionService::QueryInterface const: class-info table 0x1018e8998, this -> object +0 */
void F_10021af90(rc_ctx *c) { qi(c, 0x1018e8998, 0); }
/* 0x10023b2e0 VenueService::QueryInterface: class-info table 0x1018e9578, this -> object +0 */
void F_10023b2e0(rc_ctx *c) { qi(c, 0x1018e9578, 0); }
/* 0x10023b340 VenueService::QueryInterface const: class-info table 0x1018e9578, this -> object +0 */
void F_10023b340(rc_ctx *c) { qi(c, 0x1018e9578, 0); }
/* 0x1002732d0 GameService::QueryInterface: class-info table 0x1018e9b40, this -> object +0 */
void F_1002732d0(rc_ctx *c) { qi(c, 0x1018e9b40, 0); }
/* 0x100273330 GameService::QueryInterface const: class-info table 0x1018e9b40, this -> object +0 */
void F_100273330(rc_ctx *c) { qi(c, 0x1018e9b40, 0); }
/* 0x100275de0 GRAlphaSortingComponent::QueryInterface: class-info table 0x1018e9f50, this -> object +0 */
void F_100275de0(rc_ctx *c) { qi(c, 0x1018e9f50, 0); }
/* 0x100275e40 GRAlphaSortingComponent::QueryInterface const: class-info table 0x1018e9f50, this -> object +0 */
void F_100275e40(rc_ctx *c) { qi(c, 0x1018e9f50, 0); }
/* 0x100283910 GRBackground::QueryInterface: class-info table 0x1018ea3a0, this -> object +0 */
void F_100283910(rc_ctx *c) { qi(c, 0x1018ea3a0, 0); }
/* 0x100283970 GRBackground::QueryInterface const: class-info table 0x1018ea3a0, this -> object +0 */
void F_100283970(rc_ctx *c) { qi(c, 0x1018ea3a0, 0); }
/* 0x100284ca0 GRBackgroundService::QueryInterface: class-info table 0x1018ea7b0, this -> object +0 */
void F_100284ca0(rc_ctx *c) { qi(c, 0x1018ea7b0, 0); }
/* 0x100284d00 GRBackgroundService::QueryInterface const: class-info table 0x1018ea7b0, this -> object +0 */
void F_100284d00(rc_ctx *c) { qi(c, 0x1018ea7b0, 0); }
/* 0x100285d70 GRObjectEffectComponent::QueryInterface: class-info table 0x1018eabb8, this -> object +0 */
void F_100285d70(rc_ctx *c) { qi(c, 0x1018eabb8, 0); }
/* 0x100285dd0 GRObjectEffectComponent::QueryInterface const: class-info table 0x1018eabb8, this -> object +0 */
void F_100285dd0(rc_ctx *c) { qi(c, 0x1018eabb8, 0); }
/* 0x100296810 GRWorldTextManager::QueryInterface: class-info table 0x1018eb058, this -> object +0 */
void F_100296810(rc_ctx *c) { qi(c, 0x1018eb058, 0); }
/* 0x100296870 GRWorldTextManager::QueryInterface const: class-info table 0x1018eb058, this -> object +0 */
void F_100296870(rc_ctx *c) { qi(c, 0x1018eb058, 0); }
/* 0x10029ab50 GraphAssetFactory::QueryInterface: class-info table 0x1018eb460, this -> object +0 */
void F_10029ab50(rc_ctx *c) { qi(c, 0x1018eb460, 0); }
/* 0x10029abb0 GraphAssetFactory::QueryInterface const: class-info table 0x1018eb460, this -> object +0 */
void F_10029abb0(rc_ctx *c) { qi(c, 0x1018eb460, 0); }
/* 0x10029aca0 GraphAssetFactory::Response::QueryInterface: class-info table 0x1018eb868, this -> object +0 */
void F_10029aca0(rc_ctx *c) { qi(c, 0x1018eb868, 0); }
/* 0x10029ad00 GraphAssetFactory::Response::QueryInterface const: class-info table 0x1018eb868, this -> object +0 */
void F_10029ad00(rc_ctx *c) { qi(c, 0x1018eb868, 0); }
/* 0x10029bee0 GRComponent::QueryInterface: class-info table 0x1018ebc80, this -> object +0 */
void F_10029bee0(rc_ctx *c) { qi(c, 0x1018ebc80, 0); }
/* 0x10029bf40 GRComponent::QueryInterface const: class-info table 0x1018ebc80, this -> object +0 */
void F_10029bf40(rc_ctx *c) { qi(c, 0x1018ebc80, 0); }
/* 0x1002a9f90 MainService::QueryInterface: class-info table 0x1018ec4c8, this -> object +0 */
void F_1002a9f90(rc_ctx *c) { qi(c, 0x1018ec4c8, 0); }
/* 0x1002a9ff0 MainService::QueryInterface const: class-info table 0x1018ec4c8, this -> object +0 */
void F_1002a9ff0(rc_ctx *c) { qi(c, 0x1018ec4c8, 0); }
/* 0x1002ab980 PreloadHelper::QueryInterface: class-info table 0x1018ec8d0, this -> object +0 */
void F_1002ab980(rc_ctx *c) { qi(c, 0x1018ec8d0, 0); }
/* 0x1002ab9e0 PreloadHelper::QueryInterface const: class-info table 0x1018ec8d0, this -> object +0 */
void F_1002ab9e0(rc_ctx *c) { qi(c, 0x1018ec8d0, 0); }
/* 0x1002ad520 PreloadHelperResponse::QueryInterface: class-info table 0x1018ecce0, this -> object +0 */
void F_1002ad520(rc_ctx *c) { qi(c, 0x1018ecce0, 0); }
/* 0x1002ad580 PreloadHelperResponse::QueryInterface const: class-info table 0x1018ecce0, this -> object +0 */
void F_1002ad580(rc_ctx *c) { qi(c, 0x1018ecce0, 0); }
/* 0x1002ad720 PreloadItem::QueryInterface: class-info table 0x1018ed4f0, this -> object +0 */
void F_1002ad720(rc_ctx *c) { qi(c, 0x1018ed4f0, 0); }
/* 0x1002ad780 PreloadItem::QueryInterface const: class-info table 0x1018ed4f0, this -> object +0 */
void F_1002ad780(rc_ctx *c) { qi(c, 0x1018ed4f0, 0); }
/* 0x1002b2380 RSBootService::QueryInterface: class-info table 0x1018edd00, this -> object +0 */
void F_1002b2380(rc_ctx *c) { qi(c, 0x1018edd00, 0); }
/* 0x1002b23e0 RSBootService::QueryInterface const: class-info table 0x1018edd00, this -> object +0 */
void F_1002b23e0(rc_ctx *c) { qi(c, 0x1018edd00, 0); }
/* 0x1002b3140 RSRenderService::QueryInterface: class-info table 0x1018ee108, this -> object +0 */
void F_1002b3140(rc_ctx *c) { qi(c, 0x1018ee108, 0); }
/* 0x1002b31a0 RSRenderService::QueryInterface const: class-info table 0x1018ee108, this -> object +0 */
void F_1002b31a0(rc_ctx *c) { qi(c, 0x1018ee108, 0); }
/* 0x1002d5280 GCTickable::QueryInterface: class-info table 0x1018ee558, this -> object +0 */
void F_1002d5280(rc_ctx *c) { qi(c, 0x1018ee558, 0); }
/* 0x1002d52e0 GCTickable::QueryInterface const: class-info table 0x1018ee558, this -> object +0 */
void F_1002d52e0(rc_ctx *c) { qi(c, 0x1018ee558, 0); }
/* 0x1002e03e0 RSAudioService::QueryInterface: class-info table 0x1018eea08, this -> object +0 */
void F_1002e03e0(rc_ctx *c) { qi(c, 0x1018eea08, 0); }
/* 0x1002e0440 RSAudioService::QueryInterface const: class-info table 0x1018eea08, this -> object +0 */
void F_1002e0440(rc_ctx *c) { qi(c, 0x1018eea08, 0); }
/* 0x1003044c0 UIManagerI::QueryInterface: class-info table 0x1018f8bb0, this -> object +0 */
void F_1003044c0(rc_ctx *c) { qi(c, 0x1018f8bb0, 0); }
/* 0x100304510 UIManagerI::QueryInterface const: class-info table 0x1018f8bb0, this -> object +0 */
void F_100304510(rc_ctx *c) { qi(c, 0x1018f8bb0, 0); }
/* 0x100354b70 UIMenu_LAS_SongsList::QueryInterface: class-info table 0x1018eef98, this -> object -1008 */
void F_100354b70(rc_ctx *c) { qi(c, 0x1018eef98, -1008); }
/* 0x100354bd0 UIMenu_LAS_SongsList::QueryInterface const: class-info table 0x1018eef98, this -> object -1008 */
void F_100354bd0(rc_ctx *c) { qi(c, 0x1018eef98, -1008); }
/* 0x100367480 UIService::QueryInterface: class-info table 0x1018ef3c0, this -> object +0 */
void F_100367480(rc_ctx *c) { qi(c, 0x1018ef3c0, 0); }
/* 0x1003674e0 UIService::QueryInterface const: class-info table 0x1018ef3c0, this -> object +0 */
void F_1003674e0(rc_ctx *c) { qi(c, 0x1018ef3c0, 0); }
/* 0x100380a30 ResourceService::QueryInterface: class-info table 0x1018effe0, this -> object +0 */
void F_100380a30(rc_ctx *c) { qi(c, 0x1018effe0, 0); }
/* 0x100380a90 ResourceService::QueryInterface const: class-info table 0x1018effe0, this -> object +0 */
void F_100380a90(rc_ctx *c) { qi(c, 0x1018effe0, 0); }
/* 0x100385100 GRMinigameCamera::QueryInterface: class-info table 0x1018f0420, this -> object +0 */
void F_100385100(rc_ctx *c) { qi(c, 0x1018f0420, 0); }
/* 0x100385160 GRMinigameCamera::QueryInterface const: class-info table 0x1018f0420, this -> object +0 */
void F_100385160(rc_ctx *c) { qi(c, 0x1018f0420, 0); }
/* 0x1003a6970 OscilleratorScopeModel::QueryInterface: class-info table 0x1018f0868, this -> object +0 */
void F_1003a6970(rc_ctx *c) { qi(c, 0x1018f0868, 0); }
/* 0x1003a69d0 OscilleratorScopeModel::QueryInterface const: class-info table 0x1018f0868, this -> object +0 */
void F_1003a69d0(rc_ctx *c) { qi(c, 0x1018f0868, 0); }
/* 0x1003ac5d0 ReactiveEffectsService::QueryInterface: class-info table 0x1018f0cb8, this -> object +0 */
void F_1003ac5d0(rc_ctx *c) { qi(c, 0x1018f0cb8, 0); }
/* 0x1003ac630 ReactiveEffectsService::QueryInterface const: class-info table 0x1018f0cb8, this -> object +0 */
void F_1003ac630(rc_ctx *c) { qi(c, 0x1018f0cb8, 0); }
/* 0x1003ae9f0 GCColorShift::QueryInterface: class-info table 0x1018f10f0, this -> object +0 */
void F_1003ae9f0(rc_ctx *c) { qi(c, 0x1018f10f0, 0); }
/* 0x1003aea50 GCColorShift::QueryInterface const: class-info table 0x1018f10f0, this -> object +0 */
void F_1003aea50(rc_ctx *c) { qi(c, 0x1018f10f0, 0); }
/* 0x1003b1d90 GC2DAssetHelper::QueryInterface: class-info table 0x1018f1848, this -> object +0 */
void F_1003b1d90(rc_ctx *c) { qi(c, 0x1018f1848, 0); }
/* 0x1003b1df0 GC2DAssetHelper::QueryInterface const: class-info table 0x1018f1848, this -> object +0 */
void F_1003b1df0(rc_ctx *c) { qi(c, 0x1018f1848, 0); }
/* 0x1003c00d0 GCAccumulatedActor::QueryInterface: class-info table 0x1018f1ce0, this -> object +0 */
void F_1003c00d0(rc_ctx *c) { qi(c, 0x1018f1ce0, 0); }
/* 0x1003c0130 GCAccumulatedActor::QueryInterface const: class-info table 0x1018f1ce0, this -> object +0 */
void F_1003c0130(rc_ctx *c) { qi(c, 0x1018f1ce0, 0); }
/* 0x1003e09a0 GCPauseManager::QueryInterface: class-info table 0x1018f25e0, this -> object +0 */
void F_1003e09a0(rc_ctx *c) { qi(c, 0x1018f25e0, 0); }
/* 0x1003e0a00 GCPauseManager::QueryInterface const: class-info table 0x1018f25e0, this -> object +0 */
void F_1003e0a00(rc_ctx *c) { qi(c, 0x1018f25e0, 0); }
/* 0x1003f1160 GRCameraNarniaVenue::QueryInterface: class-info table 0x1018f2a60, this -> object +0 */
void F_1003f1160(rc_ctx *c) { qi(c, 0x1018f2a60, 0); }
/* 0x1003f11c0 GRCameraNarniaVenue::QueryInterface const: class-info table 0x1018f2a60, this -> object +0 */
void F_1003f11c0(rc_ctx *c) { qi(c, 0x1018f2a60, 0); }
/* 0x1003f93e0 FormLoader::QueryInterface: class-info table 0x1018f2ec8, this -> object +0 */
void F_1003f93e0(rc_ctx *c) { qi(c, 0x1018f2ec8, 0); }
/* 0x1003f9440 FormLoader::QueryInterface const: class-info table 0x1018f2ec8, this -> object +0 */
void F_1003f9440(rc_ctx *c) { qi(c, 0x1018f2ec8, 0); }
/* 0x100416cc0 ClipFadeManager::QueryInterface: class-info table 0x1018f3340, this -> object +0 */
void F_100416cc0(rc_ctx *c) { qi(c, 0x1018f3340, 0); }
/* 0x100416d20 ClipFadeManager::QueryInterface const: class-info table 0x1018f3340, this -> object +0 */
void F_100416d20(rc_ctx *c) { qi(c, 0x1018f3340, 0); }
/* 0x100479120 GEService::QueryInterface: class-info table 0x1018f3878, this -> object +0 */
void F_100479120(rc_ctx *c) { qi(c, 0x1018f3878, 0); }
/* 0x100479180 GEService::QueryInterface const: class-info table 0x1018f3878, this -> object +0 */
void F_100479180(rc_ctx *c) { qi(c, 0x1018f3878, 0); }
/* 0x10047bf10 NarniaPortalModel::QueryInterface: class-info table 0x1018f3ca8, this -> object +0 */
void F_10047bf10(rc_ctx *c) { qi(c, 0x1018f3ca8, 0); }
/* 0x10047bf70 NarniaPortalModel::QueryInterface const: class-info table 0x1018f3ca8, this -> object +0 */
void F_10047bf70(rc_ctx *c) { qi(c, 0x1018f3ca8, 0); }
/* 0x10047db50 SongListService::QueryInterface: class-info table 0x1018f40f0, this -> object +0 */
void F_10047db50(rc_ctx *c) { qi(c, 0x1018f40f0, 0); }
/* 0x10047dbb0 SongListService::QueryInterface const: class-info table 0x1018f40f0, this -> object +0 */
void F_10047dbb0(rc_ctx *c) { qi(c, 0x1018f40f0, 0); }
/* 0x100480710 NarniaService::QueryInterface: class-info table 0x1018f4500, this -> object +0 */
void F_100480710(rc_ctx *c) { qi(c, 0x1018f4500, 0); }
/* 0x100480770 NarniaService::QueryInterface const: class-info table 0x1018f4500, this -> object +0 */
void F_100480770(rc_ctx *c) { qi(c, 0x1018f4500, 0); }
/* 0x1004a2fa0 GRSL2Spawner::QueryInterface: class-info table 0x1018f49b8, this -> object +0 */
void F_1004a2fa0(rc_ctx *c) { qi(c, 0x1018f49b8, 0); }
/* 0x1004a3000 GRSL2Spawner::QueryInterface const: class-info table 0x1018f49b8, this -> object +0 */
void F_1004a3000(rc_ctx *c) { qi(c, 0x1018f49b8, 0); }
/* 0x1004cb650 NarniaFogModel::QueryInterface: class-info table 0x1018f4df0, this -> object +0 */
void F_1004cb650(rc_ctx *c) { qi(c, 0x1018f4df0, 0); }
/* 0x1004cb6b0 NarniaFogModel::QueryInterface const: class-info table 0x1018f4df0, this -> object +0 */
void F_1004cb6b0(rc_ctx *c) { qi(c, 0x1018f4df0, 0); }
/* 0x1004cc670 NarniaFogParticle::QueryInterface: class-info table 0x1018f5228, this -> object +0 */
void F_1004cc670(rc_ctx *c) { qi(c, 0x1018f5228, 0); }
/* 0x1004cc6d0 NarniaFogParticle::QueryInterface const: class-info table 0x1018f5228, this -> object +0 */
void F_1004cc6d0(rc_ctx *c) { qi(c, 0x1018f5228, 0); }
/* 0x100501a10 MissionService::QueryInterface: class-info table 0x1018f5660, this -> object +0 */
void F_100501a10(rc_ctx *c) { qi(c, 0x1018f5660, 0); }
/* 0x100501a70 MissionService::QueryInterface const: class-info table 0x1018f5660, this -> object +0 */
void F_100501a70(rc_ctx *c) { qi(c, 0x1018f5660, 0); }
/* 0x1005209c0 ToneService::QueryInterface: class-info table 0x1018f5aa8, this -> object +0 */
void F_1005209c0(rc_ctx *c) { qi(c, 0x1018f5aa8, 0); }
/* 0x100520a20 ToneService::QueryInterface const: class-info table 0x1018f5aa8, this -> object +0 */
void F_100520a20(rc_ctx *c) { qi(c, 0x1018f5aa8, 0); }
/* 0x10052fc60 StringTuner::QueryInterface: class-info table 0x1018f5f08, this -> object +0 */
void F_10052fc60(rc_ctx *c) { qi(c, 0x1018f5f08, 0); }
/* 0x10052fcc0 StringTuner::QueryInterface const: class-info table 0x1018f5f08, this -> object +0 */
void F_10052fcc0(rc_ctx *c) { qi(c, 0x1018f5f08, 0); }
/* 0x1005319e0 TunerService::QueryInterface: class-info table 0x1018f6340, this -> object +0 */
void F_1005319e0(rc_ctx *c) { qi(c, 0x1018f6340, 0); }
/* 0x100531a40 TunerService::QueryInterface const: class-info table 0x1018f6340, this -> object +0 */
void F_100531a40(rc_ctx *c) { qi(c, 0x1018f6340, 0); }
/* 0x1005339e0 InstrumentLoader::QueryInterface: class-info table 0x1018f6750, this -> object +0 */
void F_1005339e0(rc_ctx *c) { qi(c, 0x1018f6750, 0); }
/* 0x100533a40 InstrumentLoader::QueryInterface const: class-info table 0x1018f6750, this -> object +0 */
void F_100533a40(rc_ctx *c) { qi(c, 0x1018f6750, 0); }
/* 0x100534560 GRCrowdPoint::QueryInterface: class-info table 0x1018f6b58, this -> object +0 */
void F_100534560(rc_ctx *c) { qi(c, 0x1018f6b58, 0); }
/* 0x1005345c0 GRCrowdPoint::QueryInterface const: class-info table 0x1018f6b58, this -> object +0 */
void F_1005345c0(rc_ctx *c) { qi(c, 0x1018f6b58, 0); }
/* 0x100538c30 GearManager::QueryInterface: class-info table 0x1018f6fd0, this -> object +0 */
void F_100538c30(rc_ctx *c) { qi(c, 0x1018f6fd0, 0); }
/* 0x100538c90 GearManager::QueryInterface const: class-info table 0x1018f6fd0, this -> object +0 */
void F_100538c90(rc_ctx *c) { qi(c, 0x1018f6fd0, 0); }
/* 0x100539f10 GearService::QueryInterface: class-info table 0x1018f73e0, this -> object +0 */
void F_100539f10(rc_ctx *c) { qi(c, 0x1018f73e0, 0); }
/* 0x100539f70 GearService::QueryInterface const: class-info table 0x1018f73e0, this -> object +0 */
void F_100539f70(rc_ctx *c) { qi(c, 0x1018f73e0, 0); }
/* 0x100557690 FogService::QueryInterface: class-info table 0x1018f7840, this -> object +0 */
void F_100557690(rc_ctx *c) { qi(c, 0x1018f7840, 0); }
/* 0x1005576f0 FogService::QueryInterface const: class-info table 0x1018f7840, this -> object +0 */
void F_1005576f0(rc_ctx *c) { qi(c, 0x1018f7840, 0); }
/* 0x1005580f0 GlobalFogSettings::QueryInterface: class-info table 0x1018f7c48, this -> object +0 */
void F_1005580f0(rc_ctx *c) { qi(c, 0x1018f7c48, 0); }
/* 0x100558150 GlobalFogSettings::QueryInterface const: class-info table 0x1018f7c48, this -> object +0 */
void F_100558150(rc_ctx *c) { qi(c, 0x1018f7c48, 0); }
/* 0x100563810 ManifestManager::QueryInterface: class-info table 0x1018f80b0, this -> object +0 */
void F_100563810(rc_ctx *c) { qi(c, 0x1018f80b0, 0); }
/* 0x100563870 ManifestManager::QueryInterface const: class-info table 0x1018f80b0, this -> object +0 */
void F_100563870(rc_ctx *c) { qi(c, 0x1018f80b0, 0); }
/* 0x100581900 StatsService::QueryInterface: class-info table 0x1018f8500, this -> object +0 */
void F_100581900(rc_ctx *c) { qi(c, 0x1018f8500, 0); }
/* 0x100581960 StatsService::QueryInterface const: class-info table 0x1018f8500, this -> object +0 */
void F_100581960(rc_ctx *c) { qi(c, 0x1018f8500, 0); }
/* 0x1005c8be0 GRCameraLoft::QueryInterface: class-info table 0x1018f8fc8, this -> object +0 */
void F_1005c8be0(rc_ctx *c) { qi(c, 0x1018f8fc8, 0); }
/* 0x1005c8c40 GRCameraLoft::QueryInterface const: class-info table 0x1018f8fc8, this -> object +0 */
void F_1005c8c40(rc_ctx *c) { qi(c, 0x1018f8fc8, 0); }
/* 0x100607490 GEVideoScreenModel::QueryInterface: class-info table 0x1018f9490, this -> object +0 */
void F_100607490(rc_ctx *c) { qi(c, 0x1018f9490, 0); }
/* 0x1006074f0 GEVideoScreenModel::QueryInterface const: class-info table 0x1018f9490, this -> object +0 */
void F_1006074f0(rc_ctx *c) { qi(c, 0x1018f9490, 0); }
/* 0x100653e00 TunerProgressBuiltin::QueryInterface: class-info table 0x1018f9df8, this -> object +0 */
void F_100653e00(rc_ctx *c) { qi(c, 0x1018f9df8, 0); }
/* 0x100653e60 TunerProgressBuiltin::QueryInterface const: class-info table 0x1018f9df8, this -> object +0 */
void F_100653e60(rc_ctx *c) { qi(c, 0x1018f9df8, 0); }
/* 0x1006eb6d0 GameColorManager::QueryInterface: class-info table 0x1018fa430, this -> object +0 */
void F_1006eb6d0(rc_ctx *c) { qi(c, 0x1018fa430, 0); }
/* 0x1006eb730 GameColorManager::QueryInterface const: class-info table 0x1018fa430, this -> object +0 */
void F_1006eb730(rc_ctx *c) { qi(c, 0x1018fa430, 0); }
/* 0x10071c300 GRCameraSessionMode::QueryInterface: class-info table 0x1018fa8e0, this -> object +0 */
void F_10071c300(rc_ctx *c) { qi(c, 0x1018fa8e0, 0); }
/* 0x10071c360 GRCameraSessionMode::QueryInterface const: class-info table 0x1018fa8e0, this -> object +0 */
void F_10071c360(rc_ctx *c) { qi(c, 0x1018fa8e0, 0); }
/* 0x10074a010 GCAttachmentManager::QueryInterface: class-info table 0x1018fae88, this -> object +0 */
void F_10074a010(rc_ctx *c) { qi(c, 0x1018fae88, 0); }
/* 0x10074a070 GCAttachmentManager::QueryInterface const: class-info table 0x1018fae88, this -> object +0 */
void F_10074a070(rc_ctx *c) { qi(c, 0x1018fae88, 0); }
/* 0x10076ff60 UIMenu_ShopSongs::QueryInterface: class-info table 0x1018fb330, this -> object -1008 */
void F_10076ff60(rc_ctx *c) { qi(c, 0x1018fb330, -1008); }
/* 0x10076ffd0 UIMenu_ShopSongs::QueryInterface const: class-info table 0x1018fb330, this -> object -1008 */
void F_10076ffd0(rc_ctx *c) { qi(c, 0x1018fb330, -1008); }
/* 0x100775ae0 GRLoftAmbientLight::QueryInterface: class-info table 0x1018fb748, this -> object +0 */
void F_100775ae0(rc_ctx *c) { qi(c, 0x1018fb748, 0); }
/* 0x100775b40 GRLoftAmbientLight::QueryInterface const: class-info table 0x1018fb748, this -> object +0 */
void F_100775b40(rc_ctx *c) { qi(c, 0x1018fb748, 0); }
/* 0x100776230 GRLoftSpotLight::QueryInterface: class-info table 0x1018fbb80, this -> object +0 */
void F_100776230(rc_ctx *c) { qi(c, 0x1018fbb80, 0); }
/* 0x100776290 GRLoftSpotLight::QueryInterface const: class-info table 0x1018fbb80, this -> object +0 */
void F_100776290(rc_ctx *c) { qi(c, 0x1018fbb80, 0); }
/* 0x100777450 GRLoftState::QueryInterface: class-info table 0x1018fc828, this -> object +0 */
void F_100777450(rc_ctx *c) { qi(c, 0x1018fc828, 0); }
/* 0x1007774b0 GRLoftState::QueryInterface const: class-info table 0x1018fc828, this -> object +0 */
void F_1007774b0(rc_ctx *c) { qi(c, 0x1018fc828, 0); }
/* 0x100780ec0 LoftPostEffectsManager::QueryInterface: class-info table 0x1018fcc60, this -> object +0 */
void F_100780ec0(rc_ctx *c) { qi(c, 0x1018fcc60, 0); }
/* 0x100780f20 LoftPostEffectsManager::QueryInterface const: class-info table 0x1018fcc60, this -> object +0 */
void F_100780f20(rc_ctx *c) { qi(c, 0x1018fcc60, 0); }
/* 0x100781a10 RSVenueForkEmitterGear::QueryInterface: class-info table 0x1018fd068, this -> object +0 */
void F_100781a10(rc_ctx *c) { qi(c, 0x1018fd068, 0); }
/* 0x100781a70 RSVenueForkEmitterGear::QueryInterface const: class-info table 0x1018fd068, this -> object +0 */
void F_100781a70(rc_ctx *c) { qi(c, 0x1018fd068, 0); }
/* 0x1007899e0 UIMenu_ShopGear::QueryInterface: class-info table 0x1018fd8a8, this -> object -1008 */
void F_1007899e0(rc_ctx *c) { qi(c, 0x1018fd8a8, -1008); }
/* 0x100789a50 UIMenu_ShopGear::QueryInterface const: class-info table 0x1018fd8a8, this -> object -1008 */
void F_100789a50(rc_ctx *c) { qi(c, 0x1018fd8a8, -1008); }
/* 0x100798dc0 UIMenu_ShopPacks::QueryInterface: class-info table 0x1018fdcb0, this -> object -1008 */
void F_100798dc0(rc_ctx *c) { qi(c, 0x1018fdcb0, -1008); }
/* 0x100798e20 UIMenu_ShopPacks::QueryInterface const: class-info table 0x1018fdcb0, this -> object -1008 */
void F_100798e20(rc_ctx *c) { qi(c, 0x1018fdcb0, -1008); }
/* 0x1007aa3c0 GearPlacementNode::QueryInterface: class-info table 0x1018fe0f8, this -> object +0 */
void F_1007aa3c0(rc_ctx *c) { qi(c, 0x1018fe0f8, 0); }
/* 0x1007aa420 GearPlacementNode::QueryInterface const: class-info table 0x1018fe0f8, this -> object +0 */
void F_1007aa420(rc_ctx *c) { qi(c, 0x1018fe0f8, 0); }
/* 0x1007ee220 GearLava::QueryInterface: class-info table 0x1018fe5e0, this -> object +0 */
void F_1007ee220(rc_ctx *c) { qi(c, 0x1018fe5e0, 0); }
/* 0x1007ee280 GearLava::QueryInterface const: class-info table 0x1018fe5e0, this -> object +0 */
void F_1007ee280(rc_ctx *c) { qi(c, 0x1018fe5e0, 0); }
/* 0x1007f0f00 GearNifCache::QueryInterface: class-info table 0x1018fea18, this -> object +0 */
void F_1007f0f00(rc_ctx *c) { qi(c, 0x1018fea18, 0); }
/* 0x1007f0f60 GearNifCache::QueryInterface const: class-info table 0x1018fea18, this -> object +0 */
void F_1007f0f60(rc_ctx *c) { qi(c, 0x1018fea18, 0); }
/* 0x100811f30 GRVenueAmbientAnimations::QueryInterface: class-info table 0x1018fee70, this -> object +0 */
void F_100811f30(rc_ctx *c) { qi(c, 0x1018fee70, 0); }
/* 0x100811f90 GRVenueAmbientAnimations::QueryInterface const: class-info table 0x1018fee70, this -> object +0 */
void F_100811f90(rc_ctx *c) { qi(c, 0x1018fee70, 0); }
/* 0x10083c3c0 GRSoundIntensity::QueryInterface: class-info table 0x1018ff2c8, this -> object +0 */
void F_10083c3c0(rc_ctx *c) { qi(c, 0x1018ff2c8, 0); }
/* 0x10083c420 GRSoundIntensity::QueryInterface const: class-info table 0x1018ff2c8, this -> object +0 */
void F_10083c420(rc_ctx *c) { qi(c, 0x1018ff2c8, 0); }
/* 0x10083de80 GRVenuePointLight::QueryInterface: class-info table 0x1018ff700, this -> object +0 */
void F_10083de80(rc_ctx *c) { qi(c, 0x1018ff700, 0); }
/* 0x10083dee0 GRVenuePointLight::QueryInterface const: class-info table 0x1018ff700, this -> object +0 */
void F_10083dee0(rc_ctx *c) { qi(c, 0x1018ff700, 0); }
/* 0x10084f5b0 GRLoftCabinetScreen::QueryInterface: class-info table 0x1018ffb48, this -> object +0 */
void F_10084f5b0(rc_ctx *c) { qi(c, 0x1018ffb48, 0); }
/* 0x10084f610 GRLoftCabinetScreen::QueryInterface const: class-info table 0x1018ffb48, this -> object +0 */
void F_10084f610(rc_ctx *c) { qi(c, 0x1018ffb48, 0); }
/* 0x100863440 GCLdbChlgService::QueryInterface: class-info table 0x1018fffa0, this -> object +0 */
void F_100863440(rc_ctx *c) { qi(c, 0x1018fffa0, 0); }
/* 0x1008634a0 GCLdbChlgService::QueryInterface const: class-info table 0x1018fffa0, this -> object +0 */
void F_1008634a0(rc_ctx *c) { qi(c, 0x1018fffa0, 0); }
/* 0x100868190 GearChangeController::QueryInterface: class-info table 0x1019003c8, this -> object +0 */
void F_100868190(rc_ctx *c) { qi(c, 0x1019003c8, 0); }
/* 0x1008681f0 GearChangeController::QueryInterface const: class-info table 0x1019003c8, this -> object +0 */
void F_1008681f0(rc_ctx *c) { qi(c, 0x1019003c8, 0); }
/* 0x10086a4a0 GCGFxMouseEventHandler::QueryInterface: class-info table 0x101900bd8, this -> object +0 */
void F_10086a4a0(rc_ctx *c) { qi(c, 0x101900bd8, 0); }
/* 0x10086a500 GCGFxMouseEventHandler::QueryInterface const: class-info table 0x101900bd8, this -> object +0 */
void F_10086a500(rc_ctx *c) { qi(c, 0x101900bd8, 0); }
/* 0x10087fde0 AchievementService::QueryInterface: class-info table 0x101901068, this -> object +0 */
void F_10087fde0(rc_ctx *c) { qi(c, 0x101901068, 0); }
/* 0x10087fe40 AchievementService::QueryInterface const: class-info table 0x101901068, this -> object +0 */
void F_10087fe40(rc_ctx *c) { qi(c, 0x101901068, 0); }
/* 0x1008a7570 PrizeService::QueryInterface: class-info table 0x1019019e0, this -> object +0 */
void F_1008a7570(rc_ctx *c) { qi(c, 0x1019019e0, 0); }
/* 0x1008a75d0 PrizeService::QueryInterface const: class-info table 0x1019019e0, this -> object +0 */
void F_1008a75d0(rc_ctx *c) { qi(c, 0x1019019e0, 0); }
/* 0x1008a8530 GCKinectEventHandler::QueryInterface: class-info table 0x101902208, this -> object +0 */
void F_1008a8530(rc_ctx *c) { qi(c, 0x101902208, 0); }
/* 0x1008a8590 GCKinectEventHandler::QueryInterface const: class-info table 0x101902208, this -> object +0 */
void F_1008a8590(rc_ctx *c) { qi(c, 0x101902208, 0); }
/* 0x1008c4140 FavoritesService::QueryInterface: class-info table 0x1019026b0, this -> object +0 */
void F_1008c4140(rc_ctx *c) { qi(c, 0x1019026b0, 0); }
/* 0x1008c41a0 FavoritesService::QueryInterface const: class-info table 0x1019026b0, this -> object +0 */
void F_1008c41a0(rc_ctx *c) { qi(c, 0x1019026b0, 0); }
/* 0x100a048c0 RSConnectExtension_RDV::QueryInterface: class-info table 0x101902b88, this -> object +0 */
void F_100a048c0(rc_ctx *c) { qi(c, 0x101902b88, 0); }
/* 0x100a04920 RSConnectExtension_RDV::QueryInterface const: class-info table 0x101902b88, this -> object +0 */
void F_100a04920(rc_ctx *c) { qi(c, 0x101902b88, 0); }
/* 0x100a05710 RSConnectService_Steam::QueryInterface: class-info table 0x101902f90, this -> object +0 */
void F_100a05710(rc_ctx *c) { qi(c, 0x101902f90, 0); }
/* 0x100a05770 RSConnectService_Steam::QueryInterface const: class-info table 0x101902f90, this -> object +0 */
void F_100a05770(rc_ctx *c) { qi(c, 0x101902f90, 0); }
/* 0x100a07ce0 RSSteamService::QueryInterface: class-info table 0x1019033f8, this -> object +0 */
void F_100a07ce0(rc_ctx *c) { qi(c, 0x1019033f8, 0); }
/* 0x100a07d40 RSSteamService::QueryInterface const: class-info table 0x1019033f8, this -> object +0 */
void F_100a07d40(rc_ctx *c) { qi(c, 0x1019033f8, 0); }
/* 0x100a0ea40 RSDLCService_Steam::QueryInterface: class-info table 0x101903808, this -> object +0 */
void F_100a0ea40(rc_ctx *c) { qi(c, 0x101903808, 0); }
/* 0x100a0eaa0 RSDLCService_Steam::QueryInterface const: class-info table 0x101903808, this -> object +0 */
void F_100a0eaa0(rc_ctx *c) { qi(c, 0x101903808, 0); }
/* 0x100a108f0 RSProfileService_MacOS::QueryInterface: class-info table 0x101903c10, this -> object +0 */
void F_100a108f0(rc_ctx *c) { qi(c, 0x101903c10, 0); }
/* 0x100a10950 RSProfileService_MacOS::QueryInterface const: class-info table 0x101903c10, this -> object +0 */
void F_100a10950(rc_ctx *c) { qi(c, 0x101903c10, 0); }
/* 0x100a2f410 RSSteamAchievement::QueryInterface: class-info table 0x1019042c0, this -> object +0 */
void F_100a2f410(rc_ctx *c) { qi(c, 0x1019042c0, 0); }
/* 0x100a2f470 RSSteamAchievement::QueryInterface const: class-info table 0x1019042c0, this -> object +0 */
void F_100a2f470(rc_ctx *c) { qi(c, 0x1019042c0, 0); }
/* 0x100a30da0 RSSteamActivityFeed::QueryInterface: class-info table 0x1019046c8, this -> object +0 */
void F_100a30da0(rc_ctx *c) { qi(c, 0x1019046c8, 0); }
/* 0x100a30e00 RSSteamActivityFeed::QueryInterface const: class-info table 0x1019046c8, this -> object +0 */
void F_100a30e00(rc_ctx *c) { qi(c, 0x1019046c8, 0); }
/* 0x100a31460 RSSteamCloudFile::QueryInterface: class-info table 0x101904ad0, this -> object +0 */
void F_100a31460(rc_ctx *c) { qi(c, 0x101904ad0, 0); }
/* 0x100a314c0 RSSteamCloudFile::QueryInterface const: class-info table 0x101904ad0, this -> object +0 */
void F_100a314c0(rc_ctx *c) { qi(c, 0x101904ad0, 0); }
/* 0x100a33510 RSSteamDownloadableContent::QueryInterface: class-info table 0x101904ed8, this -> object +0 */
void F_100a33510(rc_ctx *c) { qi(c, 0x101904ed8, 0); }
/* 0x100a33570 RSSteamDownloadableContent::QueryInterface const: class-info table 0x101904ed8, this -> object +0 */
void F_100a33570(rc_ctx *c) { qi(c, 0x101904ed8, 0); }
/* 0x100a35e30 RSSteamLeaderboards::QueryInterface: class-info table 0x1019052e0, this -> object +0 */
void F_100a35e30(rc_ctx *c) { qi(c, 0x1019052e0, 0); }
/* 0x100a35e90 RSSteamLeaderboards::QueryInterface const: class-info table 0x1019052e0, this -> object +0 */
void F_100a35e90(rc_ctx *c) { qi(c, 0x1019052e0, 0); }
/* 0x100a35f60 RSSteamLeaderboardRequest::QueryInterface: class-info table 0x1019056e8, this -> object +0 */
void F_100a35f60(rc_ctx *c) { qi(c, 0x1019056e8, 0); }
/* 0x100a35fc0 RSSteamLeaderboardRequest::QueryInterface const: class-info table 0x1019056e8, this -> object +0 */
void F_100a35fc0(rc_ctx *c) { qi(c, 0x1019056e8, 0); }
/* 0x100a36090 RSSteamLeaderboardRequestUpload::QueryInterface: class-info table 0x101905af0, this -> object +0 */
void F_100a36090(rc_ctx *c) { qi(c, 0x101905af0, 0); }
/* 0x100a360f0 RSSteamLeaderboardRequestUpload::QueryInterface const: class-info table 0x101905af0, this -> object +0 */
void F_100a360f0(rc_ctx *c) { qi(c, 0x101905af0, 0); }
/* 0x100a361c0 RSSteamLeaderboardRequestDownload::QueryInterface: class-info table 0x101905ef8, this -> object +0 */
void F_100a361c0(rc_ctx *c) { qi(c, 0x101905ef8, 0); }
/* 0x100a36220 RSSteamLeaderboardRequestDownload::QueryInterface const: class-info table 0x101905ef8, this -> object +0 */
void F_100a36220(rc_ctx *c) { qi(c, 0x101905ef8, 0); }
/* 0x100a3bf90 RSSteamWebApi::QueryInterface: class-info table 0x101906708, this -> object +0 */
void F_100a3bf90(rc_ctx *c) { qi(c, 0x101906708, 0); }
/* 0x100a3bff0 RSSteamWebApi::QueryInterface const: class-info table 0x101906708, this -> object +0 */
void F_100a3bff0(rc_ctx *c) { qi(c, 0x101906708, 0); }
/* 0x100a53270 FrkService::QueryInterface: class-info table 0x101906b98, this -> object +0 */
void F_100a53270(rc_ctx *c) { qi(c, 0x101906b98, 0); }
/* 0x100a532d0 FrkService::QueryInterface const: class-info table 0x101906b98, this -> object +0 */
void F_100a532d0(rc_ctx *c) { qi(c, 0x101906b98, 0); }
/* 0x100b67dd0 GFxAssetTaskAssetFactory::QueryInterface: class-info table 0x101908e30, this -> object +0 */
void F_100b67dd0(rc_ctx *c) { qi(c, 0x101908e30, 0); }
/* 0x100b67e30 GFxAssetTaskAssetFactory::QueryInterface const: class-info table 0x101908e30, this -> object +0 */
void F_100b67e30(rc_ctx *c) { qi(c, 0x101908e30, 0); }
/* 0x100b6cd90 GFxInputRouterModel::QueryInterface: class-info table 0x101909238, this -> object +0 */
void F_100b6cd90(rc_ctx *c) { qi(c, 0x101909238, 0); }
/* 0x100b6cdf0 GFxInputRouterModel::QueryInterface const: class-info table 0x101909238, this -> object +0 */
void F_100b6cdf0(rc_ctx *c) { qi(c, 0x101909238, 0); }
/* 0x100b74ff0 GFxMovieModel::QueryInterface: class-info table 0x101909670, this -> object +0 */
void F_100b74ff0(rc_ctx *c) { qi(c, 0x101909670, 0); }
/* 0x100b75050 GFxMovieModel::QueryInterface const: class-info table 0x101909670, this -> object +0 */
void F_100b75050(rc_ctx *c) { qi(c, 0x101909670, 0); }
/* 0x100b7e8e0 GFxService::QueryInterface: class-info table 0x101909aa8, this -> object +0 */
void F_100b7e8e0(rc_ctx *c) { qi(c, 0x101909aa8, 0); }
/* 0x100b7e940 GFxService::QueryInterface const: class-info table 0x101909aa8, this -> object +0 */
void F_100b7e940(rc_ctx *c) { qi(c, 0x101909aa8, 0); }
/* 0x1011599b0 sub:LuaResumeCoroutineTask::QueryInterface: class-info table 0x1019224c0, this -> object +0 */
void F_1011599b0(rc_ctx *c) { qi(c, 0x1019224c0, 0); }
/* 0x101159a00 sub:LuaResumeCoroutineTask::QueryInterface const: class-info table 0x1019224c0, this -> object +0 */
void F_101159a00(rc_ctx *c) { qi(c, 0x1019224c0, 0); }
/* 0x10114ff10 LuaScriptFactory::QueryInterface: class-info table 0x1019228c8, this -> object +0 */
void F_10114ff10(rc_ctx *c) { qi(c, 0x1019228c8, 0); }
/* 0x10114ff70 LuaScriptFactory::QueryInterface const: class-info table 0x1019228c8, this -> object +0 */
void F_10114ff70(rc_ctx *c) { qi(c, 0x1019228c8, 0); }
/* 0x1011598e0 SchedulerLua::QueryInterface: class-info table 0x1019230e0, this -> object +0 */
void F_1011598e0(rc_ctx *c) { qi(c, 0x1019230e0, 0); }
/* 0x101159940 SchedulerLua::QueryInterface const: class-info table 0x1019230e0, this -> object +0 */
void F_101159940(rc_ctx *c) { qi(c, 0x1019230e0, 0); }
/* 0x101164650 AttachNifData::QueryInterface: class-info table 0x101923508, this -> object +0 */
void F_101164650(rc_ctx *c) { qi(c, 0x101923508, 0); }
/* 0x1011646b0 AttachNifData::QueryInterface const: class-info table 0x101923508, this -> object +0 */
void F_1011646b0(rc_ctx *c) { qi(c, 0x101923508, 0); }
/* 0x101169190 CameraService::QueryInterface: class-info table 0x101923910, this -> object +0 */
void F_101169190(rc_ctx *c) { qi(c, 0x101923910, 0); }
/* 0x1011691f0 CameraService::QueryInterface const: class-info table 0x101923910, this -> object +0 */
void F_1011691f0(rc_ctx *c) { qi(c, 0x101923910, 0); }
/* 0x101174280 LightService::QueryInterface: class-info table 0x101925950, this -> object +0 */
void F_101174280(rc_ctx *c) { qi(c, 0x101925950, 0); }
/* 0x1011742e0 LightService::QueryInterface const: class-info table 0x101925950, this -> object +0 */
void F_1011742e0(rc_ctx *c) { qi(c, 0x101925950, 0); }
/* 0x101176890 NIFFactory::QueryInterface: class-info table 0x101925d58, this -> object +0 */
void F_101176890(rc_ctx *c) { qi(c, 0x101925d58, 0); }
/* 0x1011768f0 NIFFactory::QueryInterface const: class-info table 0x101925d58, this -> object +0 */
void F_1011768f0(rc_ctx *c) { qi(c, 0x101925d58, 0); }
/* 0x101178f30 RenderService::QueryInterface: class-info table 0x101926568, this -> object +0 */
void F_101178f30(rc_ctx *c) { qi(c, 0x101926568, 0); }
/* 0x101178f90 RenderService::QueryInterface const: class-info table 0x101926568, this -> object +0 */
void F_101178f90(rc_ctx *c) { qi(c, 0x101926568, 0); }
/* 0x10117fc40 SceneGraphCache::QueryInterface: class-info table 0x101926980, this -> object +0 */
void F_10117fc40(rc_ctx *c) { qi(c, 0x101926980, 0); }
/* 0x10117fca0 SceneGraphCache::QueryInterface const: class-info table 0x101926980, this -> object +0 */
void F_10117fca0(rc_ctx *c) { qi(c, 0x101926980, 0); }
/* 0x101199950 SceneGraphService::QueryInterface: class-info table 0x101927198, this -> object +0 */
void F_101199950(rc_ctx *c) { qi(c, 0x101927198, 0); }
/* 0x1011999b0 SceneGraphService::QueryInterface const: class-info table 0x101927198, this -> object +0 */
void F_1011999b0(rc_ctx *c) { qi(c, 0x101927198, 0); }
/* 0x1011a2f00 ShaderService::QueryInterface: class-info table 0x1019275a0, this -> object +0 */
void F_1011a2f00(rc_ctx *c) { qi(c, 0x1019275a0, 0); }
/* 0x1011a2f60 ShaderService::QueryInterface const: class-info table 0x1019275a0, this -> object +0 */
void F_1011a2f60(rc_ctx *c) { qi(c, 0x1019275a0, 0); }
/* 0x1011a68a0 InputActionMessage::QueryInterface: class-info table 0x101927db0, this -> object +0 */
void F_1011a68a0(rc_ctx *c) { qi(c, 0x101927db0, 0); }
/* 0x1011a6900 InputActionMessage::QueryInterface const: class-info table 0x101927db0, this -> object +0 */
void F_1011a6900(rc_ctx *c) { qi(c, 0x101927db0, 0); }
/* 0x1011a8290 InputHandler::QueryInterface: class-info table 0x1019281b8, this -> object +0 */
void F_1011a8290(rc_ctx *c) { qi(c, 0x1019281b8, 0); }
/* 0x1011a82f0 InputHandler::QueryInterface const: class-info table 0x1019281b8, this -> object +0 */
void F_1011a82f0(rc_ctx *c) { qi(c, 0x1019281b8, 0); }
/* 0x1011aff90 InputService::QueryInterface: class-info table 0x1019285f0, this -> object +0 */
void F_1011aff90(rc_ctx *c) { qi(c, 0x1019285f0, 0); }
/* 0x1011afff0 InputService::QueryInterface const: class-info table 0x1019285f0, this -> object +0 */
void F_1011afff0(rc_ctx *c) { qi(c, 0x1019285f0, 0); }
/* 0x1011bdf20 InputStickMessage::QueryInterface: class-info table 0x101928b50, this -> object +0 */
void F_1011bdf20(rc_ctx *c) { qi(c, 0x101928b50, 0); }
/* 0x1011bdf80 InputStickMessage::QueryInterface const: class-info table 0x101928b50, this -> object +0 */
void F_1011bdf80(rc_ctx *c) { qi(c, 0x101928b50, 0); }
/* 0x1011be610 KeyDownMessage::QueryInterface: class-info table 0x101928f58, this -> object +0 */
void F_1011be610(rc_ctx *c) { qi(c, 0x101928f58, 0); }
/* 0x1011be670 KeyDownMessage::QueryInterface const: class-info table 0x101928f58, this -> object +0 */
void F_1011be670(rc_ctx *c) { qi(c, 0x101928f58, 0); }
/* 0x1011be6e0 KeyUpMessage::QueryInterface: class-info table 0x101929360, this -> object +0 */
void F_1011be6e0(rc_ctx *c) { qi(c, 0x101929360, 0); }
/* 0x1011be740 KeyUpMessage::QueryInterface const: class-info table 0x101929360, this -> object +0 */
void F_1011be740(rc_ctx *c) { qi(c, 0x101929360, 0); }
/* 0x1011bf390 MouseMoveMessage::QueryInterface: class-info table 0x101929b70, this -> object +0 */
void F_1011bf390(rc_ctx *c) { qi(c, 0x101929b70, 0); }
/* 0x1011bf3f0 MouseMoveMessage::QueryInterface const: class-info table 0x101929b70, this -> object +0 */
void F_1011bf3f0(rc_ctx *c) { qi(c, 0x101929b70, 0); }
/* 0x1011bf460 MouseDownMessage::QueryInterface: class-info table 0x101929f78, this -> object +0 */
void F_1011bf460(rc_ctx *c) { qi(c, 0x101929f78, 0); }
/* 0x1011bf4c0 MouseDownMessage::QueryInterface const: class-info table 0x101929f78, this -> object +0 */
void F_1011bf4c0(rc_ctx *c) { qi(c, 0x101929f78, 0); }
/* 0x1011bf530 MouseUpMessage::QueryInterface: class-info table 0x10192a380, this -> object +0 */
void F_1011bf530(rc_ctx *c) { qi(c, 0x10192a380, 0); }
/* 0x1011bf590 MouseUpMessage::QueryInterface const: class-info table 0x10192a380, this -> object +0 */
void F_1011bf590(rc_ctx *c) { qi(c, 0x10192a380, 0); }
/* 0x1011d2470 Entity::QueryInterface: class-info table 0x10192abd0, this -> object +0 */
void F_1011d2470(rc_ctx *c) { qi(c, 0x10192abd0, 0); }
/* 0x1011d24d0 Entity::QueryInterface const: class-info table 0x10192abd0, this -> object +0 */
void F_1011d24d0(rc_ctx *c) { qi(c, 0x10192abd0, 0); }
/* 0x100097be0 sub:EntityChangeMessage::QueryInterface: class-info table 0x10192afd8, this -> object +0 */
void F_100097be0(rc_ctx *c) { qi(c, 0x10192afd8, 0); }
/* 0x100097c30 sub:EntityChangeMessage::QueryInterface const: class-info table 0x10192afd8, this -> object +0 */
void F_100097c30(rc_ctx *c) { qi(c, 0x10192afd8, 0); }
/* 0x1011d6d00 EntityFactory::QueryInterface: class-info table 0x10192b3e0, this -> object +0 */
void F_1011d6d00(rc_ctx *c) { qi(c, 0x10192b3e0, 0); }
/* 0x1011d6d60 EntityFactory::QueryInterface const: class-info table 0x10192b3e0, this -> object +0 */
void F_1011d6d60(rc_ctx *c) { qi(c, 0x10192b3e0, 0); }
/* 0x1011d8e50 EntityLoadRequest::QueryInterface: class-info table 0x10192c410, this -> object +0 */
void F_1011d8e50(rc_ctx *c) { qi(c, 0x10192c410, 0); }
/* 0x1011d8eb0 EntityLoadRequest::QueryInterface const: class-info table 0x10192c410, this -> object +0 */
void F_1011d8eb0(rc_ctx *c) { qi(c, 0x10192c410, 0); }
/* 0x1011d8f20 EntityLoadResult::QueryInterface: class-info table 0x10192c818, this -> object +0 */
void F_1011d8f20(rc_ctx *c) { qi(c, 0x10192c818, 0); }
/* 0x1011d8f80 EntityLoadResult::QueryInterface const: class-info table 0x10192c818, this -> object +0 */
void F_1011d8f80(rc_ctx *c) { qi(c, 0x10192c818, 0); }
/* 0x1011d8ff0 EntityPreloadRequest::QueryInterface: class-info table 0x10192cc20, this -> object +0 */
void F_1011d8ff0(rc_ctx *c) { qi(c, 0x10192cc20, 0); }
/* 0x1011d9050 EntityPreloadRequest::QueryInterface const: class-info table 0x10192cc20, this -> object +0 */
void F_1011d9050(rc_ctx *c) { qi(c, 0x10192cc20, 0); }
/* 0x1011d90c0 EntityPreloadResponse::QueryInterface: class-info table 0x10192d028, this -> object +0 */
void F_1011d90c0(rc_ctx *c) { qi(c, 0x10192d028, 0); }
/* 0x1011d9120 EntityPreloadResponse::QueryInterface const: class-info table 0x10192d028, this -> object +0 */
void F_1011d9120(rc_ctx *c) { qi(c, 0x10192d028, 0); }
/* 0x1011df0a0 EntityLoaderService::QueryInterface: class-info table 0x10192d430, this -> object +0 */
void F_1011df0a0(rc_ctx *c) { qi(c, 0x10192d430, 0); }
/* 0x1011df100 EntityLoaderService::QueryInterface const: class-info table 0x10192d430, this -> object +0 */
void F_1011df100(rc_ctx *c) { qi(c, 0x10192d430, 0); }
/* 0x1011e8ad0 EntityManager::QueryInterface: class-info table 0x10192d838, this -> object +0 */
void F_1011e8ad0(rc_ctx *c) { qi(c, 0x10192d838, 0); }
/* 0x1011e8b30 EntityManager::QueryInterface const: class-info table 0x10192d838, this -> object +0 */
void F_1011e8b30(rc_ctx *c) { qi(c, 0x10192d838, 0); }
/* 0x1011ebcf0 EntitySetLoadState::QueryInterface: class-info table 0x10192dc40, this -> object +0 */
void F_1011ebcf0(rc_ctx *c) { qi(c, 0x10192dc40, 0); }
/* 0x1011ebd50 EntitySetLoadState::QueryInterface const: class-info table 0x10192dc40, this -> object +0 */
void F_1011ebd50(rc_ctx *c) { qi(c, 0x10192dc40, 0); }
/* 0x1011d2e70 sub:EventMessage::QueryInterface: class-info table 0x10192e058, this -> object +0 */
void F_1011d2e70(rc_ctx *c) { qi(c, 0x10192e058, 0); }
/* 0x1011d2ec0 sub:EventMessage::QueryInterface const: class-info table 0x10192e058, this -> object +0 */
void F_1011d2ec0(rc_ctx *c) { qi(c, 0x10192e058, 0); }
/* 0x1011fa930 FlatModelFactory::QueryInterface: class-info table 0x10192e460, this -> object +0 */
void F_1011fa930(rc_ctx *c) { qi(c, 0x10192e460, 0); }
/* 0x1011fa990 FlatModelFactory::QueryInterface const: class-info table 0x10192e460, this -> object +0 */
void F_1011fa990(rc_ctx *c) { qi(c, 0x10192e460, 0); }
/* 0x1012073b0 FlatModelManager::QueryInterface: class-info table 0x10192f480, this -> object +0 */
void F_1012073b0(rc_ctx *c) { qi(c, 0x10192f480, 0); }
/* 0x101207410 FlatModelManager::QueryInterface const: class-info table 0x10192f480, this -> object +0 */
void F_101207410(rc_ctx *c) { qi(c, 0x10192f480, 0); }
/* 0x10120f780 PendingBehavior::QueryInterface: class-info table 0x10192fca8, this -> object +0 */
void F_10120f780(rc_ctx *c) { qi(c, 0x10192fca8, 0); }
/* 0x10120f7e0 PendingBehavior::QueryInterface const: class-info table 0x10192fca8, this -> object +0 */
void F_10120f7e0(rc_ctx *c) { qi(c, 0x10192fca8, 0); }
/* 0x1012481f0 Scheduler::QueryInterface: class-info table 0x1019304f8, this -> object +0 */
void F_1012481f0(rc_ctx *c) { qi(c, 0x1019304f8, 0); }
/* 0x101248250 Scheduler::QueryInterface const: class-info table 0x1019304f8, this -> object +0 */
void F_101248250(rc_ctx *c) { qi(c, 0x1019304f8, 0); }
/* 0x10124c970 ScriptFactoryResponse::QueryInterface: class-info table 0x101930d20, this -> object +0 */
void F_10124c970(rc_ctx *c) { qi(c, 0x101930d20, 0); }
/* 0x10124c9d0 ScriptFactoryResponse::QueryInterface const: class-info table 0x101930d20, this -> object +0 */
void F_10124c9d0(rc_ctx *c) { qi(c, 0x101930d20, 0); }
/* 0x10125dca0 WorldFactory::QueryInterface: class-info table 0x101931128, this -> object +0 */
void F_10125dca0(rc_ctx *c) { qi(c, 0x101931128, 0); }
/* 0x10125dd00 WorldFactory::QueryInterface const: class-info table 0x101931128, this -> object +0 */
void F_10125dd00(rc_ctx *c) { qi(c, 0x101931128, 0); }
/* 0x10126ddb0 AnimationService::QueryInterface: class-info table 0x101932958, this -> object +0 */
void F_10126ddb0(rc_ctx *c) { qi(c, 0x101932958, 0); }
/* 0x10126de10 AnimationService::QueryInterface const: class-info table 0x101932958, this -> object +0 */
void F_10126de10(rc_ctx *c) { qi(c, 0x101932958, 0); }
/* 0x101278a70 KFMFactory::QueryInterface: class-info table 0x101932d80, this -> object +0 */
void F_101278a70(rc_ctx *c) { qi(c, 0x101932d80, 0); }
/* 0x101278ad0 KFMFactory::QueryInterface const: class-info table 0x101932d80, this -> object +0 */
void F_101278ad0(rc_ctx *c) { qi(c, 0x101932d80, 0); }
/* 0x101278bc0 KFMFactory::ResponseData::QueryInterface: class-info table 0x101933188, this -> object +0 */
void F_101278bc0(rc_ctx *c) { qi(c, 0x101933188, 0); }
/* 0x101278c20 KFMFactory::ResponseData::QueryInterface const: class-info table 0x101933188, this -> object +0 */
void F_101278c20(rc_ctx *c) { qi(c, 0x101933188, 0); }
/* 0x10127a6b0 AssetCacheResponse::QueryInterface: class-info table 0x101933da0, this -> object +0 */
void F_10127a6b0(rc_ctx *c) { qi(c, 0x101933da0, 0); }
/* 0x10127a710 AssetCacheResponse::QueryInterface const: class-info table 0x101933da0, this -> object +0 */
void F_10127a710(rc_ctx *c) { qi(c, 0x101933da0, 0); }
/* 0x10127c7d0 AssetConfigService::QueryInterface: class-info table 0x1019341a8, this -> object +0 */
void F_10127c7d0(rc_ctx *c) { qi(c, 0x1019341a8, 0); }
/* 0x10127c830 AssetConfigService::QueryInterface const: class-info table 0x1019341a8, this -> object +0 */
void F_10127c830(rc_ctx *c) { qi(c, 0x1019341a8, 0); }
/* 0x101283ce0 AssetFactoryManager::QueryInterface: class-info table 0x101934678, this -> object +0 */
void F_101283ce0(rc_ctx *c) { qi(c, 0x101934678, 0); }
/* 0x101283d40 AssetFactoryManager::QueryInterface const: class-info table 0x101934678, this -> object +0 */
void F_101283d40(rc_ctx *c) { qi(c, 0x101934678, 0); }
/* 0x101285f50 AssetLoadRequest::QueryInterface: class-info table 0x101934ab0, this -> object +0 */
void F_101285f50(rc_ctx *c) { qi(c, 0x101934ab0, 0); }
/* 0x101285fb0 AssetLoadRequest::QueryInterface const: class-info table 0x101934ab0, this -> object +0 */
void F_101285fb0(rc_ctx *c) { qi(c, 0x101934ab0, 0); }
/* 0x1012862e0 AssetLoadResponse::QueryInterface: class-info table 0x101934eb8, this -> object +0 */
void F_1012862e0(rc_ctx *c) { qi(c, 0x101934eb8, 0); }
/* 0x101286340 AssetLoadResponse::QueryInterface const: class-info table 0x101934eb8, this -> object +0 */
void F_101286340(rc_ctx *c) { qi(c, 0x101934eb8, 0); }
/* 0x101294970 sub:AssetLocatorRequest::QueryInterface: class-info table 0x1019352c0, this -> object +0 */
void F_101294970(rc_ctx *c) { qi(c, 0x1019352c0, 0); }
/* 0x1012949c0 sub:AssetLocatorRequest::QueryInterface const: class-info table 0x1019352c0, this -> object +0 */
void F_1012949c0(rc_ctx *c) { qi(c, 0x1019352c0, 0); }
/* 0x101293360 sub:AssetConfigurationMessage::QueryInterface: class-info table 0x1019356c8, this -> object +0 */
void F_101293360(rc_ctx *c) { qi(c, 0x1019356c8, 0); }
/* 0x1012933b0 sub:AssetConfigurationMessage::QueryInterface const: class-info table 0x1019356c8, this -> object +0 */
void F_1012933b0(rc_ctx *c) { qi(c, 0x1019356c8, 0); }
/* 0x1000e5dd0 sub:AssetLocatorResponse::QueryInterface: class-info table 0x101935ad0, this -> object +0 */
void F_1000e5dd0(rc_ctx *c) { qi(c, 0x101935ad0, 0); }
/* 0x1000e5e20 sub:AssetLocatorResponse::QueryInterface const: class-info table 0x101935ad0, this -> object +0 */
void F_1000e5e20(rc_ctx *c) { qi(c, 0x101935ad0, 0); }
/* 0x101289620 AssetTagsInfoResponse::QueryInterface: class-info table 0x101935ed8, this -> object +0 */
void F_101289620(rc_ctx *c) { qi(c, 0x101935ed8, 0); }
/* 0x101289680 AssetTagsInfoResponse::QueryInterface const: class-info table 0x101935ed8, this -> object +0 */
void F_101289680(rc_ctx *c) { qi(c, 0x101935ed8, 0); }
/* 0x101292140 AssetLocatorService::QueryInterface: class-info table 0x1019362e0, this -> object +0 */
void F_101292140(rc_ctx *c) { qi(c, 0x1019362e0, 0); }
/* 0x1012921a0 AssetLocatorService::QueryInterface const: class-info table 0x1019362e0, this -> object +0 */
void F_1012921a0(rc_ctx *c) { qi(c, 0x1019362e0, 0); }
/* 0x1012b5b30 ConfigManager::QueryInterface: class-info table 0x101936bf8, this -> object +0 */
void F_1012b5b30(rc_ctx *c) { qi(c, 0x101936bf8, 0); }
/* 0x1012b5b90 ConfigManager::QueryInterface const: class-info table 0x101936bf8, this -> object +0 */
void F_1012b5b90(rc_ctx *c) { qi(c, 0x101936bf8, 0); }
/* 0x1012bb8f0 SectionEntry::QueryInterface: class-info table 0x101937000, this -> object +0 */
void F_1012bb8f0(rc_ctx *c) { qi(c, 0x101937000, 0); }
/* 0x1012bb950 SectionEntry::QueryInterface const: class-info table 0x101937000, this -> object +0 */
void F_1012bb950(rc_ctx *c) { qi(c, 0x101937000, 0); }
/* 0x1012c1a90 ConfigSource::QueryInterface: class-info table 0x101937410, this -> object +0 */
void F_1012c1a90(rc_ctx *c) { qi(c, 0x101937410, 0); }
/* 0x1012c1af0 ConfigSource::QueryInterface const: class-info table 0x101937410, this -> object +0 */
void F_1012c1af0(rc_ctx *c) { qi(c, 0x101937410, 0); }
/* 0x1012c1b60 CommandLineSource::QueryInterface: class-info table 0x101937818, this -> object +0 */
void F_1012c1b60(rc_ctx *c) { qi(c, 0x101937818, 0); }
/* 0x1012c1bc0 CommandLineSource::QueryInterface const: class-info table 0x101937818, this -> object +0 */
void F_1012c1bc0(rc_ctx *c) { qi(c, 0x101937818, 0); }
/* 0x1012c1c30 IniSource::QueryInterface: class-info table 0x101937c20, this -> object +0 */
void F_1012c1c30(rc_ctx *c) { qi(c, 0x101937c20, 0); }
/* 0x1012c1c90 IniSource::QueryInterface const: class-info table 0x101937c20, this -> object +0 */
void F_1012c1c90(rc_ctx *c) { qi(c, 0x101937c20, 0); }
/* 0x1012c1d00 EnvVarSource::QueryInterface: class-info table 0x101938030, this -> object +0 */
void F_1012c1d00(rc_ctx *c) { qi(c, 0x101938030, 0); }
/* 0x1012c1d60 EnvVarSource::QueryInterface const: class-info table 0x101938030, this -> object +0 */
void F_1012c1d60(rc_ctx *c) { qi(c, 0x101938030, 0); }
/* 0x1012ddbe0 DebugOutDestination::QueryInterface: class-info table 0x101938438, this -> object +0 */
void F_1012ddbe0(rc_ctx *c) { qi(c, 0x101938438, 0); }
/* 0x1012ddc40 DebugOutDestination::QueryInterface const: class-info table 0x101938438, this -> object +0 */
void F_1012ddc40(rc_ctx *c) { qi(c, 0x101938438, 0); }
/* 0x1012e1550 FileDestination::QueryInterface: class-info table 0x101938c48, this -> object +0 */
void F_1012e1550(rc_ctx *c) { qi(c, 0x101938c48, 0); }
/* 0x1012e15b0 FileDestination::QueryInterface const: class-info table 0x101938c48, this -> object +0 */
void F_1012e15b0(rc_ctx *c) { qi(c, 0x101938c48, 0); }
/* 0x1012e3200 GenericAssetFactory::QueryInterface: class-info table 0x101939058, this -> object +0 */
void F_1012e3200(rc_ctx *c) { qi(c, 0x101939058, 0); }
/* 0x1012e3260 GenericAssetFactory::QueryInterface const: class-info table 0x101939058, this -> object +0 */
void F_1012e3260(rc_ctx *c) { qi(c, 0x101939058, 0); }
/* 0x1012e3a90 GenericAssetLoadResponse::QueryInterface: class-info table 0x101939868, this -> object +0 */
void F_1012e3a90(rc_ctx *c) { qi(c, 0x101939868, 0); }
/* 0x1012e3af0 GenericAssetLoadResponse::QueryInterface const: class-info table 0x101939868, this -> object +0 */
void F_1012e3af0(rc_ctx *c) { qi(c, 0x101939868, 0); }
/* 0x1000e5930 sub:IMessage::QueryInterface: class-info table 0x101939c98, this -> object +0 */
void F_1000e5930(rc_ctx *c) { qi(c, 0x101939c98, 0); }
/* 0x1000e5980 sub:IMessage::QueryInterface const: class-info table 0x101939c98, this -> object +0 */
void F_1000e5980(rc_ctx *c) { qi(c, 0x101939c98, 0); }
/* 0x1012ebd10 Logger::QueryInterface: class-info table 0x10193a4a8, this -> object +0 */
void F_1012ebd10(rc_ctx *c) { qi(c, 0x10193a4a8, 0); }
/* 0x1012ebd70 Logger::QueryInterface const: class-info table 0x10193a4a8, this -> object +0 */
void F_1012ebd70(rc_ctx *c) { qi(c, 0x10193a4a8, 0); }
/* 0x1012f4cb0 MessageService::QueryInterface: class-info table 0x10193b100, this -> object +0 */
void F_1012f4cb0(rc_ctx *c) { qi(c, 0x10193b100, 0); }
/* 0x1012f4d10 MessageService::QueryInterface const: class-info table 0x10193b100, this -> object +0 */
void F_1012f4d10(rc_ctx *c) { qi(c, 0x10193b100, 0); }
/* 0x1012f6820 NetMessage::QueryInterface: class-info table 0x10193b918, this -> object +0 */
void F_1012f6820(rc_ctx *c) { qi(c, 0x10193b918, 0); }
/* 0x1012f6880 NetMessage::QueryInterface const: class-info table 0x10193b918, this -> object +0 */
void F_1012f6880(rc_ctx *c) { qi(c, 0x10193b918, 0); }
/* 0x1012f68f0 AssignNetIDMessage::QueryInterface: class-info table 0x10193bd20, this -> object +0 */
void F_1012f68f0(rc_ctx *c) { qi(c, 0x10193bd20, 0); }
/* 0x1012f6950 AssignNetIDMessage::QueryInterface const: class-info table 0x10193bd20, this -> object +0 */
void F_1012f6950(rc_ctx *c) { qi(c, 0x10193bd20, 0); }
/* 0x1012f92c0 PrintDestination::QueryInterface: class-info table 0x10193c9c0, this -> object +0 */
void F_1012f92c0(rc_ctx *c) { qi(c, 0x10193c9c0, 0); }
/* 0x1012f9320 PrintDestination::QueryInterface const: class-info table 0x10193c9c0, this -> object +0 */
void F_1012f9320(rc_ctx *c) { qi(c, 0x10193c9c0, 0); }
/* 0x1012ffd30 ServiceManager::QueryInterface: class-info table 0x10193d218, this -> object +0 */
void F_1012ffd30(rc_ctx *c) { qi(c, 0x10193d218, 0); }
/* 0x1012ffd90 ServiceManager::QueryInterface const: class-info table 0x10193d218, this -> object +0 */
void F_1012ffd90(rc_ctx *c) { qi(c, 0x10193d218, 0); }
/* 0x101292a70 sub:StreamMessage::QueryInterface: class-info table 0x10193de70, this -> object +0 */
void F_101292a70(rc_ctx *c) { qi(c, 0x10193de70, 0); }
/* 0x101292ac0 sub:StreamMessage::QueryInterface const: class-info table 0x10193de70, this -> object +0 */
void F_101292ac0(rc_ctx *c) { qi(c, 0x10193de70, 0); }
/* 0x101313680 MacOSPlatformService::QueryInterface: class-info table 0x10193e718, this -> object +0 */
void F_101313680(rc_ctx *c) { qi(c, 0x10193e718, 0); }
/* 0x1013136e0 MacOSPlatformService::QueryInterface const: class-info table 0x10193e718, this -> object +0 */
void F_1013136e0(rc_ctx *c) { qi(c, 0x10193e718, 0); }
/* 0x10131d790 AudioBankAssetFactory::QueryInterface: class-info table 0x10193ed08, this -> object +0 */
void F_10131d790(rc_ctx *c) { qi(c, 0x10193ed08, 0); }
/* 0x10131d7f0 AudioBankAssetFactory::QueryInterface const: class-info table 0x10193ed08, this -> object +0 */
void F_10131d7f0(rc_ctx *c) { qi(c, 0x10193ed08, 0); }
/* 0x10131e5b0 AudioBankAssetLoadResponse::QueryInterface: class-info table 0x10193f518, this -> object +0 */
void F_10131e5b0(rc_ctx *c) { qi(c, 0x10193f518, 0); }
/* 0x10131e610 AudioBankAssetLoadResponse::QueryInterface const: class-info table 0x10193f518, this -> object +0 */
void F_10131e610(rc_ctx *c) { qi(c, 0x10193f518, 0); }
/* 0x1013223c0 AudioMessage::QueryInterface: class-info table 0x10193f920, this -> object +0 */
void F_1013223c0(rc_ctx *c) { qi(c, 0x10193f920, 0); }
/* 0x101322420 AudioMessage::QueryInterface const: class-info table 0x10193f920, this -> object +0 */
void F_101322420(rc_ctx *c) { qi(c, 0x10193f920, 0); }
/* 0x101337230 AudioService::QueryInterface: class-info table 0x101942578, this -> object +0 */
void F_101337230(rc_ctx *c) { qi(c, 0x101942578, 0); }
/* 0x101337290 AudioService::QueryInterface const: class-info table 0x101942578, this -> object +0 */
void F_101337290(rc_ctx *c) { qi(c, 0x101942578, 0); }
/* 0x1000cfe60 SceneGraphRemovedMessage::QueryInterface: class-info table 0x101924528, this -> object +0 */
void F_1000cfe60(rc_ctx *c) { qi(c, 0x101924528, 0); }
/* 0x1000cfeb0 SceneGraphRemovedMessage::QueryInterface const: class-info table 0x101924528, this -> object +0 */
void F_1000cfeb0(rc_ctx *c) { qi(c, 0x101924528, 0); }
/* 0x1000d0170 SceneGraphAddedMessage::QueryInterface: class-info table 0x101924120, this -> object +0 */
void F_1000d0170(rc_ctx *c) { qi(c, 0x101924120, 0); }
/* 0x1000d01c0 SceneGraphAddedMessage::QueryInterface const: class-info table 0x101924120, this -> object +0 */
void F_1000d01c0(rc_ctx *c) { qi(c, 0x101924120, 0); }
/* 0x1000d0bb0 SceneGraphsUpdatedMessage::QueryInterface: class-info table 0x101924930, this -> object +0 */
void F_1000d0bb0(rc_ctx *c) { qi(c, 0x101924930, 0); }
/* 0x1000d0c00 SceneGraphsUpdatedMessage::QueryInterface const: class-info table 0x101924930, this -> object +0 */
void F_1000d0c00(rc_ctx *c) { qi(c, 0x101924930, 0); }
/* 0x1000e85f0 SceneGraphMessage::QueryInterface: class-info table 0x101923d18, this -> object +0 */
void F_1000e85f0(rc_ctx *c) { qi(c, 0x101923d18, 0); }
/* 0x1000e8640 SceneGraphMessage::QueryInterface const: class-info table 0x101923d18, this -> object +0 */
void F_1000e8640(rc_ctx *c) { qi(c, 0x101923d18, 0); }
/* 0x1000f49a0 JSONFactory::JSONFactoryData::QueryInterface: class-info table 0x1018e3e30, this -> object +0 */
void F_1000f49a0(rc_ctx *c) { qi(c, 0x1018e3e30, 0); }
/* 0x1000f4a00 JSONFactory::JSONFactoryData::QueryInterface const: class-info table 0x1018e3e30, this -> object +0 */
void F_1000f4a00(rc_ctx *c) { qi(c, 0x1018e3e30, 0); }
/* 0x1000a55b0 ReturnToTitleScreen::QueryInterface: class-info table 0x1018f9908, this -> object +0 */
void F_1000a55b0(rc_ctx *c) { qi(c, 0x1018f9908, 0); }
/* 0x1000a5600 ReturnToTitleScreen::QueryInterface const: class-info table 0x1018f9908, this -> object +0 */
void F_1000a5600(rc_ctx *c) { qi(c, 0x1018f9908, 0); }
/* 0x1002abc80 PreloadHelperComplete::QueryInterface: class-info table 0x1018ed0e8, this -> object +0 */
void F_1002abc80(rc_ctx *c) { qi(c, 0x1018ed0e8, 0); }
/* 0x1002abcd0 PreloadHelperComplete::QueryInterface const: class-info table 0x1018ed0e8, this -> object +0 */
void F_1002abcd0(rc_ctx *c) { qi(c, 0x1018ed0e8, 0); }
/* 0x1002ac290 SceneGraphCacheResponse::QueryInterface: class-info table 0x101926d90, this -> object +0 */
void F_1002ac290(rc_ctx *c) { qi(c, 0x101926d90, 0); }
/* 0x1002ac2e0 SceneGraphCacheResponse::QueryInterface const: class-info table 0x101926d90, this -> object +0 */
void F_1002ac2e0(rc_ctx *c) { qi(c, 0x101926d90, 0); }
/* 0x1002ad850 PreloadGroup::QueryInterface: class-info table 0x1018ed8f8, this -> object +0 */
void F_1002ad850(rc_ctx *c) { qi(c, 0x1018ed8f8, 0); }
/* 0x1002ad8b0 PreloadGroup::QueryInterface const: class-info table 0x1018ed8f8, this -> object +0 */
void F_1002ad8b0(rc_ctx *c) { qi(c, 0x1018ed8f8, 0); }
/* 0x1002d67f0 GCPauseMessage::QueryInterface: class-info table 0x1018f21a8, this -> object +0 */
void F_1002d67f0(rc_ctx *c) { qi(c, 0x1018f21a8, 0); }
/* 0x1002d6840 GCPauseMessage::QueryInterface const: class-info table 0x1018f21a8, this -> object +0 */
void F_1002d6840(rc_ctx *c) { qi(c, 0x1018f21a8, 0); }
/* 0x100372190 RSGraphAssetFactoryLoadResponse::QueryInterface: class-info table 0x1018ef7d0, this -> object +0 */
void F_100372190(rc_ctx *c) { qi(c, 0x1018ef7d0, 0); }
/* 0x1003721f0 RSGraphAssetFactoryLoadResponse::QueryInterface const: class-info table 0x1018ef7d0, this -> object +0 */
void F_1003721f0(rc_ctx *c) { qi(c, 0x1018ef7d0, 0); }
/* 0x1003722e0 RSConnectGameInterface_Impl::QueryInterface: class-info table 0x1018efbd8, this -> object +0 */
void F_1003722e0(rc_ctx *c) { qi(c, 0x1018efbd8, 0); }
/* 0x100372340 RSConnectGameInterface_Impl::QueryInterface const: class-info table 0x1018efbd8, this -> object +0 */
void F_100372340(rc_ctx *c) { qi(c, 0x1018efbd8, 0); }
/* 0x1003ac700 ReactiveRingModel::QueryInterface: class-info table 0x1019015d8, this -> object +0 */
void F_1003ac700(rc_ctx *c) { qi(c, 0x1019015d8, 0); }
/* 0x1003ac750 ReactiveRingModel::QueryInterface const: class-info table 0x1019015d8, this -> object +0 */
void F_1003ac750(rc_ctx *c) { qi(c, 0x1019015d8, 0); }
/* 0x100782720 RingParticleEmitter::QueryInterface: class-info table 0x1018fd4a0, this -> object +0 */
void F_100782720(rc_ctx *c) { qi(c, 0x1018fd4a0, 0); }
/* 0x100782780 RingParticleEmitter::QueryInterface const: class-info table 0x1018fd4a0, this -> object +0 */
void F_100782780(rc_ctx *c) { qi(c, 0x1018fd4a0, 0); }
/* 0x100326b90 GCGFxMouseEventMessage::QueryInterface: class-info table 0x1019007d0, this -> object +0 */
void F_100326b90(rc_ctx *c) { qi(c, 0x1019007d0, 0); }
/* 0x100326be0 GCGFxMouseEventMessage::QueryInterface const: class-info table 0x1019007d0, this -> object +0 */
void F_100326be0(rc_ctx *c) { qi(c, 0x1019007d0, 0); }
/* 0x1008a89a0 GCKinectEventMessage::QueryInterface: class-info table 0x101901e00, this -> object +0 */
void F_1008a89a0(rc_ctx *c) { qi(c, 0x101901e00, 0); }
/* 0x1008a8a00 GCKinectEventMessage::QueryInterface const: class-info table 0x101901e00, this -> object +0 */
void F_1008a8a00(rc_ctx *c) { qi(c, 0x101901e00, 0); }
/* 0x100a53fb0 NiFrkParticleEffectModel::QueryInterface: class-info table 0x1019070c8, this -> object +0 */
void F_100a53fb0(rc_ctx *c) { qi(c, 0x1019070c8, 0); }
/* 0x100a54010 NiFrkParticleEffectModel::QueryInterface const: class-info table 0x1019070c8, this -> object +0 */
void F_100a54010(rc_ctx *c) { qi(c, 0x1019070c8, 0); }
/* 0x100b67ea0 GFxAssetTaskRequest::QueryInterface: class-info table 0x101908a20, this -> object +0 */
void F_100b67ea0(rc_ctx *c) { qi(c, 0x101908a20, 0); }
/* 0x100b67f00 GFxAssetTaskRequest::QueryInterface const: class-info table 0x101908a20, this -> object +0 */
void F_100b67f00(rc_ctx *c) { qi(c, 0x101908a20, 0); }
/* 0x1002b26e0 GFXModelResponse::QueryInterface: class-info table 0x10190a320, this -> object +0 */
void F_1002b26e0(rc_ctx *c) { qi(c, 0x10190a320, 0); }
/* 0x1002b2730 GFXModelResponse::QueryInterface const: class-info table 0x10190a320, this -> object +0 */
void F_1002b2730(rc_ctx *c) { qi(c, 0x10190a320, 0); }
/* 0x101150060 LuaScriptFactoryResponseData::QueryInterface: class-info table 0x101922cd0, this -> object +0 */
void F_101150060(rc_ctx *c) { qi(c, 0x101922cd0, 0); }
/* 0x1011500c0 LuaScriptFactoryResponseData::QueryInterface const: class-info table 0x101922cd0, this -> object +0 */
void F_1011500c0(rc_ctx *c) { qi(c, 0x101922cd0, 0); }
/* 0x101180380 NIFFactoryResponse::QueryInterface: class-info table 0x101926160, this -> object +0 */
void F_101180380(rc_ctx *c) { qi(c, 0x101926160, 0); }
/* 0x1011803d0 NIFFactoryResponse::QueryInterface const: class-info table 0x101926160, this -> object +0 */
void F_1011803d0(rc_ctx *c) { qi(c, 0x101926160, 0); }
/* 0x101199d00 AttachmentMadeMessage::QueryInterface: class-info table 0x101925140, this -> object +0 */
void F_101199d00(rc_ctx *c) { qi(c, 0x101925140, 0); }
/* 0x101199d50 AttachmentMadeMessage::QueryInterface const: class-info table 0x101925140, this -> object +0 */
void F_101199d50(rc_ctx *c) { qi(c, 0x101925140, 0); }
/* 0x10119a270 AttachmentMessage::QueryInterface: class-info table 0x101924d38, this -> object +0 */
void F_10119a270(rc_ctx *c) { qi(c, 0x101924d38, 0); }
/* 0x10119a2c0 AttachmentMessage::QueryInterface const: class-info table 0x101924d38, this -> object +0 */
void F_10119a2c0(rc_ctx *c) { qi(c, 0x101924d38, 0); }
/* 0x10119a3d0 AttachmentBrokenMessage::QueryInterface: class-info table 0x101925548, this -> object +0 */
void F_10119a3d0(rc_ctx *c) { qi(c, 0x101925548, 0); }
/* 0x10119a420 AttachmentBrokenMessage::QueryInterface const: class-info table 0x101925548, this -> object +0 */
void F_10119a420(rc_ctx *c) { qi(c, 0x101925548, 0); }
/* 0x1011a65d0 InputActionMapsLoaded::QueryInterface: class-info table 0x1019279a8, this -> object +0 */
void F_1011a65d0(rc_ctx *c) { qi(c, 0x1019279a8, 0); }
/* 0x1011a6630 InputActionMapsLoaded::QueryInterface const: class-info table 0x1019279a8, this -> object +0 */
void F_1011a6630(rc_ctx *c) { qi(c, 0x1019279a8, 0); }
/* 0x1011d2360 ScheduledTask::QueryInterface: class-info table 0x1019300f0, this -> object +0 */
void F_1011d2360(rc_ctx *c) { qi(c, 0x1019300f0, 0); }
/* 0x1011d23b0 ScheduledTask::QueryInterface const: class-info table 0x1019300f0, this -> object +0 */
void F_1011d23b0(rc_ctx *c) { qi(c, 0x1019300f0, 0); }
/* 0x1011d6e30 EntityFactory::EntityFactoryData::QueryInterface: class-info table 0x10192b7e8, this -> object +0 */
void F_1011d6e30(rc_ctx *c) { qi(c, 0x10192b7e8, 0); }
/* 0x1011d6e90 EntityFactory::EntityFactoryData::QueryInterface const: class-info table 0x10192b7e8, this -> object +0 */
void F_1011d6e90(rc_ctx *c) { qi(c, 0x10192b7e8, 0); }
/* 0x1011d70d0 EntityFactoryRequest::QueryInterface: class-info table 0x10192bbf0, this -> object +0 */
void F_1011d70d0(rc_ctx *c) { qi(c, 0x10192bbf0, 0); }
/* 0x1011d7120 EntityFactoryRequest::QueryInterface const: class-info table 0x10192bbf0, this -> object +0 */
void F_1011d7120(rc_ctx *c) { qi(c, 0x10192bbf0, 0); }
/* 0x1011d7a40 EntityFactoryResponse::QueryInterface: class-info table 0x10192bff8, this -> object +0 */
void F_1011d7a40(rc_ctx *c) { qi(c, 0x10192bff8, 0); }
/* 0x1011d7aa0 EntityFactoryResponse::QueryInterface const: class-info table 0x10192bff8, this -> object +0 */
void F_1011d7aa0(rc_ctx *c) { qi(c, 0x10192bff8, 0); }
/* 0x1011fab00 ResponseData::QueryInterface: class-info table 0x10192e868, this -> object +0 */
void F_1011fab00(rc_ctx *c) { qi(c, 0x10192e868, 0); }
/* 0x1011fab60 ResponseData::QueryInterface const: class-info table 0x10192e868, this -> object +0 */
void F_1011fab60(rc_ctx *c) { qi(c, 0x10192e868, 0); }
/* 0x1011fccc0 FlatModelFactoryRequest::QueryInterface: class-info table 0x10192ec70, this -> object +0 */
void F_1011fccc0(rc_ctx *c) { qi(c, 0x10192ec70, 0); }
/* 0x1011fcd20 FlatModelFactoryRequest::QueryInterface const: class-info table 0x10192ec70, this -> object +0 */
void F_1011fcd20(rc_ctx *c) { qi(c, 0x10192ec70, 0); }
/* 0x1011fd760 FlatModelFactoryResponse::QueryInterface: class-info table 0x10192f078, this -> object +0 */
void F_1011fd760(rc_ctx *c) { qi(c, 0x10192f078, 0); }
/* 0x1011fd7c0 FlatModelFactoryResponse::QueryInterface const: class-info table 0x10192f078, this -> object +0 */
void F_1011fd7c0(rc_ctx *c) { qi(c, 0x10192f078, 0); }
/* 0x10124bb00 ScriptFactoryRequest::QueryInterface: class-info table 0x101930918, this -> object +0 */
void F_10124bb00(rc_ctx *c) { qi(c, 0x101930918, 0); }
/* 0x10124bb60 ScriptFactoryRequest::QueryInterface const: class-info table 0x101930918, this -> object +0 */
void F_10124bb60(rc_ctx *c) { qi(c, 0x101930918, 0); }
/* 0x10125ddf0 WorldFactoryData::QueryInterface: class-info table 0x101931530, this -> object +0 */
void F_10125ddf0(rc_ctx *c) { qi(c, 0x101931530, 0); }
/* 0x10125de50 WorldFactoryData::QueryInterface const: class-info table 0x101931530, this -> object +0 */
void F_10125de50(rc_ctx *c) { qi(c, 0x101931530, 0); }
/* 0x10125f0b0 WorldFactoryRequest::QueryInterface: class-info table 0x101931938, this -> object +0 */
void F_10125f0b0(rc_ctx *c) { qi(c, 0x101931938, 0); }
/* 0x10125f110 WorldFactoryRequest::QueryInterface const: class-info table 0x101931938, this -> object +0 */
void F_10125f110(rc_ctx *c) { qi(c, 0x101931938, 0); }
/* 0x10125f620 WorldFactoryResponse::QueryInterface: class-info table 0x101931d40, this -> object +0 */
void F_10125f620(rc_ctx *c) { qi(c, 0x101931d40, 0); }
/* 0x10125f680 WorldFactoryResponse::QueryInterface const: class-info table 0x101931d40, this -> object +0 */
void F_10125f680(rc_ctx *c) { qi(c, 0x101931d40, 0); }
/* 0x10126e2c0 ActorAddedMessage::QueryInterface: class-info table 0x101932148, this -> object +0 */
void F_10126e2c0(rc_ctx *c) { qi(c, 0x101932148, 0); }
/* 0x10126e310 ActorAddedMessage::QueryInterface const: class-info table 0x101932148, this -> object +0 */
void F_10126e310(rc_ctx *c) { qi(c, 0x101932148, 0); }
/* 0x10126e3c0 ActorRemovedMessage::QueryInterface: class-info table 0x101932550, this -> object +0 */
void F_10126e3c0(rc_ctx *c) { qi(c, 0x101932550, 0); }
/* 0x10126e410 ActorRemovedMessage::QueryInterface const: class-info table 0x101932550, this -> object +0 */
void F_10126e410(rc_ctx *c) { qi(c, 0x101932550, 0); }
/* 0x101279510 KFMFactoryRequest::QueryInterface: class-info table 0x101933590, this -> object +0 */
void F_101279510(rc_ctx *c) { qi(c, 0x101933590, 0); }
/* 0x101279570 KFMFactoryRequest::QueryInterface const: class-info table 0x101933590, this -> object +0 */
void F_101279570(rc_ctx *c) { qi(c, 0x101933590, 0); }
/* 0x101279ca0 KFMFactoryResponse::QueryInterface: class-info table 0x101933998, this -> object +0 */
void F_101279ca0(rc_ctx *c) { qi(c, 0x101933998, 0); }
/* 0x101279d00 KFMFactoryResponse::QueryInterface const: class-info table 0x101933998, this -> object +0 */
void F_101279d00(rc_ctx *c) { qi(c, 0x101933998, 0); }
/* 0x101292280 AssetLocationMap::QueryInterface: class-info table 0x1019366e8, this -> object +0 */
void F_101292280(rc_ctx *c) { qi(c, 0x1019366e8, 0); }
/* 0x1012922e0 AssetLocationMap::QueryInterface const: class-info table 0x1019366e8, this -> object +0 */
void F_1012922e0(rc_ctx *c) { qi(c, 0x1019366e8, 0); }
/* 0x1012e3360 ResponseData::QueryInterface: class-info table 0x101939460, this -> object +0 */
void F_1012e3360(rc_ctx *c) { qi(c, 0x101939460, 0); }
/* 0x1012e33c0 ResponseData::QueryInterface const: class-info table 0x101939460, this -> object +0 */
void F_1012e33c0(rc_ctx *c) { qi(c, 0x101939460, 0); }
/* 0x10131dc30 AudioBankAssetLoadRequest::QueryInterface: class-info table 0x10193f110, this -> object +0 */
void F_10131dc30(rc_ctx *c) { qi(c, 0x10193f110, 0); }
/* 0x10131dc90 AudioBankAssetLoadRequest::QueryInterface const: class-info table 0x10193f110, this -> object +0 */
void F_10131dc90(rc_ctx *c) { qi(c, 0x10193f110, 0); }
/* 0x1013224f0 PostEventMessage::QueryInterface: class-info table 0x10193fd28, this -> object +0 */
void F_1013224f0(rc_ctx *c) { qi(c, 0x10193fd28, 0); }
/* 0x101322550 PostEventMessage::QueryInterface const: class-info table 0x10193fd28, this -> object +0 */
void F_101322550(rc_ctx *c) { qi(c, 0x10193fd28, 0); }
/* 0x1013226a0 PostTriggerMessage::QueryInterface: class-info table 0x101940130, this -> object +0 */
void F_1013226a0(rc_ctx *c) { qi(c, 0x101940130, 0); }
/* 0x101322700 PostTriggerMessage::QueryInterface const: class-info table 0x101940130, this -> object +0 */
void F_101322700(rc_ctx *c) { qi(c, 0x101940130, 0); }
/* 0x101322850 SetParamMessage::QueryInterface: class-info table 0x101940538, this -> object +0 */
void F_101322850(rc_ctx *c) { qi(c, 0x101940538, 0); }
/* 0x1013228b0 SetParamMessage::QueryInterface const: class-info table 0x101940538, this -> object +0 */
void F_1013228b0(rc_ctx *c) { qi(c, 0x101940538, 0); }
/* 0x101322a00 SetStateMessage::QueryInterface: class-info table 0x101940940, this -> object +0 */
void F_101322a00(rc_ctx *c) { qi(c, 0x101940940, 0); }
/* 0x101322a60 SetStateMessage::QueryInterface const: class-info table 0x101940940, this -> object +0 */
void F_101322a60(rc_ctx *c) { qi(c, 0x101940940, 0); }
/* 0x101322c00 SetSwitchMessage::QueryInterface: class-info table 0x101940d48, this -> object +0 */
void F_101322c00(rc_ctx *c) { qi(c, 0x101940d48, 0); }
/* 0x101322c60 SetSwitchMessage::QueryInterface const: class-info table 0x101940d48, this -> object +0 */
void F_101322c60(rc_ctx *c) { qi(c, 0x101940d48, 0); }
/* 0x101322e00 DoPrepareEventCallbackBehaviorMessage::QueryInterface: class-info table 0x101941150, this -> object +0 */
void F_101322e00(rc_ctx *c) { qi(c, 0x101941150, 0); }
/* 0x101322e60 DoPrepareEventCallbackBehaviorMessage::QueryInterface const: class-info table 0x101941150, this -> object +0 */
void F_101322e60(rc_ctx *c) { qi(c, 0x101941150, 0); }
/* 0x101322fb0 DoUnprepareEventCallbackBehaviorMessage::QueryInterface: class-info table 0x101941558, this -> object +0 */
void F_101322fb0(rc_ctx *c) { qi(c, 0x101941558, 0); }
/* 0x101323010 DoUnprepareEventCallbackBehaviorMessage::QueryInterface const: class-info table 0x101941558, this -> object +0 */
void F_101323010(rc_ctx *c) { qi(c, 0x101941558, 0); }
/* 0x101323160 DoLoadBankCallbackBehaviorMessage::QueryInterface: class-info table 0x101941960, this -> object +0 */
void F_101323160(rc_ctx *c) { qi(c, 0x101941960, 0); }
/* 0x1013231c0 DoLoadBankCallbackBehaviorMessage::QueryInterface const: class-info table 0x101941960, this -> object +0 */
void F_1013231c0(rc_ctx *c) { qi(c, 0x101941960, 0); }
/* 0x101323330 DoUnloadBankCallbackBehaviorMessage::QueryInterface: class-info table 0x101941d68, this -> object +0 */
void F_101323330(rc_ctx *c) { qi(c, 0x101941d68, 0); }
/* 0x101323390 DoUnloadBankCallbackBehaviorMessage::QueryInterface const: class-info table 0x101941d68, this -> object +0 */
void F_101323390(rc_ctx *c) { qi(c, 0x101941d68, 0); }
/* 0x101323500 DoPostEventExNotifyCallbackBehaviorMessage::QueryInterface: class-info table 0x101942170, this -> object +0 */
void F_101323500(rc_ctx *c) { qi(c, 0x101942170, 0); }
/* 0x101323560 DoPostEventExNotifyCallbackBehaviorMessage::QueryInterface const: class-info table 0x101942170, this -> object +0 */
void F_101323560(rc_ctx *c) { qi(c, 0x101942170, 0); }
