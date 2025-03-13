/*****************************************************************************
* | Author      :   Luckfox team
* | Function    :   
* | Info        :
*
*----------------
* | This version:   V2.0
* | Date        :   2024-08-26
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#define DISP_WIDTH  1920
#define DISP_HEIGHT 1080

// Unix socket path
#define SOCKET_PATH "/tmp/h264_stream.sock"

// Socket globals
int client_socket = -1;

// Add a status flag to track socket connection state
bool socket_connected = false;

// Connect to Unix socket server using SOCK_SEQPACKET
int connect_to_unix_socket() {
    struct sockaddr_un addr;
    int fd;

    // Create socket with SOCK_SEQPACKET type
    if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
        perror("socket error");
        return -1;
    }

    // Set up socket address
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

    // Try to connect to server
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect error");
        close(fd);
        return -1;
    }

    printf("Connected to Unix socket at %s using SOCK_SEQPACKET mode\n", SOCKET_PATH);
    socket_connected = true;
    return fd;
}

// Read H264 data from socket with timeout
bool read_from_socket(int socket_fd, uint8_t* buffer, size_t* size, int timeout_ms) {
    if (socket_fd <= 0) return false;
    
    // Setup for select() to implement timeout
    fd_set read_fds;
    struct timeval tv;
    
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);
    
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    // Wait until there's data or timeout
    int select_result = select(socket_fd + 1, &read_fds, NULL, NULL, &tv);
    
    if (select_result < 0) {
        perror("select error");
        return false;
    } else if (select_result == 0) {
        // Timeout occurred
        return false;
    }
    
    // Read directly into the buffer up to the maximum size
    ssize_t bytes_read = recv(socket_fd, buffer, *size, 0);
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("Socket closed by server\n");
        } else {
            perror("read error");
        }
        return false;
    }
    
    // Update the size to reflect actual bytes read
    *size = bytes_read;
    return true;
}

// Send H264 frame over socket with proper error handling - only raw data
bool send_h264_frame(int socket_fd, void* data, uint32_t size) {
    if (socket_fd <= 0 || !data || size == 0) {
        return false;
    }
    
    // Send the raw H264 data directly without sending the frame size first
    ssize_t bytes_sent = send(socket_fd, data, size, MSG_NOSIGNAL);
    if (bytes_sent <= 0) {
        perror("Failed to send H264 data");
        socket_connected = false;
        return false;
    } else if (bytes_sent != size) {
        printf("Warning: Incomplete frame sent (%zd of %u bytes)\n", bytes_sent, size);
        return false;
    }
    
    // Successfully sent the complete frame
    // printf("Sent raw H264 frame: %u bytes\n", size);
    return true;
}

int main(int argc, char *argv[]) {
    system("RkLunch-stop.sh");
    RK_S32 s32Ret = 0; 

    int width    = DISP_WIDTH;
    int height   = DISP_HEIGHT;

    char fps_text[16];
    float fps = 0;
    memset(fps_text,0,16);

    //h264_frame    
    VENC_STREAM_S stFrame;    
    stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    RK_U64 H264_PTS = 0;
    RK_U32 H264_TimeRef = 0; 
    VIDEO_FRAME_INFO_S stViFrame;
    
    // Create Pool
    MB_POOL_CONFIG_S PoolCfg;
    memset(&PoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
    PoolCfg.u64MBSize = width * height * 3 ;
    PoolCfg.u32MBCnt = 1;
    PoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
    //PoolCfg.bPreAlloc = RK_FALSE;
    MB_POOL src_Pool = RK_MPI_MB_CreatePool(&PoolCfg);
    printf("Create Pool success !\n");    

    // Get MB from Pool 
    MB_BLK src_Blk = RK_MPI_MB_GetMB(src_Pool, width * height * 3, RK_TRUE);
    
    // Build h264_frame
    VIDEO_FRAME_INFO_S h264_frame;
    h264_frame.stVFrame.u32Width = width;
    h264_frame.stVFrame.u32Height = height;
    h264_frame.stVFrame.u32VirWidth = width;
    h264_frame.stVFrame.u32VirHeight = height;
    h264_frame.stVFrame.enPixelFormat =  RK_FMT_RGB888; 
    h264_frame.stVFrame.u32FrameFlag = 160;
    h264_frame.stVFrame.pMbBlk = src_Blk;
    unsigned char *data = (unsigned char *)RK_MPI_MB_Handle2VirAddr(src_Blk);
    cv::Mat frame(cv::Size(width,height),CV_8UC3,data);

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

    // Connect to Unix socket server
    printf("Attempting to connect to Unix socket server...\n");
    client_socket = connect_to_unix_socket();
    if (client_socket < 0) {
        printf("Failed to connect to Unix socket server. Make sure the server is running.\n");
        printf("Continuing without connection. Will retry later...\n");
        socket_connected = false;
    }

    // vi init
    vi_dev_init();
    vi_chn_init(0, width, height);

    // venc init
    RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
    venc_init(0, width, height, enCodecType);
    
    printf("init success\n");    
    
    while(1) {            
        // Try to reconnect if not connected
        if (client_socket <= 0) {
            printf("Socket disconnected, attempting to reconnect...\n");
            client_socket = connect_to_unix_socket();
            if (client_socket > 0) {
                printf("Successfully reconnected to socket server\n");
                socket_connected = true;
            } else {
                // Add small delay to avoid hammering the CPU with connection attempts
                usleep(1000000); // 1 second
            }
        }
        
        // get vi frame
        h264_frame.stVFrame.u32TimeRef = H264_TimeRef++;
        h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs(); 
        s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, -1);
        if(s32Ret == RK_SUCCESS)
        {
            void *vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);

            cv::Mat yuv420sp(height + height / 2, width, CV_8UC1, vi_data);
            cv::Mat bgr(height, width, CV_8UC3, data);            
            cv::cvtColor(yuv420sp, bgr, cv::COLOR_YUV420sp2BGR);
            cv::resize(bgr, frame, cv::Size(width ,height), 0, 0, cv::INTER_LINEAR);
            
            sprintf(fps_text,"fps = %.2f",fps);        
            cv::putText(frame,fps_text,
                            cv::Point(40, 40),
                            cv::FONT_HERSHEY_SIMPLEX,1,
                            cv::Scalar(0,255,0),2);
            
        }
        memcpy(data, frame.data, width * height * 3);
        
        // encode H264    
        RK_MPI_VENC_SendFrame(0,  &h264_frame ,-1);
    
        // Get H264 stream
        s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);  
        if(s32Ret == RK_SUCCESS) {
            // Send H264 data to socket if connected
            if (client_socket > 0) {
                void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                uint32_t frame_size = stFrame.pstPack->u32Len;
                
                if (!send_h264_frame(client_socket, pData, frame_size)) {
                    printf("Failed to send H264 frame. Closing socket.\n");
                    close(client_socket);
                    client_socket = -1;
                    socket_connected = false;
                }
     
            }
            
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            fps = (float) 1000000 / (float)(nowUs - h264_frame.stVFrame.u64PTS);
            
           
        }

        // release frame 
        s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
        }
        s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
        }
    
    }

    // Clean up socket
    if (client_socket > 0)
        close(client_socket);

    // Destory MB
    RK_MPI_MB_ReleaseMB(src_Blk);
    // Destory Pool
    RK_MPI_MB_DestroyPool(src_Pool);

    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);
        
    SAMPLE_COMM_ISP_Stop(0);

    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);

    free(stFrame.pstPack);

 
    RK_MPI_SYS_Exit();

    return 0;
}
