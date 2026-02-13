import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.widget.Toast;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

public class AromaHelper {
    private static final UUID BT_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    
    private static BluetoothSocket btSocket = null;
    private static InputStream btInputStream = null;
    private static OutputStream btOutputStream = null;
    private static BluetoothAdapter btAdapter = null;
    private static ConnectThread connectThread = null;
    
    public static void showToast(Activity activity, String msg, boolean longDuration) {
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
    
    public static String[] btGetPairedDevices() {
        ArrayList<String> devices = new ArrayList<>();
        System.out.println("Called btGetPairedDevices");
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null) {
                return new String[0];
            }
            
            Set<BluetoothDevice> pairedDevices = btAdapter.getBondedDevices();
            if (pairedDevices.size() > 0) {
                for (BluetoothDevice device : pairedDevices) {
                    System.out.println("Found paired device: " + device.getName() + " [" + device.getAddress() + "]");
                    devices.add(device.getAddress() + ";" + device.getName());
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        
        return devices.toArray(new String[0]);
    }
    
    public static boolean btConnect(String address) {
        btDisconnect();
        
        try {
            btAdapter = BluetoothAdapter.getDefaultAdapter();
            if (btAdapter == null) return false;
            
            BluetoothDevice device = btAdapter.getRemoteDevice(address);
            if (device == null) return false;
            
            connectThread = new ConnectThread(device);
            connectThread.start();
            
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            
            return btIsConnected();
            
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }
    
    public static void btDisconnect() {
        try {
            if (btInputStream != null) {
                btInputStream.close();
                btInputStream = null;
            }
            if (btOutputStream != null) {
                btOutputStream.close();
                btOutputStream = null;
            }
            if (btSocket != null) {
                btSocket.close();
                btSocket = null;
            }
            if (connectThread != null) {
                connectThread.interrupt();
                connectThread = null;
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
    
    public static int btSend(byte[] data) {
        if (!btIsConnected() || btOutputStream == null) return -1;
        
        try {
            btOutputStream.write(data);
            btOutputStream.flush();
            return data.length;
        } catch (IOException e) {
            e.printStackTrace();
            btDisconnect();
            return -1;
        }
    }
    
    public static boolean btIsConnected() {
        return btSocket != null && btSocket.isConnected() && 
               btInputStream != null && btOutputStream != null;
    }
    
    private static class ConnectThread extends Thread {
        private final BluetoothDevice device;
        
        public ConnectThread(BluetoothDevice device) {
            this.device = device;
        }
        
        @Override
        public void run() {
            try {
                if (btAdapter != null) {
                    btAdapter.cancelDiscovery();
                }
                
                btSocket = device.createRfcommSocketToServiceRecord(BT_UUID);
                btSocket.connect();
                btInputStream = btSocket.getInputStream();
                btOutputStream = btSocket.getOutputStream();
                
            } catch (IOException e) {
                e.printStackTrace();
                
                try {
                    if (btSocket != null) btSocket.close();
                } catch (IOException e2) {
                    e2.printStackTrace();
                }
                btSocket = null;
                btInputStream = null;
                btOutputStream = null;
            }
        }
    }
}