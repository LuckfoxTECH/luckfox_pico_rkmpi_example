#!/bin/bash

ROOT_PWD=$(cd "$(dirname $0)" && cd -P "$(dirname "$SOURCE")" && pwd)

if [ "$1" = "clean" ]; then
	if [ -d "${ROOT_PWD}/build" ]; then
		rm -rf "${ROOT_PWD}/build"
		echo " ${ROOT_PWD}/build has been deleted!"
	fi

	if [ -d "${ROOT_PWD}/install" ]; then
		rm -rf "${ROOT_PWD}/install"
		echo " ${ROOT_PWD}/install has been deleted!"
	fi

	exit
fi

options=("luckfox_pico_rtsp_opencv"
	"luckfox_pico_rtsp_opencv_capture"
	"luckfox_pico_rtsp_retinaface"
	"luckfox_pico_rtsp_retinaface_osd"
	"luckfox_pico_rtsp_yolov5")

PS3="Enter your choice [1-${#options[@]}]: "

select opt in "${options[@]}"; do
	if [[ -n "$opt" ]]; then
		echo "You selected: $opt"
		echo "你选择了: $opt"

		src_dir="example/$opt"
		if [[ -d "$src_dir" ]]; then
			if [ -d ${ROOT_PWD}/build ]; then
				rm -rf ${ROOT_PWD}/build
			fi
			mkdir ${ROOT_PWD}/build
			cd ${ROOT_PWD}/build
			cmake .. -DEXAMPLE_DIR="$src_dir" -DEXAMPLE_NAME="$opt"
			make install
		else
			echo "错误：目录 $src_dir 不存在！"
			echo "Error: Directory $src_dir does not exist!"
		fi
		break
	else
		echo "Invalid selection, please try again."
		echo "无效的选择，请重新选择。"
	fi
done
