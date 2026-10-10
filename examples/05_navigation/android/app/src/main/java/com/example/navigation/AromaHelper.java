package com.example.navigation;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.PictureInPictureParams;
import android.app.WallpaperManager;
import android.hardware.biometrics.BiometricManager;
import android.hardware.biometrics.BiometricPrompt;
import android.content.ContentResolver;
import android.database.Cursor;
import android.hardware.ConsumerIrManager;
import android.location.Criteria;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.net.Uri;
import android.nfc.NdefMessage;
import android.nfc.NdefRecord;
import android.nfc.NfcAdapter;
import android.nfc.Tag;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.StatFs;
import android.os.VibrationEffect;
import android.provider.ContactsContract;
import android.util.Rational;
import android.view.PixelCopy;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothSocket;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.os.PowerManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.speech.tts.TextToSpeech;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;


public class AromaHelper {
    private static final String TAG = "AromaHelper";
    
    private static final UUID UUID_SPP = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_HEADSET = UUID.fromString("00001108-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_HANDSFREE = UUID.fromString("0000111F-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_A2DP_SOURCE = UUID.fromString("0000110A-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_A2DP_SINK = UUID.fromString("0000110B-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_AVRCP = UUID.fromString("0000110E-0000-1000-8000-00805F9B34FB");
    private static final UUID UUID_HID = UUID.fromString("00001124-0000-1000-8000-00805F9B34FB");
    
    public static final int TYPE_UNKNOWN = 0;
    public static final int TYPE_HEADSET = 1;
    public static final int TYPE_PHONE = 2;
    public static final int TYPE_SPEAKER = 3;
    public static final int TYPE_WEARABLE = 4;
    public static final int TYPE_KEYBOARD = 5;
    public static final int TYPE_MOUSE = 6;
    public static final int TYPE_PRINTER = 7;
    public static final int TYPE_CAR = 8;
    public static final int TYPE_MEDICAL = 9;
    public static final int TYPE_ARDUINO = 10;
    public static final int TYPE_RASPBERRY = 11;
    
    public static final int MODE_DATA = 0;
    public static final int MODE_AUDIO = 1;
    public static final int MODE_HID = 2;
    public static final int MODE_AUTO = 3;
    
    public static final int SCAN_MODE_PAIRED = 0;
    public static final int SCAN_MODE_NEW = 1;
    public static final int SCAN_MODE_ALL = 2;
    
    private static BluetoothAdapter btAdapter = null;
    private static Handler mainHandler = null;
    private static Context appContext = null;
    private static int connectionMode = MODE_AUTO;
    
    private static BluetoothSocket btSocket = null;
    private static InputStream btInputStream = null;
    private static OutputStream btOutputStream = null;
    private static ConnectThread connectThread = null;
    private static ReadThread readThread = null;
    private static final Object lock = new Object();
    
    private static volatile boolean isConnecting = false;
    private static volatile boolean isConnected = false;
    private static volatile int currentMode = MODE_AUTO;
    
    private static String connectedDeviceName = "";
    private static int connectedDeviceType = TYPE_UNKNOWN;
    private static String connectedDeviceAddress = "";
    private static BluetoothDevice connectedDevice = null;
    
    private static BluetoothProfile a2dpProfile = null;
    private static BluetoothProfile headsetProfile = null;
    private static boolean a2dpConnected = false;
    private static boolean headsetConnected = false;
    
    private static boolean isScanning = false;
    private static BroadcastReceiver scanReceiver = null;
    private static List<BluetoothDevice> discoveredDevices = new CopyOnWriteArrayList<>();
    
    private static ExecutorService backgroundExecutor = Executors.newSingleThreadExecutor();
    
    private static long lastToastTime = 0;
    private static final long TOAST_THROTTLE_MS = 1000;
    private static String pendingToastMessage = null;
    private static boolean pendingToastLongDuration = false;
    private static Runnable toastRunnable = null;
    
    private static int audioConnectionAttempts = 0;
    private static final int MAX_AUDIO_CONNECTION_ATTEMPTS = 3;
    private static final int AUDIO_CONNECTION_TIMEOUT_MS = 10000;
    private static String pendingAudioConnectionAddress = null;
    private static Runnable audioConnectionTimeoutRunnable = null;
    private static boolean audioConnectionVerified = false;
    /* BLUETOOTH */
    public interface BluetoothCallback {
        void onConnectionResult(boolean success, String deviceName, int deviceType, int mode);
        void onDataReceived(byte[] data, int length);
        void onConnectionStateChanged(int state);
        void onDeviceDiscovered(String address, String name, int type, int rssi);
        void onScanFinished();
        void onPairingResult(boolean success, String address, String name);
        void onBleDevice(String address, String name, int rssi);
        void onBleScanFinished();
        void onBleConnection(String address, int status, boolean connected);
        void onBleServices(String address, String servicesCsv);
        void onBleData(String address, String charUuid, byte[] data, int length);
        void onBleWrite(String address, String charUuid, int status);
    }

    public interface SensorCallback {
        void onSensorChanged(int type, float x, float y, float z, long timestamp);
    }
    
    private static List<BluetoothCallback> callbacks = new CopyOnWriteArrayList<>();
    
    public static class NativeCallback implements BluetoothCallback, SensorCallback {
        static {
            System.loadLibrary("aroma_app");
        }
        
        public native void onDeviceDiscovered(String address, String name, int type, int rssi);
        public native void onScanFinished();
        public native void onPairingResult(boolean success, String address, String name);
        public native void onConnectionResult(boolean success, String deviceName, int deviceType, int mode);
        public native void onDataReceived(byte[] data, int length);
        public native void onConnectionStateChanged(int state);
        public native void onBleDevice(String address, String name, int rssi);
        public native void onBleScanFinished();
        public native void onBleConnection(String address, int status, boolean connected);
        public native void onBleServices(String address, String servicesCsv);
        public native void onBleData(String address, String charUuid, byte[] data, int length);
        public native void onBleWrite(String address, String charUuid, int status);
        public native void onSensorChanged(int type, float x, float y, float z, long timestamp);
        public native void onNfcTag(String payload);
        public native void onRfidTag(String uid);
        public native void onBiometric(boolean success);
        public native void onLocation(double lat, double lon, float accuracy, long timeMs);
        public native void onScreenshot(String path);
        
        public NativeCallback() {
            Log.d(TAG, "NativeCallback created");
        }
    }
    
    static {
        mainHandler = new Handler(Looper.getMainLooper());
        btAdapter = BluetoothAdapter.getDefaultAdapter();
        Log.d(TAG, "Static initialization complete, BluetoothAdapter: " + (btAdapter != null ? "available" : "null"));
    }
    
    public static void init(Context context) {
        Log.d(TAG, "init called with context: " + context);
        appContext = context.getApplicationContext();
        registerAudioProfileListeners();
        Log.d(TAG, "init complete");
    }
    
    public static void addCallback(BluetoothCallback callback) {
        Log.d(TAG, "addCallback: " + callback);
        if (callback != null && !callbacks.contains(callback)) {
            callbacks.add(callback);
            Log.d(TAG, "Callback added, total callbacks: " + callbacks.size());
        }
    }
    
    public static void removeCallback(BluetoothCallback callback) {
        Log.d(TAG, "removeCallback: " + callback);
        callbacks.remove(callback);
        Log.d(TAG, "Callback removed, total callbacks: " + callbacks.size());
    }
    
    public static void setConnectionMode(int mode) {
        Log.d(TAG, "setConnectionMode: " + mode);
        connectionMode = mode;
    }

    
    private static void registerAudioProfileListeners() {
        Log.d(TAG, "registerAudioProfileListeners");
        if (appContext == null || btAdapter == null) {
            Log.e(TAG, "Cannot register audio profile listeners - appContext or btAdapter is null");
            return;
        }
        
        btAdapter.getProfileProxy(appContext, new BluetoothProfile.ServiceListener() {
            @Override
            public void onServiceConnected(int profile, BluetoothProfile proxy) {
                Log.d(TAG, "A2DP service connected, profile: " + profile);
                if (profile == BluetoothProfile.A2DP) {
                    a2dpProfile = proxy;
                    checkAudioConnections();
                }
            }
            
            @Override
            public void onServiceDisconnected(int profile) {
                Log.d(TAG, "A2DP service disconnected, profile: " + profile);
                if (profile == BluetoothProfile.A2DP) {
                    a2dpProfile = null;
                    a2dpConnected = false;
                    updateAudioConnectionState();
                }
            }
        }, BluetoothProfile.A2DP);
        
        btAdapter.getProfileProxy(appContext, new BluetoothProfile.ServiceListener() {
            @Override
            public void onServiceConnected(int profile, BluetoothProfile proxy) {
                Log.d(TAG, "Headset service connected, profile: " + profile);
                if (profile == BluetoothProfile.HEADSET) {
                    headsetProfile = proxy;
                    checkAudioConnections();
                }
            }
            
            @Override
            public void onServiceDisconnected(int profile) {
                Log.d(TAG, "Headset service disconnected, profile: " + profile);
                if (profile == BluetoothProfile.HEADSET) {
                    headsetProfile = null;
                    headsetConnected = false;
                    updateAudioConnectionState();
                }
            }
        }, BluetoothProfile.HEADSET);
        
        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
        filter.addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED);
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH) {
            try {
                Class<?> a2dpClass = Class.forName("android.bluetooth.BluetoothA2dp");
                java.lang.reflect.Field field = a2dpClass.getField("ACTION_CONNECTION_STATE_CHANGED");
                String a2dpAction = (String) field.get(null);
                filter.addAction(a2dpAction);
                
                Class<?> headsetClass = Class.forName("android.bluetooth.BluetoothHeadset");
                field = headsetClass.getField("ACTION_CONNECTION_STATE_CHANGED");
                String headsetAction = (String) field.get(null);
                filter.addAction(headsetAction);
            } catch (Exception e) {
                Log.e(TAG, "Error adding audio actions to filter", e);
            }
        }
        
        appContext.registerReceiver(audioConnectionReceiver, filter);
        
        Log.d(TAG, "Audio profile listeners registered");
    }
    
    private static final BroadcastReceiver audioConnectionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            
            if (BluetoothDevice.ACTION_ACL_CONNECTED.equals(action)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (device != null) {
                    Log.d(TAG, "ACL connected: " + device.getName());
                    handleAudioConnectionVerified(device);
                }
            } else if (BluetoothDevice.ACTION_ACL_DISCONNECTED.equals(action)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (device != null && device.getAddress().equals(connectedDeviceAddress)) {
                    Log.d(TAG, "ACL disconnected: " + device.getName());
                    handleAudioDisconnection();
                }
            } else if (action != null && action.contains("CONNECTION_STATE_CHANGED")) {
                int state = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (device != null) {
                    Log.d(TAG, "Profile state changed: " + state + " for " + device.getName());
                    if (state == BluetoothProfile.STATE_CONNECTED) {
                        handleAudioConnectionVerified(device);
                    } else if (state == BluetoothProfile.STATE_DISCONNECTED) {
                        if (device.getAddress().equals(connectedDeviceAddress)) {
                            handleAudioDisconnection();
                        }
                    }
                }
            }
        }
    };
    
    private static void handleAudioConnectionVerified(BluetoothDevice device) {
        if (pendingAudioConnectionAddress != null && 
            device.getAddress().equals(pendingAudioConnectionAddress)) {
            Log.d(TAG, "Audio connection verified for: " + device.getName());
            
            if (audioConnectionTimeoutRunnable != null) {
                mainHandler.removeCallbacks(audioConnectionTimeoutRunnable);
                audioConnectionTimeoutRunnable = null;
            }
            
            audioConnectionVerified = true;
            pendingAudioConnectionAddress = null;
            audioConnectionAttempts = 0;
            
            connectedDevice = device;
            connectedDeviceName = device.getName();
            if (connectedDeviceName == null) connectedDeviceName = "Unknown";
            connectedDeviceAddress = device.getAddress();
            connectedDeviceType = detectDeviceType(device);
            
            isConnected = true;
            currentMode = MODE_AUDIO;
            
            notifyConnectionResult(true, connectedDeviceName, connectedDeviceType, MODE_AUDIO);
        }
    }
    
    private static void handleAudioDisconnection() {
        if (isConnected && currentMode == MODE_AUDIO) {
            Log.d(TAG, "Audio connection lost");
            isConnected = false;
            notifyConnectionResult(false, connectedDeviceName, connectedDeviceType, MODE_AUDIO);
        }
    }
    
    private static void checkAudioConnections() {
        Log.d(TAG, "checkAudioConnections");
        if (a2dpProfile != null) {
            try {
                Method getConnectedDevices = a2dpProfile.getClass().getMethod("getConnectedDevices");
                @SuppressWarnings("unchecked")
                List<BluetoothDevice> devices = (List<BluetoothDevice>) getConnectedDevices.invoke(a2dpProfile);
                a2dpConnected = devices != null && !devices.isEmpty();
                Log.d(TAG, "A2DP connected devices: " + (devices != null ? devices.size() : 0) + ", a2dpConnected: " + a2dpConnected);
                if (a2dpConnected && devices != null && devices.size() > 0) {
                    connectedDevice = devices.get(0);
                    connectedDeviceName = connectedDevice.getName();
                    if (connectedDeviceName == null) connectedDeviceName = "Unknown";
                    connectedDeviceAddress = connectedDevice.getAddress();
                    connectedDeviceType = detectDeviceType(connectedDevice);
                    Log.d(TAG, "A2DP device: " + connectedDeviceName + ", type: " + connectedDeviceType);
                    isConnected = true;
                    currentMode = MODE_AUDIO;
                }
            } catch (Exception e) {
                Log.e(TAG, "Error checking A2DP connections", e);
            }
        }
        
        if (headsetProfile != null) {
            try {
                Method getConnectedDevices = headsetProfile.getClass().getMethod("getConnectedDevices");
                @SuppressWarnings("unchecked")
                List<BluetoothDevice> devices = (List<BluetoothDevice>) getConnectedDevices.invoke(headsetProfile);
                headsetConnected = devices != null && !devices.isEmpty();
                Log.d(TAG, "Headset connected devices: " + (devices != null ? devices.size() : 0) + ", headsetConnected: " + headsetConnected);
                if (headsetConnected && devices != null && devices.size() > 0 && !a2dpConnected) {
                    connectedDevice = devices.get(0);
                    connectedDeviceName = connectedDevice.getName();
                    if (connectedDeviceName == null) connectedDeviceName = "Unknown";
                    connectedDeviceAddress = connectedDevice.getAddress();
                    connectedDeviceType = detectDeviceType(connectedDevice);
                    Log.d(TAG, "Headset device: " + connectedDeviceName + ", type: " + connectedDeviceType);
                    isConnected = true;
                    currentMode = MODE_AUDIO;
                }
            } catch (Exception e) {
                Log.e(TAG, "Error checking headset connections", e);
            }
        }
        
        updateAudioConnectionState();
    }
    
    private static void updateAudioConnectionState() {
        boolean wasConnected = isConnected;
        isConnected = a2dpConnected || headsetConnected;
        Log.d(TAG, "updateAudioConnectionState - wasConnected: " + wasConnected + ", isConnected: " + isConnected);
        
        if (isConnected && !wasConnected) {
            Log.d(TAG, "Audio connection established, notifying");
            notifyConnectionResult(true, connectedDeviceName, connectedDeviceType, MODE_AUDIO);
        } else if (!isConnected && wasConnected) {
            Log.d(TAG, "Audio connection lost, notifying");
            notifyConnectionResult(false, "", TYPE_UNKNOWN, MODE_AUDIO);
        }
    }
    
    public static void showToast(Activity activity, String msg, boolean longDuration) {
        Log.d(TAG, "showToast: " + msg + ", longDuration: " + longDuration);
        if (activity == null && appContext == null) {
            Log.e(TAG, "Cannot show toast - no context available");
            return;
        }
        
        final Context context = (activity != null) ? activity : appContext;
        if (context == null) {
            Log.e(TAG, "Cannot show toast - context is null");
            return;
        }
        
        long now = System.currentTimeMillis();
        
        synchronized (AromaHelper.class) {
            if (toastRunnable != null) {
                mainHandler.removeCallbacks(toastRunnable);
                toastRunnable = null;
            }
            
            pendingToastMessage = msg;
            pendingToastLongDuration = longDuration;
            
            long timeSinceLastToast = now - lastToastTime;
            long delay = 0;
            
            if (timeSinceLastToast < TOAST_THROTTLE_MS) {
                delay = TOAST_THROTTLE_MS - timeSinceLastToast;
            }
            
            toastRunnable = new Runnable() {
                @Override
                public void run() {
                    try {
                        Toast.makeText(context, pendingToastMessage, 
                            pendingToastLongDuration ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show();
                        lastToastTime = System.currentTimeMillis();
                        Log.d(TAG, "Toast shown: " + pendingToastMessage);
                    } catch (Exception e) {
                        Log.e(TAG, "Error showing toast", e);
                    }
                    pendingToastMessage = null;
                    toastRunnable = null;
                }
            };
            
            if (delay > 0) {
                mainHandler.postDelayed(toastRunnable, delay);
            } else {
                mainHandler.post(toastRunnable);
            }
        }
    }
    
    public static void startScan(int scanMode) {
        Log.d(TAG, "startScan called with mode: " + scanMode);
        
        if (btAdapter == null) {
            Log.e(TAG, "Bluetooth adapter is null");
            return;
        }
        
        if (!btAdapter.isEnabled()) {
            Log.e(TAG, "Bluetooth is not enabled");
            showToast(null, "Please enable Bluetooth", true);
            return;
        }
        
        if (isScanning) {
            Log.d(TAG, "Already scanning, stopping current scan");
            stopScan();
        }
        
        discoveredDevices.clear();
        isScanning = true;
        
        if (scanMode == SCAN_MODE_PAIRED || scanMode == SCAN_MODE_ALL) {
            Set<BluetoothDevice> pairedDevices = btAdapter.getBondedDevices();
            Log.d(TAG, "Adding " + pairedDevices.size() + " paired devices");
            
            backgroundExecutor.execute(new Runnable() {
                @Override
                public void run() {
                    for (BluetoothDevice device : pairedDevices) {
                        discoveredDevices.add(device);
                        String name = device.getName();
                        if (name == null) name = "Unknown";
                        final String finalName = name;
                        final String address = device.getAddress();
                        final int type = detectDeviceType(device);
                        final int rssi = 0;
                        
                        mainHandler.post(new Runnable() {
                            @Override
                            public void run() {
                                notifyDeviceDiscovered(address, finalName, type, rssi);
                            }
                        });
                    }
                }
            });
        }
        
        if (scanMode == SCAN_MODE_NEW || scanMode == SCAN_MODE_ALL) {
            scanReceiver = new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    String action = intent.getAction();
                    
                    if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                        final BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                        final int rssi = intent.getShortExtra(BluetoothDevice.EXTRA_RSSI, (short) 0);
                        
                        if (device != null && !discoveredDevices.contains(device)) {
                            discoveredDevices.add(device);
                            
                            backgroundExecutor.execute(new Runnable() {
                                @Override
                                public void run() {
                                    String name = device.getName();
                                    if (name == null) name = "Unknown";
                                    final String finalName = name;
                                    final String address = device.getAddress();
                                    final int type = detectDeviceType(device);
                                    
                                    Log.d(TAG, "Discovered new device: " + finalName + " [" + address + "] RSSI: " + rssi);
                                    
                                    mainHandler.post(new Runnable() {
                                        @Override
                                        public void run() {
                                            notifyDeviceDiscovered(address, finalName, type, rssi);
                                        }
                                    });
                                }
                            });
                        }
                    }
                }
            };
            
            IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_FOUND);
            appContext.registerReceiver(scanReceiver, filter);
            
            btAdapter.startDiscovery();
            Log.d(TAG, "Started Bluetooth discovery");
            
            mainHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    stopScan();
                }
            }, 12000);
        } else {
            mainHandler.post(new Runnable() {
                @Override
                public void run() {
                    notifyScanFinished();
                }
            });
        }
    }
    
    public static void stopScan() {
        Log.d(TAG, "stopScan called");
        
        if (btAdapter != null && btAdapter.isDiscovering()) {
            btAdapter.cancelDiscovery();
            Log.d(TAG, "Cancelled discovery");
        }
        
        if (scanReceiver != null && appContext != null) {
            try {
                appContext.unregisterReceiver(scanReceiver);
                Log.d(TAG, "Unregistered scan receiver");
            } catch (Exception e) {
                Log.e(TAG, "Error unregistering receiver", e);
            }
            scanReceiver = null;
        }
        
        isScanning = false;
        notifyScanFinished();
    }
    
    public static boolean isScanning() {
        return isScanning;
    }
    
    public static String[] btGetPairedDevices() {
        Log.d(TAG, "btGetPairedDevices called");
        final ArrayList<String> devices = new ArrayList<>();
        
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null) {
                Log.e(TAG, "Bluetooth adapter is null");
                return new String[0];
            }
            
            Set<BluetoothDevice> pairedDevices = btAdapter.getBondedDevices();
            Log.d(TAG, "Found " + pairedDevices.size() + " paired devices");
            
            for (BluetoothDevice device : pairedDevices) {
                int deviceType = detectDeviceType(device);
                String deviceName = device.getName();
                if (deviceName == null) deviceName = "Unknown";
                String deviceInfo = device.getAddress() + ";" + deviceName + ";" + deviceType;
                devices.add(deviceInfo);
                Log.d(TAG, "Paired device: " + deviceInfo);
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting paired devices", e);
        }
        
        Log.d(TAG, "Returning " + devices.size() + " devices");
        return devices.toArray(new String[0]);
    }
    
    public static boolean btPair(String address) {
        Log.d(TAG, "btPair called with address: " + address);
        
        if (address == null || address.length() != 17) {
            Log.e(TAG, "Invalid address: " + address);
            return false;
        }
        
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null || !btAdapter.isEnabled()) {
                Log.e(TAG, "Bluetooth adapter not available or not enabled");
                return false;
            }
            
            final BluetoothDevice device = btAdapter.getRemoteDevice(address);
            if (device == null) {
                Log.e(TAG, "Could not get remote device for address: " + address);
                return false;
            }
            
            if (device.getBondState() == BluetoothDevice.BOND_BONDED) {
                Log.d(TAG, "Device already paired");
                String name = device.getName();
                if (name == null) name = "Unknown";
                notifyPairingResult(true, address, name);
                return true;
            }
            
            BroadcastReceiver pairReceiver = new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    String action = intent.getAction();
                    
                    if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action)) {
                        BluetoothDevice dev = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                        if (dev != null && dev.getAddress().equals(address)) {
                            int bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1);
                            int previousState = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, -1);
                            
                            Log.d(TAG, "Bond state changed for " + address + " from " + previousState + " to " + bondState);
                            
                            if (bondState == BluetoothDevice.BOND_BONDED) {
                                String name = dev.getName();
                                if (name == null) name = "Unknown";
                                notifyPairingResult(true, address, name);
                                try {
                                    appContext.unregisterReceiver(this);
                                } catch (Exception e) {
                                    Log.e(TAG, "Error unregistering receiver", e);
                                }
                            } else if (bondState == BluetoothDevice.BOND_NONE && previousState == BluetoothDevice.BOND_BONDING) {
                                notifyPairingResult(false, address, "");
                                try {
                                    appContext.unregisterReceiver(this);
                                } catch (Exception e) {
                                    Log.e(TAG, "Error unregistering receiver", e);
                                }
                            }
                        }
                    }
                }
            };
            
            IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
            appContext.registerReceiver(pairReceiver, filter);
            
            Method method = device.getClass().getMethod("createBond");
            boolean result = (Boolean) method.invoke(device);
            Log.d(TAG, "createBond result: " + result);
            
            return result;
            
        } catch (Exception e) {
            Log.e(TAG, "Error pairing device", e);
            notifyPairingResult(false, address, "");
            return false;
        }
    }
    
    public static boolean btUnpair(String address) {
        Log.d(TAG, "btUnpair called with address: " + address);
        
        if (address == null || address.length() != 17) {
            Log.e(TAG, "Invalid address: " + address);
            return false;
        }
        
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null || !btAdapter.isEnabled()) {
                Log.e(TAG, "Bluetooth adapter not available or not enabled");
                return false;
            }
            
            BluetoothDevice device = btAdapter.getRemoteDevice(address);
            if (device == null) {
                Log.e(TAG, "Could not get remote device for address: " + address);
                return false;
            }
            
            if (device.getBondState() == BluetoothDevice.BOND_NONE) {
                Log.d(TAG, "Device already not paired");
                return true;
            }
            
            Method method = device.getClass().getMethod("removeBond");
            boolean result = (Boolean) method.invoke(device);
            Log.d(TAG, "removeBond result: " + result);
            
            return result;
            
        } catch (Exception e) {
            Log.e(TAG, "Error unpairing device", e);
            return false;
        }
    }
    
    public static int btGetPairState(String address) {
        if (address == null || address.length() != 17) {
            return BluetoothDevice.BOND_NONE;
        }
        
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null || !btAdapter.isEnabled()) {
                return BluetoothDevice.BOND_NONE;
            }
            
            BluetoothDevice device = btAdapter.getRemoteDevice(address);
            if (device == null) {
                return BluetoothDevice.BOND_NONE;
            }
            
            return device.getBondState();
            
        } catch (Exception e) {
            Log.e(TAG, "Error getting pair state", e);
            return BluetoothDevice.BOND_NONE;
        }
    }
    
    private static int detectDeviceType(BluetoothDevice device) {
        if (device == null) {
            return TYPE_UNKNOWN;
        }
        
        String name = device.getName();
        if (name == null) {
            return TYPE_UNKNOWN;
        }
        
        String lowerName = name.toLowerCase();

        try {
            int btClass = device.getBluetoothClass().getDeviceClass();
            
            if (btClass >= 0x400 && btClass < 0x500) {
                if (btClass >= 0x404 && btClass <= 0x408) {
                    return TYPE_HEADSET;
                }
                if (btClass >= 0x414 && btClass <= 0x418) {
                    return TYPE_SPEAKER;
                }
            }
            else if (btClass >= 0x500 && btClass < 0x600) {
                if (btClass == 0x540) {
                    return TYPE_KEYBOARD;
                }
                if (btClass == 0x580) {
                    return TYPE_MOUSE;
                }
            }
        } catch (Exception e) {
            
        }
        
        if (lowerName.contains("medical") || lowerName.contains("health") ||
            lowerName.contains("glucose") || lowerName.contains("pressure")) {
            return TYPE_MEDICAL;
        }
        
        if (lowerName.contains("headphone") || lowerName.contains("headset") || 
            lowerName.contains("earphone") || lowerName.contains("earbud") ||
            lowerName.contains("airpods") || lowerName.contains("buds") ||
            lowerName.contains("inkax") || lowerName.contains("t3")) {
            return TYPE_HEADSET;
        }
        
        if (lowerName.contains("speaker") || lowerName.contains("soundbar") || 
            lowerName.contains("audio") || lowerName.contains("boombox")) {
            return TYPE_SPEAKER;
        }
        
        if (lowerName.contains("car") || lowerName.contains("auto") || 
            lowerName.contains("vehicle") || lowerName.contains("bmw")) {
            return TYPE_CAR;
        }
        
        if (lowerName.contains("watch") || lowerName.contains("fitbit") || 
            lowerName.contains("band") || lowerName.contains("wearable")) {
            return TYPE_WEARABLE;
        }
        
        if (lowerName.contains("printer") || lowerName.contains("laser") || 
            lowerName.contains("inkjet")) {
            return TYPE_PRINTER;
        }
        
        return TYPE_UNKNOWN;
    }
    
    private static boolean isDataDevice(int type) {
        return type == TYPE_ARDUINO || 
               type == TYPE_RASPBERRY || type == TYPE_MEDICAL || type == TYPE_PRINTER;
    }
    
    private static boolean isAudioDevice(int type) {
        return type == TYPE_HEADSET || type == TYPE_SPEAKER || type == TYPE_CAR;
    }
    
    private static boolean isHIDDevice(int type) {
        return type == TYPE_KEYBOARD || type == TYPE_MOUSE;
    }
    
    public static boolean btConnect(String address) {
        Log.d(TAG, "btConnect called with address: " + address);
        return btConnectWithMode(address, connectionMode);
    }
    
    public static boolean btConnectWithMode(String address, int mode) {
        Log.d(TAG, "btConnectWithMode called with address: " + address + ", mode: " + mode);
        if (address == null || address.length() != 17) {
            Log.e(TAG, "Invalid address: " + address);
            return false;
        }
        
        synchronized (lock) {
            btDisconnect();
            
            try {
                btAdapter = BluetoothAdapter.getDefaultAdapter();
                if (btAdapter == null || !btAdapter.isEnabled()) {
                    Log.e(TAG, "Bluetooth adapter not available or not enabled");
                    return false;
                }
                
                final BluetoothDevice device = btAdapter.getRemoteDevice(address);
                if (device == null) {
                    Log.e(TAG, "Could not get remote device for address: " + address);
                    return false;
                }
                
                connectedDeviceName = device.getName();
                if (connectedDeviceName == null) connectedDeviceName = "Unknown";
                connectedDeviceType = detectDeviceType(device);
                connectedDeviceAddress = address;
                connectedDevice = device;
                
                Log.d(TAG, "Device: " + connectedDeviceName + ", type: " + connectedDeviceType);
                
                int actualMode = mode;
                if (actualMode == MODE_AUTO) {
                    if (isDataDevice(connectedDeviceType)) actualMode = MODE_DATA;
                    else if (isAudioDevice(connectedDeviceType)) actualMode = MODE_AUDIO;
                    else if (isHIDDevice(connectedDeviceType)) actualMode = MODE_HID;
                    else actualMode = MODE_DATA;
                    Log.d(TAG, "Auto mode selected: " + actualMode);
                }
                
                currentMode = actualMode;
                
                btAdapter.cancelDiscovery();
                
                switch (actualMode) {
                    case MODE_DATA:
                        Log.d(TAG, "Connecting in DATA mode");
                        return connectDataMode(device);
                    case MODE_AUDIO:
                        Log.d(TAG, "Connecting in AUDIO mode");
                        return connectAudioMode(device);
                    case MODE_HID:
                        Log.d(TAG, "Connecting in HID mode");
                        return connectHIDMode(device);
                    default:
                        Log.e(TAG, "Unknown mode: " + actualMode);
                        return false;
                }
                
            } catch (Exception e) {
                Log.e(TAG, "Error in btConnectWithMode", e);
                return false;
            }
        }
    }
    
    private static boolean connectDataMode(BluetoothDevice device) {
        Log.d(TAG, "connectDataMode for device: " + device.getName());
        isConnecting = true;
        connectThread = new ConnectThread(device);
        connectThread.start();
        
        showToast(null, "Connecting to " + device.getName() + " (Data Mode)...", false);
        
        return true;
    }
    
    private static boolean connectAudioMode(BluetoothDevice device) {
        Log.d(TAG, "connectAudioMode for device: " + device.getName());
        
        showToast(null, "Connecting to " + device.getName() + " (Audio Mode)...", false);
        
        if (isDeviceAudioConnected(device)) {
            Log.d(TAG, "Device already connected via audio profile");
            handleAudioConnectionVerified(device);
            return true;
        }
        
        audioConnectionVerified = false;
        audioConnectionAttempts++;
        pendingAudioConnectionAddress = device.getAddress();
        
        if (device.getBondState() != BluetoothDevice.BOND_BONDED) {
            Log.d(TAG, "Device not paired, attempting to pair first");
            btPair(device.getAddress());
            mainHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    if (!audioConnectionVerified && pendingAudioConnectionAddress != null) {
                        Log.d(TAG, "Retrying audio connection after pairing");
                        connectAudioMode(device);
                    }
                }
            }, 3000);
            return true;
        }
        
        try {
            if (a2dpProfile != null) {
                Method connectMethod = a2dpProfile.getClass().getMethod("connect", BluetoothDevice.class);
                connectMethod.invoke(a2dpProfile, device);
                Log.d(TAG, "Called A2DP connect");
            }
            
            if (headsetProfile != null) {
                Method connectMethod = headsetProfile.getClass().getMethod("connect", BluetoothDevice.class);
                connectMethod.invoke(headsetProfile, device);
                Log.d(TAG, "Called Headset connect");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error connecting audio profiles", e);
        }
        
        if (audioConnectionTimeoutRunnable != null) {
            mainHandler.removeCallbacks(audioConnectionTimeoutRunnable);
        }
        
        audioConnectionTimeoutRunnable = new Runnable() {
            @Override
            public void run() {
                if (!audioConnectionVerified && pendingAudioConnectionAddress != null) {
                    Log.d(TAG, "Audio connection timeout for: " + device.getName());
                    
                    if (audioConnectionAttempts < MAX_AUDIO_CONNECTION_ATTEMPTS) {
                        Log.d(TAG, "Retrying audio connection (attempt " + (audioConnectionAttempts + 1) + "/" + MAX_AUDIO_CONNECTION_ATTEMPTS + ")");
                        connectAudioMode(device);
                    } else {
                        Log.e(TAG, "Max audio connection attempts reached for: " + device.getName());
                        pendingAudioConnectionAddress = null;
                        audioConnectionAttempts = 0;
                        notifyConnectionResult(false, device.getName(), connectedDeviceType, MODE_AUDIO);
                    }
                }
                audioConnectionTimeoutRunnable = null;
            }
        };
        
        mainHandler.postDelayed(audioConnectionTimeoutRunnable, AUDIO_CONNECTION_TIMEOUT_MS);
        
        return true;
    }
    
    private static boolean isDeviceAudioConnected(BluetoothDevice device) {
        try {
            if (a2dpProfile != null) {
                Method getConnectedDevices = a2dpProfile.getClass().getMethod("getConnectedDevices");
                @SuppressWarnings("unchecked")
                List<BluetoothDevice> connectedDevices = (List<BluetoothDevice>) getConnectedDevices.invoke(a2dpProfile);
                if (connectedDevices != null) {
                    for (BluetoothDevice d : connectedDevices) {
                        if (d.getAddress().equals(device.getAddress())) {
                            return true;
                        }
                    }
                }
            }
            
            if (headsetProfile != null) {
                Method getConnectedDevices = headsetProfile.getClass().getMethod("getConnectedDevices");
                @SuppressWarnings("unchecked")
                List<BluetoothDevice> connectedDevices = (List<BluetoothDevice>) getConnectedDevices.invoke(headsetProfile);
                if (connectedDevices != null) {
                    for (BluetoothDevice d : connectedDevices) {
                        if (d.getAddress().equals(device.getAddress())) {
                            return true;
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error checking device audio connection", e);
        }
        
        return false;
    }
    
    private static boolean connectHIDMode(BluetoothDevice device) {
        Log.d(TAG, "connectHIDMode for device: " + device.getName());
        isConnecting = true;
        connectThread = new ConnectThread(device, UUID_HID);
        connectThread.start();
        
        showToast(null, "Connecting to " + device.getName() + " (HID Mode)...", false);
        
        return true;
    }
    
    public static void btDisconnect() {
        Log.d(TAG, "btDisconnect called");
        synchronized (lock) {
            isConnecting = false;
            isConnected = false;
            
            if (pendingAudioConnectionAddress != null) {
                pendingAudioConnectionAddress = null;
                if (audioConnectionTimeoutRunnable != null) {
                    mainHandler.removeCallbacks(audioConnectionTimeoutRunnable);
                    audioConnectionTimeoutRunnable = null;
                }
            }
            
            if (connectThread != null) {
                Log.d(TAG, "Interrupting connect thread");
                connectThread.interrupt();
                connectThread = null;
            }
            
            if (readThread != null) {
                Log.d(TAG, "Interrupting read thread");
                readThread.interrupt();
                readThread = null;
            }
            
            try {
                if (btInputStream != null) {
                    btInputStream.close();
                    Log.d(TAG, "Input stream closed");
                }
                if (btOutputStream != null) {
                    btOutputStream.close();
                    Log.d(TAG, "Output stream closed");
                }
                if (btSocket != null) {
                    btSocket.close();
                    Log.d(TAG, "Socket closed");
                }
            } catch (IOException e) {
                Log.e(TAG, "Error closing streams/socket", e);
            }
            
            btInputStream = null;
            btOutputStream = null;
            btSocket = null;
            
            Log.d(TAG, "Disconnect complete");
        }
    }
    
    public static int btSend(byte[] data) {
        if (currentMode != MODE_DATA || !isConnected || btOutputStream == null) {
            Log.e(TAG, "Cannot send data - mode: " + currentMode + ", connected: " + isConnected + ", outputStream: " + (btOutputStream != null));
            return -1;
        }
        
        try {
            btOutputStream.write(data);
            btOutputStream.flush();
            Log.d(TAG, "Sent " + data.length + " bytes");
            return data.length;
        } catch (IOException e) {
            Log.e(TAG, "Error sending data", e);
            btDisconnect();
            return -1;
        }
    }
    
    public static boolean btIsConnected() {
        boolean connected;
        if (currentMode == MODE_AUDIO) {
            connected = isDeviceAudioConnected(connectedDevice);
        } else {
            connected = isConnected;
        }
        Log.d(TAG, "btIsConnected: " + connected + " (mode: " + currentMode + ")");
        return connected;
    }
    
    public static int btGetDeviceType() {
        Log.d(TAG, "btGetDeviceType: " + connectedDeviceType);
        return connectedDeviceType;
    }
    
    public static String btGetDeviceName() {
        Log.d(TAG, "btGetDeviceName: " + connectedDeviceName);
        return connectedDeviceName;
    }
    
    public static int btGetCurrentMode() {
        Log.d(TAG, "btGetCurrentMode: " + currentMode);
        return currentMode;
    }
    
    public static String btGetModeName() {
        String modeName;
        switch (currentMode) {
            case MODE_DATA: modeName = "Data Mode (SPP)"; break;
            case MODE_AUDIO: modeName = "Audio Mode (A2DP/HFP)"; break;
            case MODE_HID: modeName = "HID Mode"; break;
            default: modeName = "Unknown";
        }
        Log.d(TAG, "btGetModeName: " + modeName);
        return modeName;
    }
    
    private static void notifyConnectionResult(final boolean success, final String deviceName, 
                                               final int deviceType, final int mode) {
        Log.d(TAG, "notifyConnectionResult: success=" + success + ", deviceName=" + deviceName + ", type=" + deviceType + ", mode=" + mode);
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                for (BluetoothCallback callback : callbacks) {
                    try {
                        callback.onConnectionResult(success, deviceName, deviceType, mode);
                    } catch (Exception e) {
                        Log.e(TAG, "Error in callback onConnectionResult", e);
                    }
                }
            }
        });
    }
    
    private static void notifyDataReceived(final byte[] data, final int length) {
        Log.d(TAG, "notifyDataReceived: length=" + length);
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                for (BluetoothCallback callback : callbacks) {
                    try {
                        callback.onDataReceived(data, length);
                    } catch (Exception e) {
                        Log.e(TAG, "Error in callback onDataReceived", e);
                    }
                }
            }
        });
    }
    
    private static void notifyDeviceDiscovered(final String address, final String name, final int type, final int rssi) {
        for (BluetoothCallback callback : callbacks) {
            try {
                callback.onDeviceDiscovered(address, name, type, rssi);
            } catch (Exception e) {
                Log.e(TAG, "Error in callback onDeviceDiscovered", e);
            }
        }
    }
    
    private static void notifyScanFinished() {
        Log.d(TAG, "notifyScanFinished");
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                for (BluetoothCallback callback : callbacks) {
                    try {
                        callback.onScanFinished();
                    } catch (Exception e) {
                        Log.e(TAG, "Error in callback onScanFinished", e);
                    }
                }
            }
        });
    }
    
    private static void notifyPairingResult(final boolean success, final String address, final String name) {
        Log.d(TAG, "notifyPairingResult: success=" + success + ", address=" + address + ", name=" + name);
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                for (BluetoothCallback callback : callbacks) {
                    try {
                        callback.onPairingResult(success, address, name);
                    } catch (Exception e) {
                        Log.e(TAG, "Error in callback onPairingResult", e);
                    }
                }
            }
        });
    }
    
    private static class ConnectThread extends Thread {
        private final BluetoothDevice device;
        private final UUID[] uuids;
        private final AtomicBoolean stopped = new AtomicBoolean(false);
        
        public ConnectThread(BluetoothDevice device) {
            this.device = device;
            this.uuids = new UUID[]{UUID_SPP, UUID_HEADSET, UUID_HANDSFREE, UUID_A2DP_SINK};
            Log.d(TAG, "ConnectThread created with multiple UUIDs for device: " + device.getName());
        }
        
        public ConnectThread(BluetoothDevice device, UUID specificUUID) {
            this.device = device;
            this.uuids = new UUID[]{specificUUID};
            Log.d(TAG, "ConnectThread created with specific UUID: " + specificUUID + " for device: " + device.getName());
        }
        
        @Override
        public void run() {
            Log.d(TAG, "ConnectThread running");
            BluetoothSocket tempSocket = null;
            
            for (UUID uuid : uuids) {
                if (stopped.get()) {
                    Log.d(TAG, "ConnectThread stopped");
                    return;
                }
                
                try {
                    Log.d(TAG, "Trying UUID: " + uuid);
                    tempSocket = device.createRfcommSocketToServiceRecord(uuid);
                    tempSocket.connect();
                    
                    if (tempSocket.isConnected()) {
                        Log.d(TAG, "Connected with UUID: " + uuid);
                        setupDataConnection(tempSocket);
                        return;
                    }
                } catch (IOException e) {
                    Log.d(TAG, "Failed with UUID: " + uuid + ", error: " + e.getMessage());
                    try { if (tempSocket != null) tempSocket.close(); } catch (IOException ignored) {}
                }
            }
            
            int[] channels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 18, 20};
            Log.d(TAG, "Trying fallback RFCOMM channels");
            
            for (int channel : channels) {
                if (stopped.get()) {
                    Log.d(TAG, "ConnectThread stopped");
                    return;
                }
                
                try {
                    Log.d(TAG, "Trying RFCOMM channel: " + channel);
                    Method method = device.getClass().getMethod("createRfcommSocket", int.class);
                    tempSocket = (BluetoothSocket) method.invoke(device, channel);
                    tempSocket.connect();
                    
                    if (tempSocket.isConnected()) {
                        Log.d(TAG, "Connected with RFCOMM channel: " + channel);
                        setupDataConnection(tempSocket);
                        return;
                    }
                } catch (Exception e) {
                    Log.d(TAG, "Failed with RFCOMM channel: " + channel + ", error: " + e.getMessage());
                    try { if (tempSocket != null) tempSocket.close(); } catch (IOException ignored) {}
                }
            }
            
            Log.e(TAG, "All connection attempts failed for device: " + device.getName());
            isConnecting = false;
            notifyConnectionResult(false, device.getName(), connectedDeviceType, MODE_DATA);
        }
        
        private void setupDataConnection(BluetoothSocket socket) {
            try {
                Log.d(TAG, "Setting up data connection");
                btSocket = socket;
                btInputStream = btSocket.getInputStream();
                btOutputStream = btSocket.getOutputStream();
                
                isConnected = true;
                isConnecting = false;
                currentMode = MODE_DATA;
                
                Log.d(TAG, "Data connection established, notifying");
                notifyConnectionResult(true, device.getName(), connectedDeviceType, MODE_DATA);
                
                readThread = new ReadThread();
                readThread.start();
                Log.d(TAG, "Read thread started");
                
            } catch (IOException e) {
                Log.e(TAG, "Error setting up data connection", e);
                isConnecting = false;
                notifyConnectionResult(false, device.getName(), connectedDeviceType, MODE_DATA);
            }
        }
        
        public void interrupt() {
            Log.d(TAG, "ConnectThread interrupted");
            stopped.set(true);
            super.interrupt();
        }
    }
    
    private static class ReadThread extends Thread {
        private final AtomicBoolean running = new AtomicBoolean(true);
        
        public ReadThread() {
            Log.d(TAG, "ReadThread created");
        }
        
        @Override
        public void run() {
            Log.d(TAG, "ReadThread running");
            byte[] buffer = new byte[1024];
            int bytes;
            
            while (running.get() && isConnected && btInputStream != null) {
                try {
                    if (btInputStream.available() > 0) {
                        bytes = btInputStream.read(buffer);
                        if (bytes > 0) {
                            Log.d(TAG, "Read " + bytes + " bytes");
                            final byte[] data = new byte[bytes];
                            System.arraycopy(buffer, 0, data, 0, bytes);
                            notifyDataReceived(data, bytes);
                        }
                    } else {
                        Thread.sleep(10);
                    }
                } catch (IOException e) {
                    Log.e(TAG, "Error reading from input stream", e);
                    break;
                } catch (InterruptedException e) {
                    Log.d(TAG, "Read thread interrupted", e);
                    break;
                }
            }
            
            Log.d(TAG, "Read thread exiting");
        }
        
        public void interrupt() {
            Log.d(TAG, "ReadThread interrupted");
            running.set(false);
            super.interrupt();
        }
    }
    
    public static void cleanup() {
        Log.d(TAG, "cleanup called");
        stopScan();
        btDisconnect();
        
        if (audioConnectionTimeoutRunnable != null) {
            mainHandler.removeCallbacks(audioConnectionTimeoutRunnable);
            audioConnectionTimeoutRunnable = null;
        }
        
        try {
            if (appContext != null) {
                appContext.unregisterReceiver(audioConnectionReceiver);
            }
        } catch (Exception e) {
            Log.e(TAG, "Error unregistering audio receiver", e);
        }
        
        synchronized (AromaHelper.class) {
            if (toastRunnable != null) {
                mainHandler.removeCallbacks(toastRunnable);
                toastRunnable = null;
            }
        }
        
        if (btAdapter != null && appContext != null) {
            if (a2dpProfile != null) {
                btAdapter.closeProfileProxy(BluetoothProfile.A2DP, a2dpProfile);
                a2dpProfile = null;
                Log.d(TAG, "A2DP profile proxy closed");
            }
            if (headsetProfile != null) {
                btAdapter.closeProfileProxy(BluetoothProfile.HEADSET, headsetProfile);
                headsetProfile = null;
                Log.d(TAG, "Headset profile proxy closed");
            }
        }
        
        if (backgroundExecutor != null) {
            backgroundExecutor.shutdown();
            try {
                if (!backgroundExecutor.awaitTermination(800, TimeUnit.MILLISECONDS)) {
                    backgroundExecutor.shutdownNow();
                }
            } catch (InterruptedException e) {
                backgroundExecutor.shutdownNow();
            }
        }
        
        callbacks.clear();
        Log.d(TAG, "Cleanup complete");
    }

    /* Prefs */
    public static void setPref(String key, String value) {
        if (appContext == null) return;
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        prefs.edit().putString(key, value).apply();
    }

    public static String getPref(String key, String default_value) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        return prefs.getString(key, default_value);
    }

    public static void setPrefBoolean(String key, boolean value) {
        if (appContext == null) return;
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        prefs.edit().putBoolean(key, value).apply();
    }

    public static boolean getPrefBoolean(String key, boolean default_value) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        return prefs.getBoolean(key, default_value);
    }

    public static void setPrefLong(String key, long value) {
        if (appContext == null) return;
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        prefs.edit().putLong(key, value).apply();
    }

    public static long getPrefLong(String key, long default_value) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        return prefs.getLong(key, default_value);
    }

    public static void setPrefInt(String key, int value) {
        if (appContext == null) return;
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        prefs.edit().putInt(key, value).apply();
    }

    public static int getPrefInt(String key, int default_value) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        return prefs.getInt(key, default_value);
    }

    public static void setPrefFloat(String key, float value) {
        if (appContext == null) return;
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        prefs.edit().putFloat(key, value).apply();
    }

    public static float getPrefFloat(String key, float default_value) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        return prefs.getFloat(key, default_value);
    }

    private static BluetoothLeScanner bleScanner = null;
    private static ScanCallback bleScanCallback = null;
    private static boolean bleScanning = false;
    private static Runnable bleScanTimeout = null;
    private static BluetoothGatt bleGatt = null;
    private static String bleAddress = "";
    private static boolean bleConnected = false;
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB");

    private static List<SensorCallback> sensorCallbacks = new CopyOnWriteArrayList<>();
    private static SensorManager sensorManager = null;
    private static Map<Integer, Sensor> activeSensors = new HashMap<>();
    private static SensorEventListener sensorListener = null;

    private static TextToSpeech ttsEngine = null;
    private static PowerManager.WakeLock wakeLock = null;

    public static void addSensorCallback(SensorCallback callback) {
        if (callback != null && !sensorCallbacks.contains(callback)) {
            sensorCallbacks.add(callback);
        }
    }

    public static void removeSensorCallback(SensorCallback callback) {
        sensorCallbacks.remove(callback);
    }

    public static void btleStartScan(String uuidCsv, int timeoutMs) {
        if (btAdapter == null || appContext == null) return;
        try {
            if (Build.VERSION.SDK_INT < 21) return;
            btleStopScan();
            bleScanner = btAdapter.getBluetoothLeScanner();
            if (bleScanner == null) return;
            List<ScanFilter> filters = null;
            if (uuidCsv != null && uuidCsv.length() > 0) {
                filters = new ArrayList<>();
                for (String part : uuidCsv.split(",")) {
                    String id = part.trim();
                    if (id.length() == 0) continue;
                    try {
                        filters.add(new ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(id)).build());
                    } catch (IllegalArgumentException e) {
                    }
                }
                if (filters.isEmpty()) filters = null;
            }
            ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            bleScanCallback = new ScanCallback() {
                @Override public void onScanResult(int callbackType, ScanResult result) {
                    BluetoothDevice dev = result.getDevice();
                    if (dev == null) return;
                    String addr = dev.getAddress();
                    String name = "";
                    try { if (dev.getName() != null) name = dev.getName(); } catch (SecurityException e) { }
                    String faddr = addr != null ? addr : "";
                    for (BluetoothCallback cb : callbacks) {
                        cb.onBleDevice(faddr, name, result.getRssi());
                    }
                }
                @Override public void onBatchScanResults(List<ScanResult> results) {
                    if (results == null) return;
                    for (ScanResult r : results) onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES, r);
                }
                @Override public void onScanFailed(int errorCode) {
                    btleStopScan();
                    for (BluetoothCallback cb : callbacks) {
                        cb.onBleScanFinished();
                    }
                }
            };
            bleScanning = true;
            try {
                bleScanner.startScan(filters, settings, bleScanCallback);
            } catch (SecurityException e) {
                bleScanning = false;
                return;
            }
            if (timeoutMs > 0 && mainHandler != null) {
                if (bleScanTimeout != null) mainHandler.removeCallbacks(bleScanTimeout);
                bleScanTimeout = new Runnable() {
                    @Override public void run() {
                        btleStopScan();
                        for (BluetoothCallback cb : callbacks) {
                            cb.onBleScanFinished();
                        }
                    }
                };
                mainHandler.postDelayed(bleScanTimeout, timeoutMs);
            }
        } catch (Exception e) {
            bleScanning = false;
        }
    }

    public static void btleStopScan() {
        try {
            if (bleScanCallback != null && bleScanner != null) {
                try { bleScanner.stopScan(bleScanCallback); } catch (SecurityException e) { }
            }
        } catch (Exception e) {
        }
        bleScanCallback = null;
        bleScanning = false;
        if (mainHandler != null && bleScanTimeout != null) {
            mainHandler.removeCallbacks(bleScanTimeout);
            bleScanTimeout = null;
        }
    }

    public static void btleConnect(String address) {
        if (btAdapter == null || appContext == null || address == null) return;
        try {
            if (Build.VERSION.SDK_INT < 21) {
                for (BluetoothCallback cb : callbacks) {
                    cb.onBleConnection(address, -1, false);
                }
                return;
            }
            btleDisconnect();
            BluetoothDevice dev;
            try {
                dev = btAdapter.getRemoteDevice(address);
            } catch (IllegalArgumentException e) {
                for (BluetoothCallback cb : callbacks) {
                    cb.onBleConnection(address, -1, false);
                }
                return;
            }
            bleAddress = address;
            try {
                bleGatt = dev.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
            } catch (SecurityException e) {
                for (BluetoothCallback cb : callbacks) {
                    cb.onBleConnection(address, -1, false);
                }
            }
            if (bleGatt == null) {
                for (BluetoothCallback cb : callbacks) {
                    cb.onBleConnection(address, -1, false);
                }
            }
        } catch (Exception e) {
            for (BluetoothCallback cb : callbacks) {
                try { cb.onBleConnection(address != null ? address : "", -1, false); } catch (Exception ignored) { }
            }
        }
    }

    public static void btleDisconnect() {
        try {
            if (bleGatt != null) {
                try { bleGatt.disconnect(); } catch (SecurityException e) { }
                try { bleGatt.close(); } catch (Exception e) { }
            }
        } catch (Exception e) {
        }
        bleGatt = null;
        bleConnected = false;
        bleAddress = "";
    }

    public static boolean btleIsConnected() {
        return bleConnected && bleGatt != null;
    }

    public static boolean btleDiscover() {
        if (bleGatt == null) return false;
        try {
            return bleGatt.discoverServices();
        } catch (SecurityException e) {
            return false;
        }
    }

    public static boolean btleRead(String serviceUuid, String charUuid) {
        if (bleGatt == null) return false;
        try {
            BluetoothGattService svc = bleGatt.getService(UUID.fromString(serviceUuid));
            if (svc == null) return false;
            BluetoothGattCharacteristic ch = svc.getCharacteristic(UUID.fromString(charUuid));
            if (ch == null) return false;
            return bleGatt.readCharacteristic(ch);
        } catch (IllegalArgumentException e) {
            return false;
        } catch (SecurityException e) {
            return false;
        }
    }

    public static boolean btleWrite(String serviceUuid, String charUuid, byte[] data, int writeType) {
        if (bleGatt == null || data == null) return false;
        try {
            BluetoothGattService svc = bleGatt.getService(UUID.fromString(serviceUuid));
            if (svc == null) return false;
            BluetoothGattCharacteristic ch = svc.getCharacteristic(UUID.fromString(charUuid));
            if (ch == null) return false;
            ch.setValue(data);
            ch.setWriteType(writeType);
            return bleGatt.writeCharacteristic(ch);
        } catch (IllegalArgumentException e) {
            return false;
        } catch (SecurityException e) {
            return false;
        }
    }

    public static boolean btleNotify(String serviceUuid, String charUuid, boolean enable) {
        if (bleGatt == null) return false;
        try {
            BluetoothGattService svc = bleGatt.getService(UUID.fromString(serviceUuid));
            if (svc == null) return false;
            BluetoothGattCharacteristic ch = svc.getCharacteristic(UUID.fromString(charUuid));
            if (ch == null) return false;
            if (!bleGatt.setCharacteristicNotification(ch, enable)) return false;
            BluetoothGattDescriptor desc = ch.getDescriptor(CCCD_UUID);
            if (desc == null) return enable == false;
            desc.setValue(enable ? BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE : BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
            return bleGatt.writeDescriptor(desc);
        } catch (IllegalArgumentException e) {
            return false;
        } catch (SecurityException e) {
            return false;
        }
    }

    private static final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            boolean connected = (newState == BluetoothProfile.STATE_CONNECTED);
            bleConnected = connected;
            if (!connected) {
                try { gatt.close(); } catch (Exception e) { }
                if (bleGatt == gatt) {
                    bleGatt = null;
                    bleAddress = "";
                }
            }
            String addr = bleAddress;
            try {
                if (gatt.getDevice() != null && gatt.getDevice().getAddress() != null) addr = gatt.getDevice().getAddress();
            } catch (SecurityException e) { }
            for (BluetoothCallback cb : callbacks) {
                cb.onBleConnection(addr != null ? addr : "", status, connected);
            }
        }
        @Override public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) return;
            StringBuilder sb = new StringBuilder();
            try {
                for (BluetoothGattService s : gatt.getServices()) {
                    if (sb.length() > 0) sb.append(",");
                    sb.append(s.getUuid().toString());
                }
            } catch (SecurityException e) { }
            String addr = bleAddress;
            for (BluetoothCallback cb : callbacks) {
                cb.onBleServices(addr, sb.toString());
            }
        }
        @Override public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic ch, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) return;
            byte[] v = ch.getValue();
            if (v == null) v = new byte[0];
            String addr = bleAddress;
            for (BluetoothCallback cb : callbacks) {
                cb.onBleData(addr, ch.getUuid().toString(), v, v.length);
            }
        }
        @Override public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic ch) {
            byte[] v = ch.getValue();
            if (v == null) v = new byte[0];
            String addr = bleAddress;
            for (BluetoothCallback cb : callbacks) {
                cb.onBleData(addr, ch.getUuid().toString(), v, v.length);
            }
        }
        @Override public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic ch, int status) {
            String addr = bleAddress;
            for (BluetoothCallback cb : callbacks) {
                cb.onBleWrite(addr, ch.getUuid().toString(), status);
            }
        }
    };

    public static void notifyChannel(String id, String name, String desc, int importance) {
        if (appContext == null || id == null) return;
        try {
            if (Build.VERSION.SDK_INT < 26) return;
            NotificationManager mgr = (NotificationManager) appContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (mgr == null) return;
            NotificationChannel ch = new NotificationChannel(id, name != null ? name : id, importance);
            if (desc != null) ch.setDescription(desc);
            mgr.createNotificationChannel(ch);
        } catch (Exception e) {
        }
    }

    public static void notifyShow(int nid, String channel, String title, String text) {
        if (appContext == null) return;
        try {
            NotificationManager mgr = (NotificationManager) appContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (mgr == null) return;
            android.app.Notification.Builder b;
            if (Build.VERSION.SDK_INT >= 26) {
                b = new android.app.Notification.Builder(appContext, channel);
            } else {
                b = new android.app.Notification.Builder(appContext);
                b.setPriority(0);
            }
            b.setContentTitle(title != null ? title : "");
            b.setContentText(text != null ? text : "");
            b.setSmallIcon(android.R.drawable.ic_dialog_info);
            b.setAutoCancel(true);
            mgr.notify(nid, b.build());
        } catch (Exception e) {
        }
    }

    public static void notifyCancel(int nid) {
        if (appContext == null) return;
        try {
            NotificationManager mgr = (NotificationManager) appContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (mgr != null) mgr.cancel(nid);
        } catch (Exception e) {
        }
    }

    public static void notifyCancelAll() {
        if (appContext == null) return;
        try {
            NotificationManager mgr = (NotificationManager) appContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (mgr != null) mgr.cancelAll();
        } catch (Exception e) {
        }
    }

    public static boolean notifyEnabled() {
        if (appContext == null) return false;
        try {
            NotificationManager mgr = (NotificationManager) appContext.getSystemService(Context.NOTIFICATION_SERVICE);
            if (mgr == null) return false;
            return mgr.areNotificationsEnabled();
        } catch (Exception e) {
            return false;
        }
    }

    public static void vibrateEffect(long ms, int amplitude) {
        if (appContext == null) return;
        try {
            Vibrator vib = (Vibrator) appContext.getSystemService(Context.VIBRATOR_SERVICE);
            if (vib == null) return;
            int amp = amplitude < 1 ? 1 : (amplitude > 255 ? 255 : amplitude);
            if (Build.VERSION.SDK_INT >= 26) {
                vib.vibrate(VibrationEffect.createOneShot(ms, amp));
            } else {
                vib.vibrate(ms);
            }
        } catch (Exception e) {
        }
    }

    public static void clipSet(String text) {
        if (appContext == null) return;
        try {
            ClipboardManager cm = (ClipboardManager) appContext.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null) return;
            cm.setPrimaryClip(ClipData.newPlainText("aroma", text != null ? text : ""));
        } catch (Exception e) {
        }
    }

    public static String clipGet() {
        if (appContext == null) return "";
        try {
            ClipboardManager cm = (ClipboardManager) appContext.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null || !cm.hasPrimaryClip()) return "";
            android.content.ClipDescription desc = cm.getPrimaryClipDescription();
            if (desc == null || !desc.hasMimeType(android.content.ClipDescription.MIMETYPE_TEXT_PLAIN)) {
                if (cm.getPrimaryClip().getItemCount() == 0) return "";
            }
            CharSequence cs = cm.getPrimaryClip().getItemAt(0).coerceToText(appContext);
            return cs != null ? cs.toString() : "";
        } catch (Exception e) {
            return "";
        }
    }

    public static void shareText(Activity activity, String text, String title) {
        if (activity == null) return;
        try {
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_TEXT, text != null ? text : "");
            if (title != null) send.putExtra(Intent.EXTRA_SUBJECT, title);
            Intent chooser = Intent.createChooser(send, title != null ? title : "Share");
            activity.startActivity(chooser);
        } catch (Exception e) {
        }
    }

    public static void uiImmersive(Activity activity, boolean on) {
        if (activity == null) return;
        try {
            Window window = activity.getWindow();
            if (window == null) return;
            if (Build.VERSION.SDK_INT >= 30) {
                android.view.WindowInsetsController ctl = window.getInsetsController();
                if (ctl == null) return;
                if (on) {
                    ctl.hide(android.view.WindowInsets.Type.statusBars() | android.view.WindowInsets.Type.navigationBars());
                } else {
                    ctl.show(android.view.WindowInsets.Type.statusBars() | android.view.WindowInsets.Type.navigationBars());
                }
            } else {
                View decor = window.getDecorView();
                if (decor == null) return;
                if (on) {
                    decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
                } else {
                    decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_VISIBLE);
                }
            }
        } catch (Exception e) {
        }
    }

    public static void uiKeepScreenOn(Activity activity, boolean on) {
        if (activity == null) return;
        try {
            Window window = activity.getWindow();
            if (window == null) return;
            if (on) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            } else {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        } catch (Exception e) {
        }
    }

    public static void orientLock(Activity activity) {
        if (activity == null) return;
        try {
            int o = activity.getResources().getConfiguration().orientation;
            activity.setRequestedOrientation(o == Configuration.ORIENTATION_LANDSCAPE ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        } catch (Exception e) {
        }
    }

    public static void orientSet(Activity activity, int mode) {
        if (activity == null) return;
        try {
            if (mode == 0) {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
            } else if (mode == 1) {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
            } else {
                activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
            }
        } catch (Exception e) {
        }
    }

    public static void orientUnlock(Activity activity) {
        if (activity == null) return;
        try {
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        } catch (Exception e) {
        }
    }

    public static boolean orientLocked(Activity activity) {
        if (activity == null) return false;
        try {
            return activity.getRequestedOrientation() != ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
        } catch (Exception e) {
            return false;
        }
    }

    public static void pickImage(Activity activity, int requestCode) {
        if (activity == null) return;
        try {
            Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("image/*");
            activity.startActivityForResult(intent, requestCode);
        } catch (Exception e) {
        }
    }

    private static Uri captureOutputUri = null;

    /** Take and clear the pending capture output URI (one-shot). */
    public static Uri takeCaptureOutputUri() {
        Uri u = captureOutputUri;
        captureOutputUri = null;
        return u;
    }

    public static void capturePhoto(Activity activity, int requestCode) {
        if (activity == null) return;
        try {
            // Offer a MediaStore output file: many camera apps return
            // RESULT_OK with null data when no EXTRA_OUTPUT is supplied,
            // which previously surfaced as "Cancelled" with no preview.
            Uri outUri = null;
            try {
                ContentValues cv = new ContentValues();
                cv.put(MediaStore.Images.Media.DISPLAY_NAME, "aroma_capture_" + System.currentTimeMillis() + ".jpg");
                cv.put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg");
                if (Build.VERSION.SDK_INT >= 29) {
                    cv.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/AromaDemo");
                    cv.put(MediaStore.Images.Media.IS_PENDING, 1);
                }
                outUri = activity.getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cv);
            } catch (Exception e) {
                outUri = null;
            }
            captureOutputUri = outUri;
            Intent intent = new Intent("android.media.action.IMAGE_CAPTURE");
            if (outUri != null) {
                intent.putExtra(MediaStore.EXTRA_OUTPUT, outUri);
                intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            }
            activity.startActivityForResult(intent, requestCode);
        } catch (Exception e) {
            captureOutputUri = null;
        }
    }

    public static void openDocument(Activity activity, String mime, int requestCode) {
        if (activity == null) return;
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(mime != null ? mime : "*/*");
            activity.startActivityForResult(intent, requestCode);
        } catch (Exception e) {
        }
    }

    public static String copyUriToCache(Activity activity, Uri uri, String prefix) {
        if (activity == null || uri == null) return null;
        java.io.InputStream in = null;
        java.io.OutputStream out = null;
        try {
            in = activity.getContentResolver().openInputStream(uri);
            if (in == null) return null;
            File dst = new File(activity.getCacheDir(), (prefix != null ? prefix : "aroma_") + System.currentTimeMillis() + ".dat");
            out = new FileOutputStream(dst);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            out.flush();
            return dst.getAbsolutePath();
        } catch (Exception e) {
            return null;
        } finally {
            try { if (in != null) in.close(); } catch (Exception e) { }
            try { if (out != null) out.close(); } catch (Exception e) { }
        }
    }

    public static String saveBitmapToCache(Activity activity, Bitmap bmp, String prefix) {
        if (activity == null || bmp == null) return null;
        java.io.OutputStream out = null;
        try {
            File dst = new File(activity.getCacheDir(), (prefix != null ? prefix : "aroma_") + System.currentTimeMillis() + ".png");
            out = new FileOutputStream(dst);
            bmp.compress(Bitmap.CompressFormat.PNG, 100, out);
            out.flush();
            return dst.getAbsolutePath();
        } catch (Exception e) {
            return null;
        } finally {
            try { if (out != null) out.close(); } catch (Exception e) { }
        }
    }

    public static boolean sensorAvailable(int type) {
        if (appContext == null) return false;
        try {
            SensorManager sm = (SensorManager) appContext.getSystemService(Context.SENSOR_SERVICE);
            return sm != null && sm.getDefaultSensor(type) != null;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean sensorStart(int type, int rateUs) {
        if (appContext == null) return false;
        try {
            if (sensorManager == null) {
                sensorManager = (SensorManager) appContext.getSystemService(Context.SENSOR_SERVICE);
            }
            if (sensorManager == null) return false;
            Sensor sensor = sensorManager.getDefaultSensor(type);
            if (sensor == null) return false;
            if (sensorListener == null) {
                sensorListener = new SensorEventListener() {
                    @Override public void onSensorChanged(SensorEvent event) {
                        if (event == null || event.sensor == null) return;
                        float x = event.values.length > 0 ? event.values[0] : 0;
                        float y = event.values.length > 1 ? event.values[1] : 0;
                        float z = event.values.length > 2 ? event.values[2] : 0;
                        for (SensorCallback cb : sensorCallbacks) {
                            cb.onSensorChanged(event.sensor.getType(), x, y, z, event.timestamp);
                        }
                    }
                    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {
                    }
                };
            }
            boolean ok = sensorManager.registerListener(sensorListener, sensor, rateUs > 0 ? rateUs : 20000);
            if (ok) activeSensors.put(type, sensor);
            return ok;
        } catch (Exception e) {
            return false;
        }
    }

    public static void sensorStop(int type) {
        try {
            if (sensorManager == null || sensorListener == null) return;
            Sensor s = activeSensors.remove(type);
            if (s != null) {
                try { sensorManager.unregisterListener(sensorListener, s); } catch (Exception e) { }
            }
            if (activeSensors.isEmpty()) {
                try { sensorManager.unregisterListener(sensorListener); } catch (Exception e) { }
            }
        } catch (Exception e) {
        }
    }

    public static void ttsSpeak(String text) {
        if (appContext == null || text == null) return;
        try {
            if (ttsEngine == null) {
                ttsEngine = new TextToSpeech(appContext, new TextToSpeech.OnInitListener() {
                    @Override public void onInit(int status) {
                        if (status == TextToSpeech.SUCCESS && ttsEngine != null) {
                            try { ttsEngine.setLanguage(Locale.getDefault()); } catch (Exception e) { }
                        }
                    }
                });
            }
            ttsEngine.speak(text, TextToSpeech.QUEUE_FLUSH, null, "aroma-tts-1");
        } catch (Exception e) {
        }
    }

    public static void ttsStop() {
        try {
            if (ttsEngine != null) ttsEngine.stop();
        } catch (Exception e) {
        }
    }

    public static boolean ttsIsSpeaking() {
        try {
            return ttsEngine != null && ttsEngine.isSpeaking();
        } catch (Exception e) {
            return false;
        }
    }

    public static String devManufacturer() {
        try { return Build.MANUFACTURER; } catch (Exception e) { return ""; }
    }

    public static String devModel() {
        try { return Build.MODEL; } catch (Exception e) { return ""; }
    }

    public static String devOsVersion() {
        try { return Build.VERSION.RELEASE; } catch (Exception e) { return ""; }
    }

    public static int devSdkInt() {
        try { return Build.VERSION.SDK_INT; } catch (Exception e) { return 0; }
    }

    public static String appPackage() {
        try { return appContext != null ? appContext.getPackageName() : ""; } catch (Exception e) { return ""; }
    }

    public static String appVersionName() {
        if (appContext == null) return "";
        try {
            PackageManager pm = appContext.getPackageManager();
            PackageInfo info = pm.getPackageInfo(appContext.getPackageName(), 0);
            return info.versionName != null ? info.versionName : "";
        } catch (Exception e) {
            return "";
        }
    }

    public static long appVersionCode() {
        if (appContext == null) return 0;
        try {
            PackageManager pm = appContext.getPackageManager();
            PackageInfo info = pm.getPackageInfo(appContext.getPackageName(), 0);
            if (Build.VERSION.SDK_INT >= 28) return info.getLongVersionCode();
            return info.versionCode;
        } catch (Exception e) {
            return 0;
        }
    }

    public static long appInstallTime() {
        if (appContext == null) return 0;
        try {
            PackageManager pm = appContext.getPackageManager();
            PackageInfo info = pm.getPackageInfo(appContext.getPackageName(), 0);
            return info.firstInstallTime;
        } catch (Exception e) {
            return 0;
        }
    }

    public static long memAvailMb() {
        if (appContext == null) return 0;
        try {
            ActivityManager am = (ActivityManager) appContext.getSystemService(Context.ACTIVITY_SERVICE);
            if (am == null) return 0;
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            return mi.availMem / 1048576L;
        } catch (Exception e) {
            return 0;
        }
    }

    public static boolean memLow() {
        if (appContext == null) return false;
        try {
            ActivityManager am = (ActivityManager) appContext.getSystemService(Context.ACTIVITY_SERVICE);
            if (am == null) return false;
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            return mi.lowMemory;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean netConnected() {
        if (appContext == null) return false;
        try {
            ConnectivityManager cm = (ConnectivityManager) appContext.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return false;
            if (Build.VERSION.SDK_INT >= 23) {
                Network net = cm.getActiveNetwork();
                if (net == null) return false;
                NetworkCapabilities caps = cm.getNetworkCapabilities(net);
                return caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
            }
            NetworkInfo info = cm.getActiveNetworkInfo();
            return info != null && info.isConnected();
        } catch (Exception e) {
            return false;
        }
    }

    public static int netType() {
        if (appContext == null) return 0;
        try {
            ConnectivityManager cm = (ConnectivityManager) appContext.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return 0;
            if (Build.VERSION.SDK_INT >= 23) {
                Network net = cm.getActiveNetwork();
                if (net == null) return 0;
                NetworkCapabilities caps = cm.getNetworkCapabilities(net);
                if (caps == null) return 0;
                if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return 1;
                if (caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) return 2;
                return 3;
            }
            NetworkInfo info = cm.getActiveNetworkInfo();
            if (info == null || !info.isConnected()) return 0;
            int t = info.getType();
            if (t == ConnectivityManager.TYPE_WIFI) return 1;
            if (t == ConnectivityManager.TYPE_MOBILE) return 2;
            return 3;
        } catch (Exception e) {
            return 0;
        }
    }

    public static boolean battCharging() {
        if (appContext == null) return false;
        try {
            android.os.BatteryManager bm = (android.os.BatteryManager) appContext.getSystemService(Context.BATTERY_SERVICE);
            return bm != null && bm.isCharging();
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean devInteractive() {
        if (appContext == null) return false;
        try {
            PowerManager pm = (PowerManager) appContext.getSystemService(Context.POWER_SERVICE);
            return pm != null && pm.isInteractive();
        } catch (Exception e) {
            return false;
        }
    }

    public static String localeTag() {
        try { return Locale.getDefault().toLanguageTag(); } catch (Exception e) { return ""; }
    }

    public static String timezoneId() {
        try { return TimeZone.getDefault().getID(); } catch (Exception e) { return ""; }
    }

    public static String netOperator() {
        if (appContext == null) return "";
        try {
            TelephonyManager tm = (TelephonyManager) appContext.getSystemService(Context.TELEPHONY_SERVICE);
            if (tm == null) return "";
            String op = tm.getNetworkOperatorName();
            return op != null ? op : "";
        } catch (Exception e) {
            return "";
        }
    }

    public static void wakeAcquire(long timeoutMs) {
        if (appContext == null) return;
        try {
            PowerManager pm = (PowerManager) appContext.getSystemService(Context.POWER_SERVICE);
            if (pm == null) return;
            if (wakeLock == null) {
                wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AromaUI:lock");
                wakeLock.setReferenceCounted(false);
            }
            if (!wakeLock.isHeld()) {
                if (timeoutMs > 0) wakeLock.acquire(timeoutMs);
                else wakeLock.acquire();
            }
        } catch (Exception e) {
        }
    }

    public static void wakeRelease() {
        try {
            if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        } catch (Exception e) {
        }
        wakeLock = null;
    }

    public static boolean torchSet(boolean on) {
        if (appContext == null) return false;
        try {
            if (Build.VERSION.SDK_INT < 23) return false;
            CameraManager cm = (CameraManager) appContext.getSystemService(Context.CAMERA_SERVICE);
            if (cm == null) return false;
            String[] ids = cm.getCameraIdList();
            if (ids == null || ids.length == 0) return false;
            cm.setTorchMode(ids[0], on);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private static NfcAdapter nfcAdapter = null;
    private static boolean nfcArmed = false;

    public static boolean nfcAvailable() {
        try {
            NfcAdapter a = NfcAdapter.getDefaultAdapter(appContext);
            return a != null;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean nfcEnabled() {
        try {
            NfcAdapter a = NfcAdapter.getDefaultAdapter(appContext);
            return a != null && a.isEnabled();
        } catch (Exception e) {
            return false;
        }
    }

    public static void nfcStart(Activity activity) {
        if (activity == null) return;
        try {
            NfcAdapter a = NfcAdapter.getDefaultAdapter(activity);
            if (a == null) return;
            Intent intent = new Intent(activity, activity.getClass()).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
            PendingIntent pi = PendingIntent.getActivity(activity, 0, intent, flags);
            IntentFilter ndef = new IntentFilter(NfcAdapter.ACTION_NDEF_DISCOVERED);
            try { ndef.addDataType("*/*"); } catch (IntentFilter.MalformedMimeTypeException e) { }
            // Catch-all: text-record, blank and non-NDEF tags arrive as
            // TECH_DISCOVERED/TAG_DISCOVERED and would otherwise bypass
            // foreground dispatch entirely.
            IntentFilter tech = new IntentFilter(NfcAdapter.ACTION_TECH_DISCOVERED);
            IntentFilter tag = new IntentFilter(NfcAdapter.ACTION_TAG_DISCOVERED);
            a.enableForegroundDispatch(activity, pi, new IntentFilter[]{ndef, tech, tag}, null);
            nfcAdapter = a;
            nfcArmed = true;
        } catch (Exception e) {
        }
    }

    public static void nfcStop(Activity activity) {
        try {
            NfcAdapter a = nfcAdapter != null ? nfcAdapter : NfcAdapter.getDefaultAdapter(activity);
            if (a != null && activity != null) {
                try { a.disableForegroundDispatch(activity); } catch (Exception e) { }
            }
        } catch (Exception e) {
        }
        nfcArmed = false;
        nfcAdapter = null;
    }

    /** Tag UID bytes as uppercase hex ("" when unavailable). */
    public static String rfidUidHex(Tag tag) {
        if (tag == null) return "";
        try {
            byte[] id = tag.getId();
            if (id == null || id.length == 0) return "";
            StringBuilder sb = new StringBuilder(id.length * 2);
            for (byte b : id) sb.append(String.format("%02X", b));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    public static void dispatchRfid(String uid) {
        boolean delivered = false;
        for (BluetoothCallback cb : callbacks) {
            if (cb instanceof NativeCallback) {
                ((NativeCallback) cb).onRfidTag(uid != null ? uid : "");
                delivered = true;
            }
        }
        if (!delivered) {
            // Deliver directly when nothing registered yet, same as NFC.
            try {
                new NativeCallback().onRfidTag(uid != null ? uid : "");
            } catch (Throwable t) {
            }
        }
    }

    public static void dispatchNfc(String payload) {
        boolean delivered = false;
        for (BluetoothCallback cb : callbacks) {
            if (cb instanceof NativeCallback) {
                ((NativeCallback) cb).onNfcTag(payload != null ? payload : "");
                delivered = true;
            }
        }
        if (!delivered) {
            // No callback registered yet (e.g. NFC demo opened before any
            // Bluetooth flow): deliver directly so taps are never dropped.
            try {
                new NativeCallback().onNfcTag(payload != null ? payload : "");
            } catch (Throwable t) {
            }
        }
    }

    public static void contactsPick(Activity activity, int requestCode) {
        if (activity == null) return;
        try {
            Intent intent = new Intent(Intent.ACTION_PICK, ContactsContract.Contacts.CONTENT_URI);
            activity.startActivityForResult(intent, requestCode);
        } catch (Exception e) {
        }
    }

    public static String nfcReadText(Tag tag) {
        if (tag == null) return "";
        try {
            NdefMessage msg = readNdef(tag);
            if (msg == null) return "";
            for (NdefRecord rec : msg.getRecords()) {
                if (rec.getTnf() == NdefRecord.TNF_WELL_KNOWN) {
                    byte[] type = rec.getType();
                    if (type.length == 1 && type[0] == 'T') {
                        byte[] payload = rec.getPayload();
                        if (payload.length < 1) continue;
                        String enc = ((payload[0] & 0x80) == 0) ? "UTF-8" : "UTF-16";
                        int langLen = payload[0] & 0x3F;
                        return new String(payload, 1 + langLen, payload.length - 1 - langLen, enc);
                    }
                }
            }
            return "";
        } catch (Exception e) {
            return "";
        }
    }

    private static NdefMessage readNdef(Tag tag) {
        try {
            android.nfc.tech.Ndef ndef = android.nfc.tech.Ndef.get(tag);
            if (ndef == null) return null;
            ndef.connect();
            NdefMessage msg = ndef.getNdefMessage();
            try { ndef.close(); } catch (Exception e) { }
            return msg;
        } catch (Exception e) {
            return null;
        }
    }

    private static CancellationSignal biometricCancel = null;

    public static int bioAvailable() {
        if (appContext == null) return 1;
        try {
            if (Build.VERSION.SDK_INT < 29) return 1;
            Object svc = appContext.getSystemService(Context.BIOMETRIC_SERVICE);
            if (svc == null) return 1;
            android.hardware.biometrics.BiometricManager bm = (android.hardware.biometrics.BiometricManager) svc;
            return bm.canAuthenticate() == android.hardware.biometrics.BiometricManager.BIOMETRIC_SUCCESS ? 0 : 1;
        } catch (Exception e) {
            return 1;
        }
    }

    public static void bioAuth(Activity activity, String title, String subtitle) {
        if (activity == null) return;
        try {
            if (Build.VERSION.SDK_INT < 29) {
                for (BluetoothCallback cb : callbacks) {
                    if (cb instanceof NativeCallback) ((NativeCallback) cb).onBiometric(false);
                }
                return;
            }
            if (biometricCancel != null) {
                try { biometricCancel.cancel(); } catch (Exception e) { }
            }
            biometricCancel = new CancellationSignal();
            java.util.concurrent.Executor exec = activity.getMainExecutor();
            android.hardware.biometrics.BiometricPrompt prompt = new android.hardware.biometrics.BiometricPrompt.Builder(activity)
                .setTitle(title != null ? title : "")
                .setSubtitle(subtitle != null ? subtitle : "")
                .setNegativeButton("Cancel", exec, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface dialog, int which) {
                    }
                })
                .build();
            prompt.authenticate(biometricCancel, exec, new android.hardware.biometrics.BiometricPrompt.AuthenticationCallback() {
                @Override public void onAuthenticationSucceeded(android.hardware.biometrics.BiometricPrompt.AuthenticationResult result) {
                    for (BluetoothCallback cb : callbacks) {
                        if (cb instanceof NativeCallback) ((NativeCallback) cb).onBiometric(true);
                    }
                }
                @Override public void onAuthenticationError(int errorCode, CharSequence errString) {
                    for (BluetoothCallback cb : callbacks) {
                        if (cb instanceof NativeCallback) ((NativeCallback) cb).onBiometric(false);
                    }
                }
                @Override public void onAuthenticationFailed() {
                }
            });
        } catch (Exception e) {
        }
    }

    private static LocationManager locManager = null;
    private static LocationListener locListener = null;

    public static boolean locAvailable() {
        if (appContext == null) return false;
        try {
            LocationManager lm = (LocationManager) appContext.getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) return false;
            return lm.isProviderEnabled(LocationManager.GPS_PROVIDER) || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean locStart(long minTimeMs, float minDistM) {
        if (appContext == null) return false;
        try {
            LocationManager lm = (LocationManager) appContext.getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) return false;
            if (locListener == null) {
                locListener = new LocationListener() {
                    @Override public void onLocationChanged(Location loc) {
                        if (loc == null) return;
                        for (BluetoothCallback cb : callbacks) {
                            if (cb instanceof NativeCallback) {
                                ((NativeCallback) cb).onLocation(loc.getLatitude(), loc.getLongitude(), loc.hasAccuracy() ? loc.getAccuracy() : -1, loc.getTime());
                            }
                        }
                    }
                    @Override public void onStatusChanged(String provider, int status, Bundle extras) {
                    }
                    @Override public void onProviderEnabled(String provider) {
                    }
                    @Override public void onProviderDisabled(String provider) {
                    }
                };
            }
            locManager = lm;
            boolean ok = false;
            try {
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, minTimeMs, minDistM, locListener, Looper.getMainLooper());
                ok = true;
            } catch (SecurityException e) {
            } catch (IllegalArgumentException e) {
            }
            try {
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, minTimeMs, minDistM, locListener, Looper.getMainLooper());
                ok = true;
            } catch (SecurityException e) {
            } catch (IllegalArgumentException e) {
            }
            return ok;
        } catch (Exception e) {
            return false;
        }
    }

    public static void locStop() {
        try {
            if (locManager != null && locListener != null) {
                try { locManager.removeUpdates(locListener); } catch (Exception e) { }
            }
        } catch (Exception e) {
        }
        locManager = null;
    }

    private static MediaRecorder mediaRecorder = null;
    private static AudioRecord audioRecord = null;
    private static Thread audioThread = null;
    private static String audioPath = null;
    private static volatile boolean audioRunning = false;

    public static boolean recStart(String path) {
        if (appContext == null || path == null) return false;
        try {
            recStop();
            int sampleRate = 44100;
            int channel = AudioFormat.CHANNEL_IN_MONO;
            int encoding = AudioFormat.ENCODING_PCM_16BIT;
            int minBuf = AudioRecord.getMinBufferSize(sampleRate, channel, encoding);
            if (minBuf <= 0) return false;
            final AudioRecord rec = new AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, channel, encoding, minBuf * 2);
            if (rec.getState() != AudioRecord.STATE_INITIALIZED) {
                try { rec.release(); } catch (Exception e) { }
                return false;
            }
            final java.io.RandomAccessFile raf = new java.io.RandomAccessFile(path, "rw");
            raf.setLength(0);
            raf.write(new byte[44]);
            audioRecord = rec;
            audioPath = path;
            audioRunning = true;
            audioThread = new Thread(new Runnable() {
                @Override public void run() {
                    try {
                        rec.startRecording();
                        byte[] buf = new byte[4096];
                        while (audioRunning) {
                            int n = rec.read(buf, 0, buf.length);
                            if (n > 0) {
                                synchronized (raf) {
                                    raf.write(buf, 0, n);
                                }
                            } else if (n < 0) {
                                break;
                            }
                        }
                    } catch (Exception e) {
                    }
                    try { rec.stop(); } catch (Exception e) { }
                    try { rec.release(); } catch (Exception e) { }
                }
            });
            audioThread.start();
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean recStop() {
        if (audioRecord == null && audioThread == null) return false;
        audioRunning = false;
        Thread t = audioThread;
        audioThread = null;
        if (t != null) {
            try { t.join(3000); } catch (Exception e) { }
        }
        audioRecord = null;
        if (audioPath == null) return false;
        try {
            java.io.RandomAccessFile raf = new java.io.RandomAccessFile(audioPath, "rw");
            long total = raf.length();
            long dataLen = total > 44 ? total - 44 : 0;
            raf.seek(0);
            raf.write(new byte[]{'R', 'I', 'F', 'F'});
            writeLe32(raf, (int) (36 + dataLen));
            raf.write(new byte[]{'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
            writeLe32(raf, 16);
            writeLe16(raf, 1);
            writeLe16(raf, 1);
            writeLe32(raf, 44100);
            writeLe32(raf, 44100 * 2);
            writeLe16(raf, 2);
            writeLe16(raf, 16);
            raf.write(new byte[]{'d', 'a', 't', 'a'});
            writeLe32(raf, (int) dataLen);
            raf.close();
            audioPath = null;
            return dataLen >= 0;
        } catch (Exception e) {
            audioPath = null;
            return false;
        }
    }

    private static void writeLe32(java.io.RandomAccessFile raf, int v) throws java.io.IOException {
        raf.write(v & 0xFF);
        raf.write((v >> 8) & 0xFF);
        raf.write((v >> 16) & 0xFF);
        raf.write((v >> 24) & 0xFF);
    }

    private static void writeLe16(java.io.RandomAccessFile raf, int v) throws java.io.IOException {
        raf.write(v & 0xFF);
        raf.write((v >> 8) & 0xFF);
    }

    public static boolean scAdd(String id, String shortLabel, String longLabel) {
        if (appContext == null || id == null) return false;
        try {
            if (Build.VERSION.SDK_INT < 25) return false;
            android.content.pm.ShortcutManager sm = (android.content.pm.ShortcutManager) appContext.getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null || !sm.isRequestPinShortcutSupported()) {
                if (sm == null) return false;
            }
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setPackage(appContext.getPackageName());
            android.content.pm.ShortcutInfo info = new android.content.pm.ShortcutInfo.Builder(appContext, id)
                .setShortLabel(shortLabel != null ? shortLabel : id)
                .setLongLabel(longLabel != null ? longLabel : (shortLabel != null ? shortLabel : id))
                .setIntent(intent)
                .build();
            sm.pushDynamicShortcut(info);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean scRemove(String id) {
        if (appContext == null || id == null) return false;
        try {
            if (Build.VERSION.SDK_INT < 25) return false;
            android.content.pm.ShortcutManager sm = (android.content.pm.ShortcutManager) appContext.getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return false;
            List<String> ids = new ArrayList<>();
            ids.add(id);
            sm.removeDynamicShortcuts(ids);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static int scCount() {
        if (appContext == null) return 0;
        try {
            if (Build.VERSION.SDK_INT < 25) return 0;
            android.content.pm.ShortcutManager sm = (android.content.pm.ShortcutManager) appContext.getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return 0;
            return sm.getDynamicShortcuts().size();
        } catch (Exception e) {
            return 0;
        }
    }

    public static boolean pipEnter(Activity activity, int w, int h) {
        if (activity == null) return false;
        try {
            if (Build.VERSION.SDK_INT < 26) return false;
            if (!activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE)) return false;
            if (w <= 0) w = 16;
            if (h <= 0) h = 9;
            PictureInPictureParams params = new PictureInPictureParams.Builder()
                .setAspectRatio(new Rational(w, h))
                .build();
            return activity.enterPictureInPictureMode(params);
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean pipAvailable() {
        if (appContext == null) return false;
        try {
            if (Build.VERSION.SDK_INT < 26) return false;
            return appContext.getPackageManager().hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE);
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean wpSetImage(String path) {
        if (appContext == null || path == null) return false;
        try {
            android.graphics.Bitmap bmp = android.graphics.BitmapFactory.decodeFile(path);
            if (bmp == null) return false;
            WallpaperManager wm = WallpaperManager.getInstance(appContext);
            wm.setBitmap(bmp);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static int volMusicGet() {
        if (appContext == null) return 0;
        try {
            AudioManager am = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return 0;
            return am.getStreamVolume(AudioManager.STREAM_MUSIC);
        } catch (Exception e) {
            return 0;
        }
    }

    public static int volMusicMax() {
        if (appContext == null) return 0;
        try {
            AudioManager am = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return 0;
            return am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        } catch (Exception e) {
            return 0;
        }
    }

    public static void volMusicSet(int level) {
        if (appContext == null) return;
        try {
            AudioManager am = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return;
            int max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
            if (level < 0) level = 0;
            if (level > max) level = max;
            am.setStreamVolume(AudioManager.STREAM_MUSIC, level, 0);
        } catch (Exception e) {
        }
    }

    public static int ringerGet() {
        if (appContext == null) return 2;
        try {
            AudioManager am = (AudioManager) appContext.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return 2;
            return am.getRingerMode();
        } catch (Exception e) {
            return 2;
        }
    }

    public static boolean downloadToFile(String urlStr, String destPath) {
        if (urlStr == null || destPath == null) return false;
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlStr);
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(30000);
            conn.setInstanceFollowRedirects(true);
            conn.setRequestProperty("User-Agent", "AromaUI/1.0");
            conn.connect();
            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) return false;
            int total = conn.getContentLength();
            if (total > 16777216) return false;
            InputStream in = conn.getInputStream();
            File outFile = new File(destPath);
            File parent = outFile.getParentFile();
            if (parent != null) parent.mkdirs();
            File tmpFile = new File(destPath + ".part");
            FileOutputStream out = new FileOutputStream(tmpFile);
            byte[] buf = new byte[32768];
            int n;
            long got = 0;
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
                got += n;
                if (got > 16777216) {
                    out.close();
                    in.close();
                    tmpFile.delete();
                    return false;
                }
            }
            out.close();
            in.close();
            if (got == 0) {
                tmpFile.delete();
                return false;
            }
            return tmpFile.renameTo(outFile);
        } catch (Exception e) {
            return false;
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    private static View sImeView = null;

    public static void showIme(final Activity activity) {
        if (activity == null) return;
        activity.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    if (sImeView == null) {
                        final Activity act = activity;
                        sImeView = new View(act) {
                            @Override
                            public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
                                outAttrs.actionLabel = null;
                                outAttrs.inputType = android.text.InputType.TYPE_CLASS_TEXT;
                                outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE;
                                return new BaseInputConnection(this, true) {
                                    private String mComposing = "";
                                    @Override
                                    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                                        try {
                                            for (int i = 0; i < beforeLength; i++) {
                                                AromaActivity.imeBackspace();
                                            }
                                            return beforeLength > 0;
                                        } catch (Exception e) {
                                            return false;
                                        }
                                    }
                                    @Override
                                    public boolean commitText(CharSequence text, int newCursorPosition) {
                                        try {
                                            if (text == null || text.length() == 0) return false;
                                            mComposing = "";
                                            AromaActivity.imeText(text.toString());
                                            return true;
                                        } catch (Exception e) {
                                            return false;
                                        }
                                    }
                                    @Override
                                    public boolean setComposingText(CharSequence text, int newCursorPosition) {
                                        try {
                                            String cur = text == null ? "" : text.toString();
                                            String old = mComposing;
                                            int common = 0;
                                            while (common < old.length() && common < cur.length() && old.charAt(common) == cur.charAt(common)) {
                                                common++;
                                            }
                                            for (int i = common; i < old.length(); i++) {
                                                AromaActivity.imeBackspace();
                                            }
                                            if (common < cur.length()) {
                                                AromaActivity.imeText(cur.substring(common));
                                            }
                                            mComposing = cur;
                                            return true;
                                        } catch (Exception e) {
                                            return false;
                                        }
                                    }
                                    @Override
                                    public boolean finishComposingText() {
                                        mComposing = "";
                                        return true;
                                    }
                                    @Override
                                    public boolean setComposingRegion(int start, int end) {
                                        return true;
                                    }
                                    @Override
                                    public boolean performEditorAction(int actionCode) {
                                        try {
                                            AromaActivity.imeDone();
                                        } catch (Exception e) {
                                        }
                                        return true;
                                    }
                                    @Override
                                    public boolean sendKeyEvent(KeyEvent event) {
                                        try {
                                            if (event != null && event.getAction() == KeyEvent.ACTION_DOWN) {
                                                if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
                                                    AromaActivity.imeBackspace();
                                                    return true;
                                                }
                                                if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
                                                    AromaActivity.imeDone();
                                                    return true;
                                                }
                                                int uni = event.getUnicodeChar();
                                                if (uni >= 32 && uni <= 126) {
                                                    AromaActivity.imeText(String.valueOf((char) uni));
                                                    return true;
                                                }
                                            } else if (event != null) {
                                                return true;
                                            }
                                            act.dispatchKeyEvent(event);
                                            return true;
                                        } catch (Exception e) {
                                            return false;
                                        }
                                    }
                                };
                            }
                        };
                        sImeView.setFocusable(true);
                        sImeView.setFocusableInTouchMode(true);
                        sImeView.setLayoutParams(new android.view.ViewGroup.LayoutParams(8, 8));
                    }
                    android.view.ViewGroup decor = null;
                    try {
                        decor = (android.view.ViewGroup) activity.getWindow().getDecorView();
                    } catch (Exception e) {
                        decor = null;
                    }
                    if (decor != null && sImeView.getParent() == null) {
                        decor.addView(sImeView);
                    }
                    boolean gotFocus = sImeView.requestFocus();
                    android.util.Log.i("AromaIME", "focus=" + gotFocus + " attached=" + (sImeView.getParent() != null));
                    Object imm = activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE);
                    if (imm instanceof InputMethodManager) {
                        ((InputMethodManager) imm).showSoftInput(sImeView, 0);
                    }
                } catch (Exception e) {
                }
            }
        });
    }

    public static void hideIme(final Activity activity) {
        if (activity == null) return;
        activity.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    if (sImeView != null) {
                        Object imm = activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE);
                        if (imm instanceof InputMethodManager) {
                            ((InputMethodManager) imm).hideSoftInputFromWindow(sImeView.getWindowToken(), 0);
                        }
                        sImeView.clearFocus();
                    }
                } catch (Exception e) {
                }
            }
        });
    }

    public static long storageFreeMb() {
        if (appContext == null) return 0;
        try {
            StatFs stat = new StatFs(appContext.getFilesDir().getAbsolutePath());
            return stat.getAvailableBytes() / 1048576L;
        } catch (Exception e) {
            return 0;
        }
    }

    public static long storageTotalMb() {
        if (appContext == null) return 0;
        try {
            StatFs stat = new StatFs(appContext.getFilesDir().getAbsolutePath());
            return stat.getTotalBytes() / 1048576L;
        } catch (Exception e) {
            return 0;
        }
    }

    public static int sysbarStatusPx(Activity activity) {
        if (activity == null) return 0;
        try {
            int id = activity.getResources().getIdentifier("status_bar_height", "dimen", "android");
            if (id <= 0) return 0;
            return activity.getResources().getDimensionPixelSize(id);
        } catch (Exception e) {
            return 0;
        }
    }

    public static int sysbarNavPx(Activity activity) {
        if (activity == null) return 0;
        try {
            int id = activity.getResources().getIdentifier("navigation_bar_height", "dimen", "android");
            if (id <= 0) return 0;
            return activity.getResources().getDimensionPixelSize(id);
        } catch (Exception e) {
            return 0;
        }
    }

    public static boolean kbVisible(Activity activity) {
        if (activity == null) return false;
        try {
            View decor = activity.getWindow().getDecorView();
            if (decor == null) return false;
            if (Build.VERSION.SDK_INT >= 30) {
                android.view.WindowInsets insets = decor.getRootWindowInsets();
                if (insets == null) return false;
                return insets.isVisible(android.view.WindowInsets.Type.ime());
            }
            android.graphics.Rect rect = new android.graphics.Rect();
            decor.getWindowVisibleDisplayFrame(rect);
            int screenH = decor.getRootView().getHeight();
            return (screenH - rect.bottom) > screenH * 0.15;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean appInstalled(String pkg) {
        if (appContext == null || pkg == null) return false;
        try {
            appContext.getPackageManager().getPackageInfo(pkg, 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean openUrl(String url) {
        if (appContext == null || url == null) return false;
        try {
            String u = url.trim();
            if (!u.contains("://")) {
                u = "https://" + u;
            }
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(u));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            appContext.startActivity(intent);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean appOpen(String pkg) {
        if (appContext == null || pkg == null) return false;
        try {
            Intent launch = appContext.getPackageManager().getLaunchIntentForPackage(pkg);
            if (launch == null) return false;
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            appContext.startActivity(launch);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static String contactsResolve(Activity activity, Uri uri) {
        if (activity == null || uri == null) return "";
        Cursor cursor = null;
        try {
            ContentResolver cr = activity.getContentResolver();
            cursor = cr.query(uri, null, null, null, null);
            if (cursor == null || !cursor.moveToFirst()) return "";
            int nameIdx = cursor.getColumnIndex(ContactsContract.Contacts.DISPLAY_NAME);
            String name = nameIdx >= 0 ? cursor.getString(nameIdx) : "";
            String number = "";
            int idIdx = cursor.getColumnIndex(ContactsContract.Contacts._ID);
            int hasPhoneIdx = cursor.getColumnIndex(ContactsContract.Contacts.HAS_PHONE_NUMBER);
            if (idIdx >= 0 && hasPhoneIdx >= 0 && cursor.getInt(hasPhoneIdx) > 0) {
                String contactId = cursor.getString(idIdx);
                Cursor phones = null;
                try {
                    phones = cr.query(ContactsContract.CommonDataKinds.Phone.CONTENT_URI, null,
                        ContactsContract.CommonDataKinds.Phone.CONTACT_ID + " = ?", new String[]{contactId}, null);
                    if (phones != null && phones.moveToFirst()) {
                        int numIdx = phones.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER);
                        if (numIdx >= 0) number = phones.getString(numIdx);
                    }
                } catch (Exception e) {
                } finally {
                    if (phones != null) {
                        try { phones.close(); } catch (Exception e) { }
                    }
                }
            }
            return (name != null ? name : "") + "\n" + (number != null ? number : "");
        } catch (Exception e) {
            return "";
        } finally {
            if (cursor != null) {
                try { cursor.close(); } catch (Exception e) { }
            }
        }
    }

    public static void shotCapture(final Activity activity) {
        if (activity == null) return;
        try {
            final View view = activity.getWindow().getDecorView().getRootView();
            final int w = view.getWidth();
            final int h = view.getHeight();
            if (w <= 0 || h <= 0) {
                for (BluetoothCallback cb : callbacks) {
                    if (cb instanceof NativeCallback) ((NativeCallback) cb).onScreenshot(null);
                }
                return;
            }
            final android.graphics.Bitmap bmp = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
            PixelCopy.request(activity.getWindow(), bmp, new PixelCopy.OnPixelCopyFinishedListener() {
                @Override public void onPixelCopyFinished(int copyResult) {
                    String path = null;
                    if (copyResult == PixelCopy.SUCCESS) {
                        path = saveBitmapToCache(activity, bmp, "aroma_shot_");
                    } else {
                        bmp.recycle();
                    }
                    final String done = path;
                    for (BluetoothCallback cb : callbacks) {
                        if (cb instanceof NativeCallback) ((NativeCallback) cb).onScreenshot(done);
                    }
                }
            }, new Handler(Looper.getMainLooper()));
        } catch (Exception e) {
            for (BluetoothCallback cb : callbacks) {
                try {
                    if (cb instanceof NativeCallback) ((NativeCallback) cb).onScreenshot(null);
                } catch (Exception ignored) { }
            }
        }
    }

    public static boolean irAvailable() {
        if (appContext == null) return false;
        try {
            ConsumerIrManager ir = (ConsumerIrManager) appContext.getSystemService(Context.CONSUMER_IR_SERVICE);
            return ir != null && ir.hasIrEmitter();
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean irTransmit(int frequency, int[] pattern) {
        if (appContext == null || pattern == null) return false;
        try {
            ConsumerIrManager ir = (ConsumerIrManager) appContext.getSystemService(Context.CONSUMER_IR_SERVICE);
            if (ir == null || !ir.hasIrEmitter()) return false;
            ir.transmit(frequency, pattern);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}