package com.watts.myapplication.utils;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Random;

/**
 * Created by zhangxiao on 2018/9/10
 */
public class ScreenUtils {

    public static int getScreenHeight(Context context) {
        try {
            return context.getResources().getDisplayMetrics().heightPixels;
        } catch (Exception e) {
            e.printStackTrace();
        }
        return 0;
    }

    public static int getScreenWidth(Context context) {
        try {
            return context.getResources().getDisplayMetrics().widthPixels;
        } catch (Exception e) {
            e.printStackTrace();
        }
        return 0;
    }

    public static int getFullScreenWidth(Context context) {
        int width = -1;
        if (Build.BRAND.equalsIgnoreCase("asus") || Build.BRAND.equalsIgnoreCase("huawei")) {
            try {
                WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
                Display display = windowManager.getDefaultDisplay();
                Field f = null;
                f = Display.class.getDeclaredField("mDisplayInfo");
                if (f != null) {
                    f.setAccessible(true);
                    Object o = f.get(display);
                    Class c = Class.forName("android.view.DisplayInfo");
                    Field f1 = c.getDeclaredField("logicalWidth");
                    width = f1.getInt(o);
                }
            } catch (Throwable e) {
                e.printStackTrace();
            }
        }
        if (width == -1){
            width = getFullScreenWidthDefault(context);
        }
        return width;
    }


    public static int getFullScreenHeight(Context context) {
        int height = -1;
        if (Build.BRAND.equals("asus")  ||Build.BRAND.equalsIgnoreCase("huawei")) {
            try {
                WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
                Display display = windowManager.getDefaultDisplay();
                Field f = null;
                f = Display.class.getDeclaredField("mDisplayInfo");
                if (f != null) {
                    f.setAccessible(true);
                    Object o = f.get(display);
                    Class c = Class.forName("android.view.DisplayInfo");
                    Field f1 = c.getDeclaredField("logicalHeight");
                    height = f1.getInt(o);
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
        if (height == -1){
            height = getFullScreenHeightDefault(context);
        }
        return height;
    }

    /**
     * 获取屏幕原始尺寸高度，包括虚拟键
     *
     * @param context
     * @return
     */
    public static int getFullScreenWidthDefault(Context context) {
        int height = 0;
        try {
            WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            Display display = windowManager.getDefaultDisplay();
            DisplayMetrics displayMetrics = new DisplayMetrics();
            @SuppressWarnings("rawtypes")
            Class c;
            c = Class.forName("android.view.Display");
            @SuppressWarnings("unchecked")
            Method method = c.getMethod("getRealMetrics", DisplayMetrics.class);
            method.invoke(display, displayMetrics);
            height = displayMetrics.widthPixels;
        } catch (Throwable e) {
            e.printStackTrace();
        }
        return height;
    }

    /**
     * 获取屏幕原始尺寸高度，包括虚拟键
     *
     * @param context
     * @return
     */
    public static int getFullScreenHeightDefault(Context context) {
        int height = 0;
        try {
            WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            Display display = windowManager.getDefaultDisplay();
            DisplayMetrics displayMetrics = new DisplayMetrics();
            @SuppressWarnings("rawtypes")
            Class c;
            c = Class.forName("android.view.Display");
            @SuppressWarnings("unchecked")
            Method method = c.getMethod("getRealMetrics", DisplayMetrics.class);
            method.invoke(display, displayMetrics);
            height = displayMetrics.heightPixels;
        } catch (Exception e) {
            e.printStackTrace();
        }
        return height;
    }


    /**
     * 获得状态栏的高度
     *
     * @param context
     * @return
     */
    public static int getStatusBarHeight(Context context) {
        int statusHeight = 24;
        try {
            Class<?> clazz = Class.forName("com.android.internal.R$dimen");
            Object object = clazz.newInstance();
            int height = Integer.parseInt(clazz.getField("status_bar_height").get(object).toString());
            statusHeight = context.getResources().getDimensionPixelSize(height);
        } catch (Throwable e) {
            e.printStackTrace();
        }
        return statusHeight;
    }

    /**
     * 获取虚拟按键的高度
     *
     * @param context
     * @return
     */
    public static int getNavigationBarHeight(Context context) {
        int totalHeight = getFullScreenHeight(context);
        int contentHeight = getScreenHeight(context);
        return totalHeight - contentHeight;
    }

    // 计算视频宽高大小，视频比例xxx*xxx按屏幕比例放大或者缩小
    public static int[] reckonVideoWidthHeight(float width, float height, Context mContext) {
        try {
            int sWidth = getScreenWidth(mContext);
            float wRatio = 0.0f;
            wRatio = (sWidth - width) / width;
            // 等比缩放
            int nWidth = sWidth;
            int nHeight = (int) (height * (wRatio + 1));
            return new int[]{nWidth, nHeight};
        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }

    public static int dip2px(Context context, float dpValue) {
        float scale = context.getResources().getDisplayMetrics().density;
        return (int) (dpValue * scale + 0.5f);
    }

    public static int sp2px(Context context, float spValue) {
        final float fontScale = context.getResources().getDisplayMetrics().scaledDensity;
        return (int) (spValue * fontScale + 0.5f);
    }

    private static boolean isPortrait(Context context) {
        Configuration configuration = context.getResources().getConfiguration();
        if (configuration != null && configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            return false;
        }
        return true;
    }

    public static void hideSystemUI(Window context) {
        // Enables regular immersive mode.
        // For "lean back" mode, remove SYSTEM_UI_FLAG_IMMERSIVE.
        // Or for "sticky immersive," replace it with SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        View decorView = context.getDecorView();
        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE
                        // Set the content to appear under the system bars so that the
                        // content doesn't resize when the system bars hide and show.
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        // Hide the nav bar and status bar
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN);
    }

    public static void showSystemUI(Window context) {
        View decorView = context.getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    public static String generateFileName(String phoneType, int width, int height) {
        Random random = new Random();
        StringBuilder sb = new StringBuilder();
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMddHHmmss", Locale.US);
        sb.append(format.format(new Date()) + "_" + phoneType + "_" + width + "_" + height + "_"
                + Build.VERSION.RELEASE + "_" + random.nextInt(999));
        return sb.toString() + ".JPEG";
    }

    public static String generateZipFileName(String phoneType) {
        StringBuilder sb = new StringBuilder();
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMddHHmmss", Locale.US);
        sb.append(format.format(new Date()) + "_" + phoneType + "_" + Build.VERSION.RELEASE);
        return sb.toString() + ".ZIP";
    }

    public static String generateFileName(String phoneType, int width, int height, String name) {
        Random random = new Random();
        StringBuilder sb = new StringBuilder();
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMddHHmmss", Locale.US);
        sb.append(format.format(new Date())).append("_").append(name).append("_").append(phoneType).append("_").append(width).append("_").append(height).append("_").append(random.nextInt(999));
        return sb.toString() + ".JPEG";
    }

    /**
     * 向本地SD卡写网络图片
     *
     * @param bitmap
     */
    public static String saveBitmapToLocal(File filePath, String fileName, Bitmap bitmap) {
        try {
			/*
			 创建文件流，指向该路径，文件名叫做fileName
			  */
            File file = new File(filePath, fileName);
			/*
			 file其实是图片，它的父级File是文件夹，判断一下文件夹是否存在，如果不存在，创建文件夹
			  */
            File fileParent = file.getParentFile();
            if (!fileParent.exists()) {
				/*
				 文件夹不存在
				  */
                fileParent.mkdirs();// 创建文件夹
            }
            try (FileOutputStream fos = new FileOutputStream(file)) {
                bitmap.compress(Bitmap.CompressFormat.JPEG, 100,
                        fos);
            } catch (Exception e) {

            }
			/*
			 将图片保存到本地
			  */
            return file.getAbsolutePath();
        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }
}
