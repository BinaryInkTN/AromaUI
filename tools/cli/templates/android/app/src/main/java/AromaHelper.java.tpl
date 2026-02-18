import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothHeadset;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
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
    
    public interface BluetoothCallback {
        void onConnectionResult(boolean success, String deviceName, int deviceType, int mode);
        void onDataReceived(byte[] data, int length);
        void onConnectionStateChanged(int state);
        void onDeviceDiscovered(String address, String name, int type, int rssi);
        void onScanFinished();
        void onPairingResult(boolean success, String address, String name);
    }
    
    private static List<BluetoothCallback> callbacks = new CopyOnWriteArrayList<>();
    
    public static class NativeCallback implements BluetoothCallback {
        static {
            System.loadLibrary("aroma_app");
        }
        
        public native void onDeviceDiscovered(String address, String name, int type, int rssi);
        public native void onScanFinished();
        public native void onPairingResult(boolean success, String address, String name);
        public native void onConnectionResult(boolean success, String deviceName, int deviceType, int mode);
        public native void onDataReceived(byte[] data, int length);
        public native void onConnectionStateChanged(int state);
        
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
}