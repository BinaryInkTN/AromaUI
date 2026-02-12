package com.example.robot_control;

import android.app.Activity;
import android.widget.Toast;

public class AromaHelper {
    public static void showToast(final Activity activity, final String msg, final boolean longDuration) {
        if (activity == null) return;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Toast.makeText(activity, msg, longDuration ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show();
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        });
    }
}
