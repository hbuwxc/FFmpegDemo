package com.watts.myapplication;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.widget.MediaController;
import android.widget.VideoView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = MainActivity.class.getSimpleName();

    private static final int REUQEST_PERMISSION_CAPTURE = 100;

    private final Lock mImageReaderLock = new ReentrantLock(true);

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avfilter");
        System.loadLibrary("postproc");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
        System.loadLibrary("native-lib");
    }

    VideoView mVideoView;
    String demoVideoPath;
    String filterVideoPath;
    String filterImagePath;
    String mergeOutputPath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mVideoView = findViewById(R.id.mVideoView);

        stringFromJNI();

        if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 0);
        } else {
            copyResource();
        }
    }

    private void copyResource() {
        demoVideoPath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "/111/base.mp4";
        filterVideoPath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "/111/filter_video.mp4";
        filterImagePath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "/111/test.png";
        mergeOutputPath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "/111/merge_output.mp4";
        File f = new File(demoVideoPath);
        if (!f.exists()) {
            copyAssets(this, "demo.mp4", demoVideoPath);
        }

        File imageFile = new File(filterImagePath);
        if (!imageFile.exists()) {
            copyAssets(this, "test.png", filterImagePath);
        }

        createNewFile(mergeOutputPath);

        mVideoView.setVideoPath(demoVideoPath);
        mVideoView.setMediaController(new MediaController(this));
        mVideoView.setOnPreparedListener(new MediaPlayer.OnPreparedListener() {
            @Override
            public void onPrepared(MediaPlayer mp) {
                Log.e(TAG, "video Prepared");
                mp.setLooping(true);
            }
        });

        mVideoView.start();
    }

    public static void copyAssets(Context context, String fileName, String newPath) {
        try {
            InputStream is = context.getAssets().open(fileName);
            File f = new File(newPath);
            if (!f.getParentFile().exists()) {
                f.getParentFile().mkdirs();
            } else if (!f.exists()) {
                f.createNewFile();
            }
            FileOutputStream fos = new FileOutputStream(f);
            byte[] buffer = new byte[1024];
            int byteCount;
            while ((byteCount = is.read(buffer)) != -1) {
                // buffer字节
                fos.write(buffer, 0, byteCount);
            }
            fos.flush();// 刷新缓冲区
            is.close();
            fos.close();
            Log.e(TAG, "copy demo mp4 to data directory , path = " + newPath);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void createNewFile(String path){
        File filterFile = new File(path);
        if (filterFile.exists()){
            filterFile.delete();
            try {
                filterFile.createNewFile();
            } catch (IOException e) {
                e.printStackTrace();
            }
        } else {
            try {
                filterFile.createNewFile();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();


    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED) {
            copyResource();
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    public void filterVideo(View view) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                createNewFile(filterVideoPath);
                FFmpegNativeUtils.filterVideo(demoVideoPath, filterVideoPath, generateFilterGraph(5));
            }
        }).start();
    }

    private String generateFilterGraph(int location){
        return "movie="+filterImagePath+"[wm];[in][wm]overlay=5:"+location+"[out]";
    }

    public void generateVideo(View view) {
        FFmpegNativeUtils.generateVideo(filterVideoPath);
    }

    public void transcodeVideo(View view) {
        FFmpegNativeUtils.transcodeVideo(demoVideoPath, filterVideoPath);
    }

    public void mergeVideo(View view) {
        FFmpegNativeUtils.mergeVideo(demoVideoPath, filterVideoPath, mergeOutputPath, generateFilterGraph(100));

    }
}
