/*****************************************************************************
* | Author      :   Luckfox team
* | Function    :   
* | Info        :
*
*----------------
* | This version:   V1.0
* | Date        :   2024-04-07
* | Info        :   Basic version
*
******************************************************************************/

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

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

int main(int argc, char *argv[]) {
  system("RkLunch-stop.sh");
  RK_S32 s32Ret = 0; 

	int sX,sY,eX,eY;
	int width    = 2304;
    int height   = 1296;

	char fps_text[16];
	float fps = 0;
	memset(fps_text,0,16);

	//h264_frame	
	VENC_STREAM_S stFrame;	
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
 	VIDEO_FRAME_INFO_S h264_frame;
 	VIDEO_FRAME_INFO_S stVpssFrame;

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
	rtsp_demo_handle g_rtsplive = NULL;
	rtsp_session_handle g_rtsp_session;
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

	// vi init
	vi_dev_init();
	vi_chn_init(0, width, height);

	// vpss init
	vpss_init(0, width, height);

	// bind vi to vpss
	MPP_CHN_S stSrcChn, stvpssChn;
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;

	stvpssChn.enModId = RK_ID_VPSS;
	stvpssChn.s32DevId = 0;
	stvpssChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vi0 to vpss0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		return -1;
	}

	// venc init
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	venc_init(0, width, height, enCodecType);
	
	while(1)
	{	
		

		// get vpss frame
		s32Ret = RK_MPI_VPSS_GetChnFrame(0,0, &stVpssFrame,-1);
		if(s32Ret == RK_SUCCESS)
		{
			void *data = RK_MPI_MB_Handle2VirAddr(stVpssFrame.stVFrame.pMbBlk);

			cv::Mat frame(height,width,CV_8UC3, data);	
			sprintf(fps_text,"fps = %.2f",fps);		
            cv::putText(frame,fps_text,
							cv::Point(40, 40),
							cv::FONT_HERSHEY_SIMPLEX,1,
							cv::Scalar(0,255,0),2);
			memcpy(data, frame.data, width * height * 3);					
		}


		// send stream
		// encode H264	
		RK_MPI_VENC_SendFrame(0, &stVpssFrame,-1);
		// rtsp
		s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);
		if(s32Ret == RK_SUCCESS)
		{
			if(g_rtsplive && g_rtsp_session)
			{
				//printf("len = %d PTS = %d \n",stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);	
				void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, stFrame.pstPack->u32Len,
							  stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}
			RK_U64 nowUs = TEST_COMM_GetNowUs();
			fps = (float) 1000000 / (float)(nowUs - stVpssFrame.stVFrame.u64PTS);			
		}

		// release frame 
		s32Ret = RK_MPI_VPSS_ReleaseChnFrame(0, 0, &stVpssFrame);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
		}
		s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
		}

	}


	RK_MPI_SYS_UnBind(&stSrcChn, &stvpssChn);
	
	RK_MPI_VI_DisableChn(0, 0);
	RK_MPI_VI_DisableDev(0);
	
	RK_MPI_VPSS_StopGrp(0);
	RK_MPI_VPSS_DestroyGrp(0);
	
	SAMPLE_COMM_ISP_Stop(0);

	RK_MPI_VENC_StopRecvFrame(0);
	RK_MPI_VENC_DestroyChn(0);

	free(stFrame.pstPack);

	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	
	RK_MPI_SYS_Exit();

	return 0;
}
