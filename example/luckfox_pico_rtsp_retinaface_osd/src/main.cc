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

#define DISP_WIDTH  720
#define DISP_HEIGHT 480

MPP_CHN_S stSrcChn, stSrcChn1, stvpssChn, stvencChn;
VENC_RECV_PIC_PARAM_S stRecvParam;

rknn_app_context_t rknn_app_ctx;	
object_detect_result_list od_results;

rtsp_demo_handle g_rtsplive = NULL;
rtsp_session_handle g_rtsp_session;

static void *GetMediaBuffer(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);
	void *pData = RK_NULL;
	int s32Ret;

	VENC_STREAM_S stFrame;
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));

	while (1) {
		s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);
		if (s32Ret == RK_SUCCESS) {
			if (g_rtsplive && g_rtsp_session) {
				pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}

			s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
			}
		}
		usleep(10 * 1000);
	}
	printf("\n======exit %s=======\n", __func__);
	free(stFrame.pstPack);
	return NULL;
}

static void *RetinaProcessBuffer(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);

	int disp_width  = DISP_WIDTH;
	int disp_height = DISP_HEIGHT;
	int model_width = 640;
	int model_height = 640;
	
	char text[16];	
	float scale_x = (float)disp_width / (float)model_width;  
	float scale_y = (float)disp_height / (float)model_height;   
	int sX,sY,eX,eY;

	int s32Ret;
	int group_count = 0;
	VIDEO_FRAME_INFO_S stViFrame;

	while(1)
	{
		s32Ret = RK_MPI_VI_GetChnFrame(0, 1, &stViFrame, -1);
		if(s32Ret == RK_SUCCESS)
		{
			void *vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			if(vi_data != RK_NULL)
			{
				cv::Mat yuv420sp(disp_height + disp_height / 2, disp_width, CV_8UC1, vi_data);
				cv::Mat bgr(disp_height, disp_width, CV_8UC3);			
				cv::Mat model_bgr(model_height, model_width, CV_8UC3);			

				cv::cvtColor(yuv420sp, bgr, cv::COLOR_YUV420sp2BGR);

				cv::resize(bgr, model_bgr, cv::Size(model_width ,model_height), 0, 0, cv::INTER_LINEAR);	
				memcpy(rknn_app_ctx.input_mems[0]->virt_addr, model_bgr.data, model_width * model_height * 3);
				inference_retinaface_model(&rknn_app_ctx, &od_results);

				for(int i = 0; i < od_results.count; i++)
				{					
					object_detect_result *det_result = &(od_results.results[i]);
					
					if(i == 0)
					{
						sX = (int)((float)det_result->box.left 	 *scale_x);	
						sY = (int)((float)det_result->box.top 	 *scale_y);	
						eX = (int)((float)det_result->box.right  *scale_x);	
						eY = (int)((float)det_result->box.bottom *scale_y);
						printf("%d %d %d %d\n",sX,sY,eX,eY);

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

			}
			s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 1, &stViFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
			}
		}
		else{
			printf("Get viframe error %d !\n", s32Ret);
			continue;
		}

		usleep(500000);		
		for(int i = 0;i < group_count; i++)
			rgn_overlay_release(i);
		group_count = 0;
	}			
	return NULL;
}


int main(int argc, char *argv[]) {
  system("RkLunch-stop.sh");
	RK_S32 s32Ret = 0; 

	int width    = DISP_WIDTH;
    int height   = DISP_HEIGHT;
	
	// Rknn model
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

	// rkmpi init
	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		return -1;
	}
	
	// rtsp init	
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

	// vi init
	vi_dev_init();
	vi_chn_init(0, width, height);
	vi_chn_init(1, width, height);
	
	// venc init
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	venc_init(0, width, height, enCodecType);

	// bind vi to venc	
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;
		
	stvencChn.enModId = RK_ID_VENC;
	stvencChn.s32DevId = 0;
	stvencChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vi0 to venc0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stvencChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 1 ch venc failed");
		return -1;
	}
			
	printf("init success\n");	
	
	pthread_t main_thread;
	pthread_create(&main_thread, NULL, GetMediaBuffer, NULL);
	pthread_t retina_thread;
	pthread_create(&retina_thread, NULL, RetinaProcessBuffer, NULL);
	
	
	while (1) {		
		usleep(50000);
	}

	pthread_join(main_thread, NULL);
	pthread_join(retina_thread, NULL);

	RK_MPI_SYS_UnBind(&stSrcChn, &stvencChn);
	RK_MPI_VI_DisableChn(0, 0);
	RK_MPI_VI_DisableChn(0, 1);
	
	RK_MPI_VENC_StopRecvFrame(0);
	RK_MPI_VENC_DestroyChn(0);
	
	RK_MPI_VI_DisableDev(0);

	RK_MPI_SYS_Exit();

	// Stop RKAIQ
	SAMPLE_COMM_ISP_Stop(0);

	// Release rknn model
    release_retinaface_model(&rknn_app_ctx);	

	return 0;
}
