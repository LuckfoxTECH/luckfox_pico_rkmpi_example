#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "retinaface.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>


static RK_S32 g_s32FrameCnt = -1;

//test param
MPP_CHN_S stSrcChn, stSrcChn1, stvpssChn, stvencChn;
VENC_RECV_PIC_PARAM_S stRecvParam;

rknn_app_context_t rknn_app_ctx;	
object_detect_result_list od_results;

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;


bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}


static void *GetVpssBuffer(void *arg)
{
	(void)arg;
	printf("========%s========\n", __func__);

	int width = 640;
	int height = 640;
	char text[16];
	int sX,sY,eX,eY;
	float scale_x = 1.125;
	float scale_y = 0.75;

	int s32Ret;
	int group_count = 0;
	void *vpssData = RK_NULL;
	VIDEO_FRAME_INFO_S vpssFrame;

	while(!quit)
	{
		s32Ret = RK_MPI_VPSS_GetChnFrame(0, 0, &vpssFrame, -1);
		if(s32Ret == RK_SUCCESS)
		{
			vpssData = RK_MPI_MB_Handle2VirAddr(vpssFrame.stVFrame.pMbBlk);
			if(vpssData != RK_NULL)
			{
				memcpy(rknn_app_ctx.input_mems[0]->virt_addr, vpssData, 640*640*3);
				inference_retinaface_model(&rknn_app_ctx, &od_results);

				for(int i = 0; i < od_results.count; i++)
				{					
					// 获取框的四个坐标 
					object_detect_result *det_result = &(od_results.results[i]);
					printf("%d %d %d %d\n",det_result->box.left ,det_result->box.top,det_result->box.right,det_result->box.bottom);

					// 只标记一个	
					if(i == 0)
					{
						sX = (int)((float)det_result->box.left 	 *scale_x);	
						sY = (int)((float)det_result->box.top 	 *scale_y);	
						eX = (int)((float)det_result->box.right  *scale_x);	
						eY = (int)((float)det_result->box.bottom *scale_y);

						// OSD 坐标要求为偶数
						sX = sX - (sX % 2);
						sY = sY - (sY % 2);
						eX = eX	- (eX % 2);				
						eY = eY	- (eY % 2);					
					
						if((eX > sX) && (eY > sY) && (sX > 0) && (sY > 0))
						{
							test_rgn_overlay_line_process(sX,sY,0,i);
							test_rgn_overlay_line_process(eX,sY,1,i);
							test_rgn_overlay_line_process(eX,eY,2,i);
							test_rgn_overlay_line_process(sX,eY,3,i);
						}
						group_count++;
					}

				}			
				RK_MPI_VPSS_ReleaseChnFrame(0, 0, &vpssFrame);
			}
		}
		usleep(500000);
		for(int i = 0;i < group_count; i++)
		{
			rgn_overlay_release(i);
		}
		group_count = 0;
	}			
	return NULL;
}

static void *GetMediaBuffer(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);
	void *pData = RK_NULL;
	void *vpssData = RK_NULL;

	int loopCount = 0;
	int s32Ret;
	static RK_U32 jpeg_id = 0;
	char jpeg_path[128];

	VENC_STREAM_S stFrame;
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	VIDEO_FRAME_INFO_S vpssFrame;

	while (!quit) {
		s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 200000);
		if (s32Ret == RK_SUCCESS) {
			if (g_rtsplive && g_rtsp_session) {
				pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				rtsp_tx_video(g_rtsp_session,(uint8_t *)pData, stFrame.pstPack->u32Len,
							  stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}

			RK_U64 nowUs = TEST_COMM_GetNowUs();

			RK_LOGD("chn:0, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
					loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
					stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

			s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
			}
			loopCount++;
		}

		if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
			quit = true;
			break;
		}
		usleep(10 * 1000);
	}

	printf("\n======exit %s=======\n", __func__);

	free(stFrame.pstPack);
	return NULL;
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 u32Width    = 720;
	RK_U32 u32Height   = 480;
	RK_U32 disp_width  = 640;
	RK_U32 disp_height = 640;

	RK_CHAR *pOutPath = NULL;
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	RK_CHAR *pCodecName = (RK_CHAR *)"H264";
	RK_S32 s32chnlId = 0;
	RK_S32 s32chnlId1 = 1;

	// Rknn model init
    int ret;
	const char *model_path = "./model/retinaface.rknn";
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));	
    if(init_retinaface_model(model_path, &rknn_app_ctx) != RK_SUCCESS)
	{
		RK_LOGE("rknn model init fail!");
		return -1;
	}

	// rkaiq init 
	RK_BOOL multi_sensor = RK_FALSE;	
	const char *iq_dir = "/etc/iqfiles";
	rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
	//hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;
	SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
	SAMPLE_COMM_ISP_Run(0);

	// rtsp init	
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	// Ctrl-c quit
	signal(SIGINT, sigterm_handler);

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	// vi init
	vi_dev_init();
	vi_chn_init(s32chnlId, u32Width, u32Height);
	vi_chn_init(s32chnlId1, u32Width, u32Height);

	// vpss init
	vpss_init(0, disp_width, disp_height);
	
	// venc init
	enCodecType = RK_VIDEO_ID_AVC; 
	venc_init(0, u32Width, u32Height, enCodecType);

	// bind vi to venc	
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;
		
	stvencChn.enModId = RK_ID_VENC;
	stvencChn.s32DevId = 0;
	stvencChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vpss0 to venc0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stvencChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 1 ch venc failed");
		goto __FAILED;
	}
	// bind vi to vpss
	stSrcChn1.enModId = RK_ID_VI;
	stSrcChn1.s32DevId = 0;
	stSrcChn1.s32ChnId = 1;

	stvpssChn.enModId = RK_ID_VPSS;
	stvpssChn.s32DevId = 0;
	stvpssChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vi0 to vpss0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn1, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		goto __FAILED;
	}

			
	pthread_t main_thread;
	pthread_create(&main_thread, NULL, GetMediaBuffer, NULL);
	pthread_t vpss_thread;
	pthread_create(&vpss_thread, NULL, GetVpssBuffer, NULL);


	while (!quit) {	
		usleep(50000);
	}

	pthread_join(main_thread, NULL);
	pthread_join(vpss_thread, NULL);

__FAILED:

	s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stvencChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_SYS_UnBind(&stSrcChn1, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

	s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId1);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);
	
	RK_MPI_VPSS_StopGrp(0);
	RK_MPI_VPSS_DestroyGrp(0);

	s32Ret = RK_MPI_VENC_StopRecvFrame(0);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	s32Ret = RK_MPI_VENC_DestroyChn(0);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
	}

	s32Ret = RK_MPI_VI_DisableDev(0);
	RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);

	RK_LOGE("test running exit:%d", s32Ret);
	RK_MPI_SYS_Exit();

	// Stop RKAIQ
	SAMPLE_COMM_ISP_Stop(0);

	// Release rknn model
    release_retinaface_model(&rknn_app_ctx);	

	return 0;
}
