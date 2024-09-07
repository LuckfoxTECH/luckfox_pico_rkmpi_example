/*****************************************************************************
* | Author      :   Luckfox team
* | Function    :   
* | Info        :
*
*----------------
* | This version:   V1.1
* | Date        :   2024-08-26
* | Info        :   Basic version
*
******************************************************************************/

#include "luckfox_mpi.h"

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

static void set_argb8888_line(RK_U32 *buf, int sX, int sY, int eX, int eY, int type, RK_U32 color) {
	switch(type)
	{
		case 0:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = sY; j < (sY+2); j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < (sX+2); i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 1:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = sY; j < (sY+2); j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = (eX-2); i < eX; i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 2:
			for (RK_U32 i = (eX-2); i < eX; i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = (eY-2); j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 3:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = (eY-2); j < eY; j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < (sX+2); i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
	}

	return;
}



RK_S32 test_rgn_overlay_line_process(int sX ,int sY,int type, int group) {
	printf("========%s========\n", __func__);
	RK_S32 s32Ret = RK_SUCCESS;
	RGN_HANDLE RgnHandle = group * 4 + type;
	BITMAP_S stBitmap;
	RGN_ATTR_S stRgnAttr;
	RGN_CHN_ATTR_S stRgnChnAttr;

	int u32Width = 20;
	int u32Height = 20;
	int s32X = sX;
	int s32Y = sY;

	MPP_CHN_S stMppChn;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	/****************************************
	step 1: create overlay regions
	****************************************/
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = (PIXEL_FORMAT_E)RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = u32Width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = u32Height;
	stRgnAttr.unAttr.stOverlay.u32ClutNum = 0;

	s32Ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_RGN_Create (%d) failed with %#x!", RgnHandle, s32Ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	RK_LOGI("The handle: %d, create success!", RgnHandle);

	/*********************************************
	step 2: display overlay regions to groups
	*********************************************/
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = RK_TRUE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = s32X;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = s32Y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bEnable = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bForceIntra = RK_TRUE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[0] = 0x00;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[1] = 0xFFFFFFF;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.bInvColEn = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Width = 16;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Height = 16;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.enChgMod = LESSTHAN_LUM_THRESH;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.u32LumThresh = 100;
	s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_RGN_AttachToChn (%d) failed with %#x!", RgnHandle, s32Ret);
		return RK_FAILURE;
	}
	RK_LOGI("Display region to chn success!");

	/*********************************************
	step 3: show bitmap
	*********************************************/
	stBitmap.enPixelFormat = (PIXEL_FORMAT_E)RK_FMT_ARGB8888;
	stBitmap.u32Width = u32Width;
	stBitmap.u32Height = u32Height;

	RK_U16 ColorBlockSize = stBitmap.u32Height * stBitmap.u32Width;
	stBitmap.pData = malloc(ColorBlockSize * TEST_ARGB32_PIX_SIZE);
	memset(stBitmap.pData, 0, ColorBlockSize * TEST_ARGB32_PIX_SIZE);	
	RK_U8 *ColorData = (RK_U8 *)stBitmap.pData;

	set_argb8888_line((RK_U32 *)ColorData, 0, 0, 20, 20, type, TEST_ARGB32_GREEN); 		
		
	s32Ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_RGN_SetBitMap failed with %#x!", s32Ret);
		return RK_FAILURE;
	}

	return 0;
}


RK_S32 rgn_overlay_release(int group) {	
	MPP_CHN_S stMppChn;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;

	RGN_HANDLE RgnHandle = 0;
	for(RGN_HANDLE i = 0;i < 4;i++)
	{
		RK_MPI_RGN_DetachFromChn(i, &stMppChn);
		RK_MPI_RGN_Destroy(i);
	}
	return 0;	
}

int vi_dev_init() {
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int vi_chn_init(int channelId, int width, int height) {
	int ret;
	int buf_cnt = 4;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	return ret;
}

int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
	printf("========%s========\n", __func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	if (enType == RK_VIDEO_ID_AVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_HEVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		stAttr.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_MJPEG) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		stAttr.stRcAttr.stMjpegCbr.u32BitRate = 10 * 1024;
	}

	stAttr.stVencAttr.enType = enType;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	if (enType == RK_VIDEO_ID_AVC)
		stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	stAttr.stVencAttr.enMirror = MIRROR_NONE;

	RK_MPI_VENC_CreateChn(chnId, &stAttr);

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

	return 0;
}
