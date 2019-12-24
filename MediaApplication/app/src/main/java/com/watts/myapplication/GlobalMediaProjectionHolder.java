package com.watts.myapplication;

import android.media.projection.MediaProjection;

public enum  GlobalMediaProjectionHolder {
    INSTANCE;

    /*
     * TODO: listen to remote stop: add callback
     */
    private volatile MediaProjection mediaProjection = null;
    private MediaProjection.Callback mCallback = new MediaProjection.Callback() {
        @Override
        public void onStop() {
            super.onStop();
            mediaProjection.unregisterCallback(mCallback);
        }
    };
    private Object lock = new Object();

    public void set(MediaProjection mp) {
        synchronized (lock) {
            if (mediaProjection == mp) {
            } else {
                // new MediaProjection is setting
                if (null != mediaProjection) {
                    mediaProjection.stop();
                }
                mediaProjection = mp;
                mediaProjection.registerCallback(mCallback, null);
            }
        }
    }

    public MediaProjection get() {
        synchronized (lock) {
            return mediaProjection;
        }
    }
}
