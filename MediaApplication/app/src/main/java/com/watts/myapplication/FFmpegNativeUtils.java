package com.watts.myapplication;

import java.nio.ByteBuffer;

/**
 * @author wxc on 2019-07-05.
 */
public class FFmpegNativeUtils {
    public native static void filterVideo(String videoPath, String filterVideoPath, String filterImagePath);
    public native static void generateVideo(String path);
    public native static void transcodeVideo(String inputPath, String outputPath);
}
