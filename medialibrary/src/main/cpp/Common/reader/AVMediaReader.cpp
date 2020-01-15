//
// Created by CainHuang on 2019/8/12.
//

#include "AVMediaReader.h"

AVMediaReader::AVMediaReader() {
    av_register_all();
    reset();
    // 初始化数据包
    av_init_packet(&mPacket);
    mPacket.data = nullptr;
    mPacket.size = 0;
    abortRequest = false;
}

AVMediaReader::~AVMediaReader() {
    release();
    abortRequest = true;
    if (mSrcPath) {
        av_freep(&mSrcPath);
        mSrcPath = nullptr;
    }
    if (mFormat) {
        av_freep(&mFormat);
        mFormat = nullptr;
    }
    if (mVideoCodecName) {
        av_freep(&mAudioCodecName);
        mVideoCodecName = nullptr;
    }
    if (mAudioCodecName) {
        av_freep(&mAudioCodecName);
        mAudioCodecName = nullptr;
    }
}

/**
 * 设置数据源
 * @param url
 */
void AVMediaReader::setDataSource(const char *url) {
    mSrcPath = av_strdup(url);
}

/**
 * 设置输入格式
 * @param format 输入格式名称
 */
void AVMediaReader::setInputFormat(const char *format) {
    mFormat = av_strdup(format);
}

/**
 * 设置音频解码器名称
 * @param decoder 解码器名称
 */
void AVMediaReader::setAudioDecoder(const char *decoder) {
    mAudioCodecName = av_strdup(decoder);
}

/**
 * 设置视频解码器名称
 * @param decoder 解码器名称
 */
void AVMediaReader::setVideoDecoder(const char *decoder) {
    mVideoCodecName = av_strdup(decoder);
}

/**
 * 添加格式参数
 * @param key
 * @param value
 */
void AVMediaReader::addFormatOptions(std::string key, std::string value) {
    mFormatOptions[key] = value;
}

/**
 * 添加解码参数
 * @param key
 * @param value
 */
void AVMediaReader::addDecodeOptions(std::string key, std::string value) {
    mDecodeOptions[key] = value;
}

/**
 * 设置读取监听器
 * @param listener
 * @param autoRelease 是否自动释放
 */
void AVMediaReader::setReadListener(OnDecodeListener *listener, bool autoRelease) {
    mReadListener = listener;
    mAutoRelease = autoRelease;
}

/**
 * seekTo定位
 * @param timeMs
 */
int AVMediaReader::seekTo(float timeMs) {
    if (!mMediaDemuxer || !mMediaDemuxer->getContext()) {
        LOGE("Failed to find demuxer or demuxer context");
        return -1;
    }
    if (mMediaDemuxer->getDuration() <= 0) {
        return -1;
    }

    int seekFlags = 0;
    seekFlags &= ~AVSEEK_FLAG_BYTE;
    int64_t start_time = 0;
    int64_t seek_pos = av_rescale(timeMs, AV_TIME_BASE, 1000);
    start_time = mMediaDemuxer->getContext() ? mMediaDemuxer->getContext()->start_time : 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE) {
        seek_pos += start_time;
    }
    int ret = avformat_seek_file(mMediaDemuxer->getContext(), -1, INT64_MIN, seek_pos, INT64_MAX, seekFlags);
    if (ret < 0) {
        LOGE("%s: could not seek to position %0.3f\n", mSrcPath, (double)seek_pos / AV_TIME_BASE);
        return ret;
    }
    return 0;
}

/**
 * 获取时长
 * @return
 */
int64_t AVMediaReader::getDuration() {
    if (mMediaDemuxer) {
        return mMediaDemuxer->getDuration();
    }
    return 0;
}

/**
 * 重置所有参数
 */
void AVMediaReader::reset() {
    mSrcPath = nullptr;
    mReadListener = nullptr;
    mVideoCodecName = nullptr;
    mAudioCodecName = nullptr;
    mFormat = nullptr;
    mSrcPath = nullptr;
}

/**
 * 释放所有资源
 */
void AVMediaReader::release() {
    if (mAudioDecoder != nullptr) {
        mAudioDecoder->closeDecoder();
        mAudioDecoder.reset();
        mAudioDecoder = nullptr;
    }
    if (mVideoDecoder != nullptr) {
        mVideoDecoder->closeDecoder();
        mVideoDecoder.reset();
        mVideoDecoder = nullptr;
    }
    if (mMediaDemuxer != nullptr) {
        mMediaDemuxer->closeDemuxer();
        mMediaDemuxer.reset();
        mMediaDemuxer = nullptr;
    }
    if (mAutoRelease) {
        if (mReadListener != nullptr) {
            delete mReadListener;
        }
    }
    mReadListener = nullptr;
}

/**
 * 打开文件
 * @return
 */
int AVMediaReader::openInputFile() {
    int ret;
    mMediaDemuxer = std::make_shared<AVMediaDemuxer>();
    mMediaDemuxer->setInputPath(mSrcPath);
    mMediaDemuxer->setInputFormat(mFormat);
    ret = mMediaDemuxer->openDemuxer(mFormatOptions);
    if (ret < 0) {
        LOGE("Failed to open media demuxer");
        mMediaDemuxer.reset();
        mMediaDemuxer = nullptr;
        return ret;
    }

    // 打开视频解码器
    if (mMediaDemuxer->hasVideoStream()) {
        mVideoDecoder = std::make_shared<AVVideoDecoder>(mMediaDemuxer);
        mVideoDecoder->setDecoder(mVideoCodecName);
        ret = mVideoDecoder->openDecoder(mDecodeOptions);
        if (ret < 0) {
            LOGE("Failed to open video decoder");
            mVideoDecoder.reset();
            mVideoDecoder = nullptr;
        }
    }

    // 打开音频解码器
    if (mMediaDemuxer->hasAudioStream()) {
        mAudioDecoder = std::make_shared<AVAudioDecoder>(mMediaDemuxer);
        mAudioDecoder->setDecoder(mAudioCodecName);
        ret = mAudioDecoder->openDecoder(mDecodeOptions);
        if (ret < 0) {
            LOGE("Failed to open audio decoder");
            mAudioDecoder.reset();
            mAudioDecoder = nullptr;
        }
    }

    // 打印信息
    mMediaDemuxer->printInfo();

    // 判断是否音频流和视频流都找不到
    if (!mAudioDecoder && !mVideoDecoder) {
        LOGE("Could not find audio or video stream in the input, aborting");
        return -1;
    }

    return 0;
}

/**
 * 解码一个数据包
 * @return
 */
int AVMediaReader::decodePacket() {
    if (!mMediaDemuxer || !mMediaDemuxer->getContext()) {
        LOGE("Failed to find demuxer context");
        return -1;
    }
    if (!mVideoDecoder && !mAudioDecoder) {
        LOGE("Failed to find audio decoder or video decoder");
        return -1;
    }
    int ret = av_read_frame(mMediaDemuxer->getContext(), &mPacket);
    if (ret < 0) {
        LOGE("Failed to call av_read_frame: %s", av_err2str(ret));
        return ret;
    }
    ret = decodePacket(&mPacket, mReadListener);
    av_packet_unref(&mPacket);
    return ret;
}

/**
 * 解码数据包
 * @param packet
 * @param listener
 */
int AVMediaReader::decodePacket(AVPacket *packet, OnDecodeListener *listener) {
    int ret = 0;

    if (!packet || packet->stream_index < 0) {
        return -1;
    }

    // 等效队列消耗足够的帧之后再做处理
    if (listener != nullptr) {
        while (!abortRequest && listener->isDecodeWaiting()) {
        }
    }

    // 如果处于终止状态，则直接退出解码过程
    if (abortRequest) {
        return 0;
    }
    if (packet->stream_index == mVideoDecoder->getStreamIndex() || packet->stream_index == mAudioDecoder->getStreamIndex()) {

        AVCodecContext *pCodecContext = (packet->stream_index == mVideoDecoder->getStreamIndex())
                ? mVideoDecoder->getContext()
                : mAudioDecoder->getContext();
        // 将数据包送去解码
        ret = avcodec_send_packet(pCodecContext, packet);
        if (ret < 0) {
            LOGE("Failed to call avcodec_send_packet: %s", av_err2str(ret));
            return ret;
        }

        while (ret == 0 && !abortRequest) {

            AVFrame *frame = av_frame_alloc();
            if (!frame) {
                LOGE("Failed to allocate audio AVFrame");
                ret = -1;
                break;
            }

            // 取出解码后的AVFrame
            ret = avcodec_receive_frame(pCodecContext, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_unref(frame);
                av_frame_free(&frame);
                break;
            } else if (ret < 0) {
                LOGE("Failed to call avcodec_receive_frame: %s", av_err2str(ret));
                av_frame_unref(frame);
                av_frame_free(&frame);
                break;
            }

            // 将解码后的帧送出去
            if (listener != nullptr) {
                listener->onDecodedFrame(frame, (packet->stream_index == mVideoDecoder->getStreamIndex())
                                                ? AVMEDIA_TYPE_VIDEO
                                                : AVMEDIA_TYPE_AUDIO);
            } else {
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
        }
    }
    return ret;
}